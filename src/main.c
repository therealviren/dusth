#include "interpreter.h"
#include "parser.h"
#include "builtins.h"
#include "utils.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <limits.h>

static volatile sig_atomic_t g_interrupted = 0;

static void sigint_handler(int sig){
    (void)sig;
    g_interrupted = 1;
}

static char* safe_strdup(const char* s){
    if(!s) return NULL;
    size_t n = strlen(s);
    char* p = malloc(n + 1);
    if(!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

static char* str_strip(const char* s){
    if(!s) return NULL;
    const char* a = s;
    while(*a && isspace((unsigned char)*a)) a++;
    const char* b = s + strlen(s);
    while(b > a && isspace((unsigned char)*(b-1))) b--;
    size_t len = (size_t)(b - a);
    char* out = malloc(len + 1);
    if(!out) return NULL;
    memcpy(out, a, len);
    out[len] = '\0';
    return out;
}

static int is_statement_start(const char* s){
    if(!s) return 0;
    const char* p = s;
    while(*p && isspace((unsigned char)*p)) p++;
    if(*p == '\0') return 0;
    const char* keywords[] = {
        "let","func","extern","loop","if","return","while","for","break","continue","struct"
    };
    for(size_t i = 0; i < sizeof(keywords)/sizeof(keywords[0]); ++i){
        size_t L = strlen(keywords[i]);
        if(strncmp(p, keywords[i], L) == 0){
            char c = p[L];
            if(c == '\0' || isspace((unsigned char)c) || c == '(' || c == '{') return 1;
        }
    }
    if(strchr(p, '=') != NULL) return 1;
    return 0;
}

static int ends_with_semicolon_or_block(const char* s){
    if(!s) return 0;
    size_t len = strlen(s);
    if(len == 0) return 0;
    size_t i = len;
    while(i > 0 && isspace((unsigned char)s[i-1])) i--;
    if(i == 0) return 0;
    char last = s[i-1];
    if(last == ';') return 1;
    if(last == '}') return 1;
    return 0;
}

static void print_banner_and_help(void){
    const char* ver = version_string();
    const char* build = version_build();
    printf("Dusth %s (%s)\n", ver ? ver : "unknown", build ? build : "unknown");
    printf("Type \"help\", \"credits\", \"license\" or \"exit\".\n");
}

static void print_help(void){
    printf("Dusth commands:\n");
    printf("  help            Show this help text\n");
    printf("  credits         Show credits\n");
    printf("  license         Show license and disclaimer\n");
    printf("  exit / quit     Quit REPL\n");
    printf("  -v / --version  Show version\n");
}

static void print_credits(void){
    printf("Dusth %s - created by Viren Sahti\n", version_string());
}

static void print_license(void){
    printf("\n");
    printf("================================================================================\n");
    printf("                        VIREN SAHTI PROPRIETARY LICENSE                         \n");
    printf("                       Copyright \u00A9 2026 Viren Sahti.                       \n");
    printf("                              ALL RIGHTS RESERVED.                              \n");
    printf("================================================================================\n\n");
    printf("THIS SOFTWARE IS PROVIDED \"AS IS\" WITHOUT WARRANTY. USE AT YOUR OWN RISK.\n\n");
}

static int run_file_mode(const char* path, Env* env){
    if(!path) return 1;
    if(strcmp(path, "credits") == 0){ print_credits(); return 0; }
    if(strcmp(path, "license") == 0){ print_license(); return 0; }
    char* src = read_file_to_string(path);
    if(!src){
        fprintf(stderr, "Error: Cannot open file %s\n", path);
        return 2;
    }
    Node* program = parse_program(src);
    free(src);
    if(!program){
        fprintf(stderr, "Error: Parse error in file %s\n", path);
        return 3;
    }
    int exec_result = execute_program(program, env);
    free_node(program);
    if(exec_result != 0){
        fprintf(stderr, "Error: Runtime error in file %s\n", path);
        return 4;
    }
    return 0;
}

static char* join_with_newline(const char* a, const char* b){
    if(!a && !b) return NULL;
    if(!a) return safe_strdup(b);
    if(!b) return safe_strdup(a);
    size_t la = strlen(a);
    size_t lb = strlen(b);
    char* out = malloc(la + 1 + lb + 1);
    if(!out) return NULL;
    memcpy(out, a, la);
    out[la] = '\n';
    memcpy(out + la + 1, b, lb);
    out[la + 1 + lb] = '\0';
    return out;
}

int main(int argc, char** argv){
    signal(SIGINT, sigint_handler);
    Env* env = global_env();
    register_builtins(env);
    if(argc >= 2){
        if(argc == 2){
            if(strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0){
                printf("%s\n", version_string());
                return 0;
            }
            if(strcmp(argv[1], "credits") == 0){
                print_credits();
                return 0;
            }
            if(strcmp(argv[1], "license") == 0){
                print_license();
                return 0;
            }
            int r = run_file_mode(argv[1], env);
            return r;
        } else {
            fprintf(stderr, "Usage: dusth [script.dth]\n");
            return 1;
        }
    }
    size_t linecap = 0;
    char* line = NULL;
    print_banner_and_help();
    for(;;){
        if(g_interrupted){
            printf("\nInterrupted\n");
            g_interrupted = 0;
        }
        printf(">>> ");
        fflush(stdout);
        ssize_t n = getline(&line, &linecap, stdin);
        if(n == -1){
            if(feof(stdin)) break;
            clearerr(stdin);
            continue;
        }
        if(n > 0 && line[n-1] == '\n') line[n-1] = '\0';
        char* trimmed = str_strip(line);
        if(!trimmed) continue;
        if(strcmp(trimmed, "exit") == 0 || strcmp(trimmed, "quit") == 0){
            free(trimmed);
            break;
        }
        if(strcmp(trimmed, "help") == 0){
            print_help();
            free(trimmed);
            continue;
        }
        if(strcmp(trimmed, "credits") == 0){
            print_credits();
            free(trimmed);
            continue;
        }
        if(strcmp(trimmed, "license") == 0){
            print_license();
            free(trimmed);
            continue;
        }
        if(strcmp(trimmed, "-v") == 0 || strcmp(trimmed, "--version") == 0){
            printf("%s\n", version_string());
            free(trimmed);
            continue;
        }
        int stmt = is_statement_start(trimmed);
        int ended = ends_with_semicolon_or_block(trimmed);
        char* src = NULL;
        if(strncmp(trimmed, "extern", 6) == 0){
            src = safe_strdup(trimmed);
        } else if(ended){
            src = safe_strdup(trimmed);
        } else {
            src = malloc(strlen(trimmed) + 2);
            if(!src){
                free(trimmed);
                fprintf(stderr, "Error: out of memory\n");
                continue;
            }
            strcpy(src, trimmed);
            strcat(src, ";");
            if(stmt == 0){
                free(src);
                char* accum = safe_strdup(trimmed);
                for(;;){
                    printf("... ");
                    fflush(stdout);
                    ssize_t m = getline(&line, &linecap, stdin);
                    if(m == -1) break;
                    if(m > 0 && line[m-1] == '\n') line[m-1] = '\0';
                    char* piece = str_strip(line);
                    if(!piece) break;
                    char* joined = join_with_newline(accum, piece);
                    free(accum);
                    free(piece);
                    accum = joined;
                    if(ends_with_semicolon_or_block(accum)) break;
                }
                if(accum){
                    free(trimmed);
                    trimmed = accum;
                    src = safe_strdup(trimmed);
                } else {
                    free(trimmed);
                    continue;
                }
            }
        }
        Node* program = parse_program(src);
        free(src);
        if(!program){
            fprintf(stderr, "Error: parse failed\n");
            free(trimmed);
            continue;
        }
        int ok = execute_program(program, env);
        free_node(program);
        free(trimmed);
        if(ok != 0){
            fprintf(stderr, "Runtime error\n");
        }
    }
    free(line);
    return 0;
}