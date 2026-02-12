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
#include <stdarg.h>
#include <time.h>

static volatile sig_atomic_t interrupted = 0;

static void handle_sigint(int sig) {
    (void)sig;
    interrupted = 1;
}

static char* duplicate_string_safe(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char* p = (char*)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

static char* malloc_or_null(size_t n) {
    if (n == 0) return NULL;
    void* p = malloc(n);
    return p ? (char*)p : NULL;
}

static void* calloc_or_die(size_t nmemb, size_t size) {
    void* p = calloc(nmemb, size);
    if (!p) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    return p;
}

static char* trim_string_alloc(const char* s) {
    if (!s) return NULL;
    const char* start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    const char* end = s + strlen(s);
    while (end > start && isspace((unsigned char)*(end - 1))) end--;
    size_t len = (size_t)(end - start);
    char* out = malloc_or_null(len + 1);
    if (!out) return NULL;
    if (len) memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static char* safe_strdup_n(const char* s, size_t n) {
    if (!s) return NULL;
    char* p = malloc_or_null(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static int string_is_blank(const char* s) {
    if (!s) return 1;
    for (const char* p = s; *p; ++p) if (!isspace((unsigned char)*p)) return 0;
    return 1;
}

static int statement_starts(const char* s) {
    if (!s) return 0;
    const char* p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '\0') return 0;
    const char* keywords[] = {
        "let","fn","func","extern","loop","if","return","while","for","break","continue","struct","import"
    };
    for (size_t i = 0; i < sizeof(keywords)/sizeof(keywords[0]); ++i) {
        size_t L = strlen(keywords[i]);
        if (strncmp(p, keywords[i], L) == 0) {
            char c = p[L];
            if (c == '\0' || isspace((unsigned char)c) || c == '(' || c == '{' || c == ';') return 1;
        }
    }
    const char* eq = strchr(p, '=');
    if (eq) {
        const char* before = p;
        while (before < eq && isspace((unsigned char)*before)) ++before;
        if (before < eq) return 1;
    }
    return 0;
}

static int ends_with_semicolon_or_brace(const char* s) {
    if (!s) return 0;
    size_t len = strlen(s);
    if (len == 0) return 0;
    size_t i = len;
    while (i > 0 && isspace((unsigned char)s[i - 1])) i--;
    if (i == 0) return 0;
    char last = s[i - 1];
    return last == ';' || last == '}';
}

static char* join_lines_safe(const char* a, const char* b) {
    if (!a && !b) return NULL;
    if (!a) return duplicate_string_safe(b);
    if (!b) return duplicate_string_safe(a);
    size_t la = strlen(a);
    size_t lb = strlen(b);
    char* out = malloc_or_null(la + lb + 2);
    if (!out) return NULL;
    memcpy(out, a, la);
    out[la] = '\n';
    memcpy(out + la + 1, b, lb);
    out[la + 1 + lb] = '\0';
    return out;
}

static void print_banner(void) {
    const char* vs = version_string ? version_string() : "0.0.0";
    const char* vb = version_build ? version_build() : "unknown";
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
    const char* vs = version_string ? version_string() : "0.0.0";
    printf("Dusth %s - created by Viren Sahti\n", vs);
}

static void print_license(void) {
    printf(
"Please read the license on our repo in /docs!\n"
    );
}

static int safe_getline(char** lineptr, size_t* n, FILE* stream) {
    if (!lineptr || !n || !stream) return -1;
    char* buf = *lineptr;
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
            char* tmp = realloc(buf, ncap);
            if (!tmp) return -1;
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

static int is_exact_command(const char* a, const char* b) {
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}

static int is_prefixed_by(const char* s, const char* pref) {
    if (!s || !pref) return 0;
    size_t L = strlen(pref);
    return strncmp(s, pref, L) == 0;
}

static int read_more_lines_for_block(char** out_source, const char* initial, char** linebuf, size_t* linecap) {
    if (!initial) return 0;
    char* accum = duplicate_string_safe(initial);
    if (!accum) return 0;
    int ok = 0;
    for (;;) {
        printf("... ");
        fflush(stdout);
        int r = safe_getline(linebuf, linecap, stdin);
        if (r == -1) {
            break;
        }
        if ((*linebuf)[r - 1] == '\n') (*linebuf)[r - 1] = '\0';
        char* piece_trim = trim_string_alloc(*linebuf);
        if (!piece_trim) {
            free(accum);
            return 0;
        }
        char* joined = join_lines_safe(accum, piece_trim);
        free(accum);
        free(piece_trim);
        accum = joined;
        if (!accum) return 0;
        if (ends_with_semicolon_or_brace(accum)) { ok = 1; break; }
    }
    if (ok) {
        *out_source = accum;
    } else {
        free(accum);
        *out_source = NULL;
    }
    return ok;
}

static int execute_source_string(const char* source, Env* env) {
    if (!source || !env) return 1;
    Node* program = parse_program(source);
    if (!program) {
        fprintf(stderr, "Error: Parse failed\n");
        return 1;
    }
    int r = execute_program(program, env);
    free_node(program);
    return r;
}

static char* read_file_to_string_safe(const char* path) {
    if (!path) return NULL;
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char* buf = malloc_or_null((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    if (got != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[got] = '\0';
    fclose(f);
    return buf;
}

static int execute_file_if_exists(const char* path, Env* env) {
    if (!path || !env) return 1;
    Node* program = NULL;
    char* src = read_file_to_string_safe(path);
    if (!src) return 1;
    program = parse_program(src);
    free(src);
    if (!program) return 1;
    int r = execute_program(program, env);
    free_node(program);
    return r;
}

typedef struct {
    char** lines;
    size_t capacity;
    size_t count;
    size_t head;
} History;

static History* history_new(size_t cap) {
    History* h = calloc_or_die(1, sizeof(History));
    h->capacity = cap ? cap : 256;
    h->lines = calloc_or_die(h->capacity, sizeof(char*));
    h->count = 0;
    h->head = 0;
    return h;
}

static void history_add(History* h, const char* line) {
    if (!h || !line) return;
    char* copy = duplicate_string_safe(line);
    if (!copy) return;
    if (h->count < h->capacity) {
        h->lines[h->count++] = copy;
        return;
    }
    free(h->lines[h->head]);
    h->lines[h->head] = copy;
    h->head = (h->head + 1) % h->capacity;
}

static void history_free(History* h) {
    if (!h) return;
    for (size_t i = 0; i < h->count; ++i) {
        size_t idx = (h->head + i) % h->capacity;
        if (h->lines[idx]) free(h->lines[idx]);
    }
    free(h->lines);
    free(h);
}

static void print_prompt(const char* p) {
    if (!p) p = ">>> ";
    fputs(p, stdout);
    fflush(stdout);
}

static int handle_command_line(const char* line, Env* env) {
    if (!line || !env) return 0;
    if (is_exact_command(line, "exit") || is_exact_command(line, "quit")) return 2;
    if (is_exact_command(line, "help")) { print_help(); return 1; }
    if (is_exact_command(line, "credits")) { print_credits(); return 1; }
    if (is_exact_command(line, "license")) { print_license(); return 1; }
    if (is_exact_command(line, "-v") || is_exact_command(line, "--version")) {
        const char* vs = version_string ? version_string() : "0.0.0";
        puts(vs);
        return 1;
    }
    return 0;
}

static int process_repl_line(char* rawline, char** linebuf, size_t* linecap, Env* env, History* hist) {
    if (!rawline) return 0;
    char* trimmed = trim_string_alloc(rawline);
    if (!trimmed) return 0;
    if (string_is_blank(trimmed)) { free(trimmed); return 0; }
    int cmd = handle_command_line(trimmed, env);
    if (cmd == 2) { free(trimmed); return 2; }
    if (cmd == 1) { free(trimmed); return 0; }
    history_add(hist, trimmed);
    int stmt = statement_starts(trimmed);
    int ended = ends_with_semicolon_or_brace(trimmed);
    char* source = NULL;
    if (is_prefixed_by(trimmed, "extern") || ended) {
        source = duplicate_string_safe(trimmed);
    } else {
        char* temp = malloc_or_null(strlen(trimmed) + 2);
        if (!temp) { free(trimmed); fprintf(stderr, "Error: Out of memory\n"); return 0; }
        strcpy(temp, trimmed);
        strcat(temp, ";");
        if (!stmt) {
            free(temp);
            char* accum = duplicate_string_safe(trimmed);
            if (!accum) { free(trimmed); return 0; }
            for (;;) {
                print_prompt("... ");
                int r = safe_getline(linebuf, linecap, stdin);
                if (r == -1) break;
                if ((*linebuf)[r - 1] == '\n') (*linebuf)[r - 1] = '\0';
                char* piece = trim_string_alloc(*linebuf);
                if (!piece) break;
                char* joined = join_lines_safe(accum, piece);
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
            source = temp;
        }
    }
    if (!source) { free(trimmed); return 0; }
    int res = execute_source_string(source, env);
    free(source);
    free(trimmed);
    return res;
}

int main(int argc, char** argv) {
    signal(SIGINT, handle_sigint);
    Env* env = global_env();
    if (!env) {
        fprintf(stderr, "Failed to initialize global environment\n");
        return 1;
    }
    register_builtins(env);
    if (argc >= 2) {
        if (argc == 2) {
            if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
                const char* vs = version_string ? version_string() : "0.0.0";
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
    char* linebuf = NULL;
    size_t linecap = 0;
    print_banner();
    History* hist = history_new(1024);
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
        if (r != 0) {
            if (r != 0) {
            }
        }
    }
    history_free(hist);
    free(linebuf);
    return 0;
}