#include "interpreter.h"
#include "builtins.h"
#include "utils.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

static volatile sig_atomic_t interrupted = 0;

static void handle_sigint(int sig) {
    (void)sig;
    interrupted = 1;
}

static char *malloc_or_null(size_t n) {
    if (n == 0) return NULL;
    return (char *)malloc(n);
}

static void *calloc_or_null(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) return NULL;
    return calloc(nmemb, size);
}

static char *duplicate_string_safe(const char *s) {
    return dh_strdup(s);
}

static char *trim_string_alloc(const char *s) {
    if (!s) return NULL;
    const unsigned char *start = (const unsigned char *)s;
    while (*start && isspace(*start)) start++;
    const unsigned char *end = (const unsigned char *)s + strlen(s);
    while (end > start && isspace(*(end - 1))) end--;
    size_t len = (size_t)(end - start);
    char *out = malloc_or_null(len + 1);
    if (!out) return NULL;
    if (len) memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static int string_is_blank(const char *s) {
    if (!s) return 1;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) if (!isspace(*p)) return 0;
    return 1;
}

static int is_exact_command(const char *a, const char *b) {
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}

static int is_prefixed_by(const char *s, const char *pref) {
    if (!s || !pref) return 0;
    size_t L = strlen(pref);
    return strncmp(s, pref, L) == 0;
}

static int statement_starts(const char *s) {
    if (!s) return 0;
    const char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '\0') return 0;
    const char *keywords[] = {
        "let","fn","func","extern","loop","if","return","while","for","break","continue","struct","import"
    };
    for (size_t i = 0; i < sizeof(keywords)/sizeof(keywords[0]); ++i) {
        size_t L = strlen(keywords[i]);
        if (strncmp(p, keywords[i], L) == 0) {
            char c = p[L];
            if (c == '\0' || isspace((unsigned char)c) || c == '(' || c == '{' || c == ';') return 1;
        }
    }
    const char *eq = strchr(p, '=');
    if (eq) {
        const char *before = p;
        while (before < eq && isspace((unsigned char)*before)) ++before;
        if (before < eq) return 1;
    }
    return 0;
}

static int ends_with_semicolon_or_brace(const char *s) {
    if (!s) return 0;
    size_t len = strlen(s);
    if (len == 0) return 0;
    size_t i = len;
    while (i > 0 && isspace((unsigned char)s[i - 1])) i--;
    if (i == 0) return 0;
    char last = s[i - 1];
    return last == ';' || last == '}';
}

static char *join_lines_safe(const char *a, const char *b) {
    if (!a && !b) return NULL;
    if (!a) return duplicate_string_safe(b);
    if (!b) return duplicate_string_safe(a);
    size_t la = strlen(a);
    size_t lb = strlen(b);
    char *out = malloc_or_null(la + lb + 2);
    if (!out) return NULL;
    memcpy(out, a, la);
    out[la] = '\n';
    memcpy(out + la + 1, b, lb);
    out[la + 1 + lb] = '\0';
    return out;
}

static int safe_getline(char **lineptr, size_t *n, FILE *stream) {
    if (!lineptr || !n || !stream) return -1;
    char *buf = *lineptr;
    size_t cap = *n;
    if (!buf || cap == 0) {
        cap = 512;
        buf = malloc_or_null(cap);
        if (!buf) return -1;
        *lineptr = buf;
        *n = cap;
    }
    size_t len = 0;
    int c;
    while ((c = fgetc(stream)) != EOF) {
        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            char *tmp = realloc(buf, ncap);
            if (!tmp) {
                return -1;
            }
            buf = tmp;
            cap = ncap;
            *lineptr = buf;
            *n = cap;
        }
        buf[len++] = (char)c;
        if (c == '\n') break;
    }
    if (len == 0 && c == EOF) return -1;
    buf[len] = '\0';
    return (int)len;
}

static int read_file_to_string_safe(const char *path, char **out) {
    if (!path || !out) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char *buf = NULL;
    size_t cap = 0;
    size_t len = 0;
    char tmp[4096];
    while (1) {
        size_t r = fread(tmp, 1, sizeof(tmp), f);
        if (r > 0) {
            if (len + r + 1 > cap) {
                size_t newcap = (cap == 0) ? (len + r + 1) : cap * 2;
                while (newcap < len + r + 1) newcap *= 2;
                char *nb = realloc(buf, newcap);
                if (!nb) {
                    free(buf);
                    fclose(f);
                    return 0;
                }
                buf = nb;
                cap = newcap;
            }
            memcpy(buf + len, tmp, r);
            len += r;
        }
        if (r < sizeof(tmp)) break;
    }
    if (ferror(f)) {
        free(buf);
        fclose(f);
        return 0;
    }
    if (!buf) {
        buf = malloc_or_null(1);
        if (!buf) { fclose(f); return 0; }
        buf[0] = '\0';
    } else {
        buf[len] = '\0';
    }
    fclose(f);
    *out = buf;
    return 1;
}

static int execute_source_string(const char *source, Env *env) {
    if (!source || !env) return 1;
    Node *program = parse_program(source);
    if (!program) {
        fprintf(stderr, "Error: Parse failed\n");
        return 1;
    }
    int r = execute_program(program, env);
    free_node(program);
    return r;
}

static int execute_file_if_exists(const char *path, Env *env) {
    if (!path || !env) return 1;
    char *src = NULL;
    if (!read_file_to_string_safe(path, &src)) return 1;
    Node *program = parse_program(src);
    free(src);
    if (!program) return 1;
    int r = execute_program(program, env);
    free_node(program);
    return r;
}

typedef struct {
    char **lines;
    size_t capacity;
    size_t count;
    size_t start;
} History;

static History *history_new(size_t cap) {
    if (cap == 0) cap = 256;
    History *h = calloc_or_null(1, sizeof(History));
    if (!h) return NULL;
    h->capacity = cap;
    h->lines = calloc_or_null(h->capacity, sizeof(char *));
    if (!h->lines) { free(h); return NULL; }
    h->count = 0;
    h->start = 0;
    return h;
}

static void history_add(History *h, const char *line) {
    if (!h || !line) return;
    char *copy = duplicate_string_safe(line);
    if (!copy) return;
    if (h->count < h->capacity) {
        size_t idx = (h->start + h->count) % h->capacity;
        h->lines[idx] = copy;
        h->count++;
        return;
    }
    size_t idx = h->start;
    if (h->lines[idx]) free(h->lines[idx]);
    h->lines[idx] = copy;
    h->start = (h->start + 1) % h->capacity;
}

static void history_free(History *h) {
    if (!h) return;
    for (size_t i = 0; i < h->count; ++i) {
        size_t idx = (h->start + i) % h->capacity;
        if (h->lines[idx]) free(h->lines[idx]);
    }
    free(h->lines);
    free(h);
}

static void print_banner(void) {
    const char *vs = version_string ? version_string() : "0.0.0";
    const char *vb = version_build ? version_build() : "unknown";
    printf("Dusth %s (%s)\n", vs, vb);
    printf("Type \"help\", \"credits\", \"license\", or \"exit\".\n");
}

static void print_help(void) {
    printf("Dusth commands:\n");
    printf("  help            Show this help\n");
    printf("  credits         Show credits\n");
    printf("  license         Show license\n");
    printf("  exit / quit     Quit REPL\n");
    printf("  -v / --version  Show version\n");
}

static void print_credits(void) {
    const char *vs = version_string ? version_string() : "0.0.0";
    printf("Dusth %s - created by Viren Sahti\n", vs);
}

static void print_license(void) {
    printf("Please read the license on our repo in /docs!\n");
}

static void print_prompt(const char *p) {
    if (!p) p = ">>> ";
    fputs(p, stdout);
    fflush(stdout);
}

static int handle_command_line(const char *line, Env *env) {
    if (!line || !env) return 0;
    if (is_exact_command(line, "exit") || is_exact_command(line, "quit")) return 2;
    if (is_exact_command(line, "help")) { print_help(); return 1; }
    if (is_exact_command(line, "credits")) { print_credits(); return 1; }
    if (is_exact_command(line, "license")) { print_license(); return 1; }
    if (is_exact_command(line, "-v") || is_exact_command(line, "--version")) {
        const char *vs = version_string ? version_string() : "0.0.0";
        puts(vs);
        return 1;
    }
    return 0;
}

static int process_repl_line(char *rawline, char **linebuf, size_t *linecap, Env *env, History *hist) {
    if (!rawline || !env) return 0;
    char *trimmed = trim_string_alloc(rawline);
    if (!trimmed) return 0;
    if (string_is_blank(trimmed)) { free(trimmed); return 0; }
    int cmd = handle_command_line(trimmed, env);
    if (cmd == 2) { free(trimmed); return 2; }
    if (cmd == 1) { free(trimmed); return 0; }
    history_add(hist, trimmed);
    int stmt = statement_starts(trimmed);
    int ended = ends_with_semicolon_or_brace(trimmed);
    char *source = NULL;
    if (is_prefixed_by(trimmed, "extern") || ended) {
        source = duplicate_string_safe(trimmed);
    } else {
        if (!stmt) {
            char *accum = duplicate_string_safe(trimmed);
            if (!accum) { free(trimmed); return 0; }
            for (;;) {
                print_prompt("... ");
                int r = safe_getline(linebuf, linecap, stdin);
                if (r == -1) break;
                if ((*linebuf)[r - 1] == '\n') (*linebuf)[r - 1] = '\0';
                char *piece = trim_string_alloc(*linebuf);
                if (!piece) { free(accum); break; }
                char *joined = join_lines_safe(accum, piece);
                free(accum);
                free(piece);
                accum = joined;
                if (!accum) break;
                if (ends_with_semicolon_or_brace(accum)) break;
            }
            if (!accum) { free(trimmed); return 0; }
            source = duplicate_string_safe(accum);
            free(accum);
        } else {
            size_t len = strlen(trimmed) + 2;
            char *temp = malloc_or_null(len);
            if (!temp) { free(trimmed); fprintf(stderr, "Error: Out of memory\n"); return 0; }
            strcpy(temp, trimmed);
            strcat(temp, ";");
            source = temp;
        }
    }
    if (!source) { free(trimmed); return 0; }
    int res = execute_source_string(source, env);
    free(source);
    free(trimmed);
    return res;
}

int main(int argc, char **argv) {
    signal(SIGINT, handle_sigint);
    Env *env = global_env();
    if (!env) {
        fprintf(stderr, "Failed to initialize global environment\n");
        return 1;
    }
    register_builtins(env);
    if (argc >= 2) {
        if (argc == 2) {
            if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
                const char *vs = version_string ? version_string() : "0.0.0";
                printf("%s\n", vs);
                return 0;
            }
            if (strcmp(argv[1], "credits") == 0) { print_credits(); return 0; }
            if (strcmp(argv[1], "license") == 0) { print_license(); return 0; }
            int r = execute_file_if_exists(argv[1], env);
            return r == 0 ? 0 : 1;
        } else {
            fprintf(stderr, "Usage: dusth [script.dth]\n");
            return 1;
        }
    }
    char *linebuf = NULL;
    size_t linecap = 0;
    print_banner();
    History *hist = history_new(1024);
    if (!hist) hist = history_new(256);
    for (;;) {
        if (interrupted) {
            printf("\nInterrupted\n");
            interrupted = 0;
        }
        print_prompt(">>> ");
        int n = safe_getline(&linebuf, &linecap, stdin);
        if (n == -1) {
            if (feof(stdin)) break;
            clearerr(stdin);
            continue;
        }
        if (n > 0 && linebuf[n - 1] == '\n') linebuf[n - 1] = '\0';
        int r = process_repl_line(linebuf, &linebuf, &linecap, env, hist);
        if (r == 2) break;
    }
    history_free(hist);
    free(linebuf);
    return 0;
}