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
    size_t len = b - a;
    char* out = malloc(len + 1);
    if(!out) return NULL;
    memcpy(out, a, len);
    out[len] = '\0';
    return out;
}

static int is_statement_start(const char* s){
    if(!s) return 0;
    while(*s && isspace((unsigned char)*s)) s++;
    if(*s == '\0') return 0;
    const char* keywords[] = {
        "let","func","extern","loop","if","return","while","for","break","continue","struct"
    };
    for(size_t i = 0; i < sizeof(keywords)/sizeof(keywords[0]); ++i){
        size_t L = strlen(keywords[i]);
        if(strncmp(s, keywords[i], L) == 0){
            char c = s[L];
            if(c == '\0' || isspace((unsigned char)c) || c == '(' || c == '{') return 1;
        }
    }
    if(strchr(s, '=') != NULL) return 1;
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
    printf("Dusth %s (%s)\n", ver ? ver : "unknown", build ? build : "");
    printf("Type \"help\", \"credits\", \"license\" or \"exit\".\n");
}

static void print_help(void){
    printf("Dusth commands:\n");
    printf("  help      Show this help text\n");
    printf("  credits   Show credits\n");
    printf("  license   Show license and disclaimer\n");
    printf("  exit/quit Quit REPL\n");
    printf("  -v/--version Show version\n");
}

static void print_credits(void){
    printf("Dusth %s: Founded by Viren Sahti\n", version_string());
}

static void print_license(void){
    printf("License & Disclaimer\n");
    printf("Copyright \u00A9 2026 Viren Sahti\n\n");
    printf("This software is provided \"as is\", without any warranty, express or implied, including but not limited to warranties of merchantability, fitness for a particular purpose, or non-infringement.\n\n");
    printf("By using this software, you agree that Viren is not responsible for any direct or indirect damages, loss of data, profits, or other consequences arising from:\n");
    printf("- Misuse of the software\n");
    printf("- Criminal activity, hacking, or any illegal use\n");
    printf("- Modifications or derivative works\n");
    printf("- Software bugs, failures, or security breaches\n\n");
    printf("This software may only be used for legitimate purposes. Redistribution, modification, or claiming the code as your own is strictly prohibited. All rights remain with Viren.\n\n");
    printf("Use at your own risk.\n");
}

static void run_file_mode(const char* path, Env* env){
    if(!path) return;

    if(strcmp(path, "credits") == 0){
        print_credits();
        return;
    }
    if(strcmp(path, "license") == 0){
        print_license();
        return;
    }

    char* src = read_file_to_string(path);
    if(!src){
        fprintf(stderr, "Error: Cannot open file %s\n", path);
        exit(1);
    }

    Node* program = parse_program(src);
    free(src);
    if(!program){
        fprintf(stderr, "Error: Parse error in file %s\n", path);
        exit(1);
    }

    int exec_result = execute_program(program, env);
    free_node(program);

    if(exec_result != 0){
        fprintf(stderr, "Error: Runtime error in file %s\n", path);
        exit(1);
    }
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
            run_file_mode(argv[1], env);
            return 0;
        } else {
            fprintf(stderr, "Usage: dusth [script.dth]\n");
            return 1;
        }
    }
    size_t bufcap = 8192;
    char* linebuf = malloc(bufcap);
    if(!linebuf) return 1;
    print_banner_and_help();
    for(;;){
        if(g_interrupted){
            printf("\nInterrupted\n");
            free(linebuf);
            exit(1);
        }
        printf(">>> ");
        fflush(stdout);
        if(!fgets(linebuf, (int)bufcap, stdin)) break;
        char* trimmed = str_strip(linebuf);
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
        if(stmt || ended){
            char* src = NULL;
            if(ended) src = safe_strdup(trimmed);
            else {
                src = malloc(strlen(trimmed) + 2);
                if(!src){ free(trimmed); fprintf(stderr, "Error (problem in C written)\n"); exit(1); }
                strcpy(src, trimmed);
                strcat(src, ";");
            }
            Node* program = parse_program(src);
            free(src);
            if(!program){ free(trimmed); fprintf(stderr, "Error (problem in C written)\n"); exit(1); }
            int ok = execute_program(program, env);
            free_node(program);
            free(trimmed);
            if(!ok){ fprintf(stderr, "Error (problem in C written)\n"); exit(1); }
            continue;
        } else {
            size_t wrap_len = 4 + strlen(trimmed) + 2;
            char* wrapped = malloc(wrap_len);
            if(!wrapped){ free(trimmed); fprintf(stderr, "Error (problem in C written)\n"); exit(1); }
            strcpy(wrapped, "say(");
            strcat(wrapped, trimmed);
            strcat(wrapped, ");");
            Node* program = parse_program(wrapped);
            free(wrapped);
            if(!program){ free(trimmed); fprintf(stderr, "Error (problem in C written)\n"); exit(1); }
            int ok = execute_program(program, env);
            free_node(program);
            free(trimmed);
            if(!ok){ fprintf(stderr, "Error (problem in C written)\n"); exit(1); }
            continue;
        }
    }
    free(linebuf);
    return 0;
}