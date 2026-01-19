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

static void print_license(void) {
    printf("\n");
    printf("================================================================================\n");
    printf("                        VIREN SAHTI PROPRIETARY LICENSE                         \n");
    printf("                       Copyright \u00A9 2026 Viren Sahti.                       \n");
    printf("                              ALL RIGHTS RESERVED.                              \n");
    printf("================================================================================\n\n");

    printf("1. CRITICAL WARNING: NO WARRANTY & LIMITATION OF LIABILITY\n");
    printf("----------------------------------------------------------\n");
    printf("THIS SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND. BY EXECUTING\n");
    printf("THIS PROGRAM, YOU EXPLICITLY AGREE TO THE FOLLOWING TERMS:\n\n");

    printf("A) TOTAL LIABILITY EXCLUSION:\n");
    printf("   VIREN SAHTI IS NOT RESPONSIBLE FOR ANY DAMAGE TO YOUR HARDWARE, SOFTWARE,\n");
    printf("   OR DATA. IF THIS SOFTWARE CAUSES YOUR COMPUTER TO CRASH, OVERHEAT, MALFUNCTION,\n");
    printf("   OR IF IT RESULTS IN TOTAL SYSTEM FAILURE (\"BRICKING\"), THIS IS SOLELY YOUR\n");
    printf("   RESPONSIBILITY. YOU ASSUME ALL COSTS FOR REPAIR AND SERVICING.\n\n");

    printf("B) NO REMEDY:\n");
    printf("   IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT, INDIRECT,\n");
    printf("   INCIDENTAL, OR CONSEQUENTIAL DAMAGES (INCLUDING LOSS OF PROFITS, DATA LOSS,\n");
    printf("   OR BUSINESS INTERRUPTION) HOWEVER CAUSED.\n\n");

    printf("2. STRICT ANTI-THEFT & COPYRIGHT PROTECTION\n");
    printf("-------------------------------------------\n");
    printf("This software is PROPRIETARY and CONFIDENTIAL. It is NOT open-source.\n\n");
    
    printf("- THEFT: Claiming this code as your own is strictly prohibited.\n");
    printf("- MODIFICATION: You may NOT decompile, reverse engineer, disassemble, or modify\n");
    printf("  any part of this software.\n");
    printf("- REDISTRIBUTION: You may NOT share, sell, or distribute this software without\n");
    printf("  prior written consent from Viren Sahti.\n\n");

    printf("ANY UNAUTHORIZED USE OR COPYING IS A VIOLATION OF COPYRIGHT LAW.\n");
    printf("USE ENTIRELY AT YOUR OWN RISK.\n");
    printf("================================================================================\n\n");
}

static void run_file_mode(const char* path, Env* env){
    if(!path) return;
    if(strcmp(path, "credits") == 0){ print_credits(); return; }
    if(strcmp(path, "license") == 0){ print_license(); return; }
    char* src = read_file_to_string(path);
    if(!src){ fprintf(stderr, "Error: Cannot open file %s\n", path); exit(1); }
    Node* program = parse_program(src);
    free(src);
    if(!program){ fprintf(stderr, "Error: Parse error in file %s\n", path); exit(1); }
    int exec_result = execute_program(program, env);
    free_node(program);
    if(exec_result != 0){ fprintf(stderr, "Error: Runtime error in file %s\n", path); exit(1); }
}

int main(int argc, char** argv){
    signal(SIGINT, sigint_handler);
    Env* env = global_env();
    register_builtins(env);
    if(argc >= 2){
        if(argc == 2){
            if(strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0){ printf("%s\n", version_string()); return 0; }
            if(strcmp(argv[1], "credits") == 0){ print_credits(); return 0; }
            if(strcmp(argv[1], "license") == 0){ print_license(); return 0; }
            interpret_file(argv[1], env);
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
        if(g_interrupted){ printf("\nInterrupted\n"); free(linebuf); exit(1); }
        printf(">>> ");
        fflush(stdout);
        if(!fgets(linebuf, (int)bufcap, stdin)) break;
        char* trimmed = str_strip(linebuf);
        if(!trimmed) continue;
        if(strcmp(trimmed, "exit") == 0 || strcmp(trimmed, "quit") == 0){ free(trimmed); break; }
        if(strcmp(trimmed, "help") == 0){ print_help(); free(trimmed); continue; }
        if(strcmp(trimmed, "credits") == 0){ print_credits(); free(trimmed); continue; }
        if(strcmp(trimmed, "license") == 0){ print_license(); free(trimmed); continue; }
        if(strcmp(trimmed, "-v") == 0 || strcmp(trimmed, "--version") == 0){ printf("%s\n", version_string()); free(trimmed); continue; }
        int stmt = is_statement_start(trimmed);
        int ended = ends_with_semicolon_or_block(trimmed);
        char* src = NULL;
        if(strncmp(trimmed, "extern", 6) == 0) {
            src = safe_strdup(trimmed);
        } else if(ended) {
            src = safe_strdup(trimmed);
        } else {
            src = malloc(strlen(trimmed) + 2);
            if(!src){ free(trimmed); fprintf(stderr, "Error\n"); exit(1); }
            strcpy(src, trimmed);
            strcat(src, ";");
        }
        Node* program = parse_program(src);
        free(src);
        if(!program){ free(trimmed); fprintf(stderr, "Error\n"); exit(1); }
        int ok = execute_program(program, env);
        free_node(program);
        free(trimmed);
        if(ok != 0){ fprintf(stderr, "Error\n"); exit(1); }
    }
    free(linebuf);
    return 0;
}