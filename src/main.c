#include "interpreter.h"
#include "builtins.h"
#include "utils.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>

static volatile sig_atomic_t interrupted = 0;

static void handle_sigint(int sig){
    (void)sig;
    interrupted = 1;
}

static char* duplicate_string(const char* s){
    if(!s) return NULL;
    size_t n = strlen(s);
    char* p = malloc(n + 1);
    if(!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

static char* trim_string(const char* s){
    if(!s) return NULL;
    const char* start = s;
    while(*start && isspace((unsigned char)*start)) start++;
    const char* end = s + strlen(s);
    while(end > start && isspace((unsigned char)*(end-1))) end--;
    size_t len = (size_t)(end - start);
    char* out = malloc(len + 1);
    if(!out) return NULL;
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static int statement_starts(const char* s){
    if(!s) return 0;
    const char* p = s;
    while(*p && isspace((unsigned char)*p)) p++;
    if(*p == '\0') return 0;
    const char* keywords[] = {
        "let","func","extern","loop","if","return","while","for","break","continue","struct"
    };
    for(size_t i = 0; i < sizeof(keywords)/sizeof(keywords[0]); i++){
        size_t L = strlen(keywords[i]);
        if(strncmp(p, keywords[i], L) == 0){
            char c = p[L];
            if(c == '\0' || isspace((unsigned char)c) || c == '(' || c == '{') return 1;
        }
    }
    if(strchr(p, '=') != NULL) return 1;
    return 0;
}

static int ends_with_semicolon_or_brace(const char* s){
    if(!s) return 0;
    size_t len = strlen(s);
    if(len == 0) return 0;
    size_t i = len;
    while(i > 0 && isspace((unsigned char)s[i-1])) i--;
    if(i == 0) return 0;
    char last = s[i-1];
    return last == ';' || last == '}';
}

static void display_banner(){
    printf("Dusth %s (%s)\n", version_string(), version_build());
    printf("Type \"help\", \"credits\", \"license\", or \"exit\".\n");
}

static void display_help(){
    printf("Dusth commands:\n");
    printf("  help            Show this help\n");
    printf("  credits         Show credits\n");
    printf("  license         Show license\n");
    printf("  exit / quit     Quit REPL\n");
    printf("  -v / --version  Show version\n");
}

static void display_credits(){
    printf("Dusth %s - created by Viren Sahti\n", version_string());
}

static void display_license(){
    printf(
"/***********************************************************************************\n"
" * Viren Sahti Proprietary License\n"
" * Copyright (c) 2026 Viren Sahti. All Rights Reserved.\n"
" *\n"
" * 1. NO WARRANTY & LIMITATION OF LIABILITY (\"USE AT YOUR OWN RISK\")\n"
" * -----------------------------------------------------------------\n"
" * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER (VIREN SAHTI) \"AS IS\" AND ANY \n"
" * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED \n"
" * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND \n"
" * NON-INFRINGEMENT ARE DISCLAIMED.\n"
" *\n"
" * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT, INDIRECT, \n"
" * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES HOWEVER CAUSED.\n"
" *\n"
" * BY USING THIS SOFTWARE, YOU EXPRESSLY ACKNOWLEDGE AND AGREE THAT THE USE IS AT YOUR SOLE RISK.\n"
" *\n"
" * 2. NO THEFT, REDISTRIBUTION, OR MODIFICATION\n"
" * --------------------------------------------\n"
" * This software is proprietary and confidential. It is NOT open-source.\n"
" *\n"
" * 3. TERMINATION\n"
" * --------------\n"
" * Any use in violation of these terms will terminate your rights.\n"
" ***********************************************************************************/\n"
    );
}

static char* join_lines(const char* a, const char* b){
    if(!a && !b) return NULL;
    if(!a) return duplicate_string(b);
    if(!b) return duplicate_string(a);
    size_t la = strlen(a);
    size_t lb = strlen(b);
    char* out = malloc(la + lb + 2);
    if(!out) return NULL;
    memcpy(out, a, la);
    out[la] = '\n';
    memcpy(out + la + 1, b, lb);
    out[la + 1 + lb] = '\0';
    return out;
}

int main(int argc, char** argv){
    signal(SIGINT, handle_sigint);
    Env* env = global_env();
    register_builtins(env);
    if(argc >= 2){
        if(argc == 2){
            if(strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0){
                printf("%s\n", version_string());
                return 0;
            }
            if(strcmp(argv[1], "credits") == 0){ display_credits(); return 0; }
            if(strcmp(argv[1], "license") == 0){ display_license(); return 0; }
            return execute_file(argv[1], env);
        } else {
            fprintf(stderr, "Usage: dusth [script.dth]\n");
            return 1;
        }
    }
    size_t linecap = 0;
    char* line = NULL;
    display_banner();
    for(;;){
        if(interrupted){
            printf("\nInterrupted\n");
            interrupted = 0;
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
        char* trimmed = trim_string(line);
        if(!trimmed) continue;
        if(strcmp(trimmed, "exit") == 0 || strcmp(trimmed, "quit") == 0){ free(trimmed); break; }
        if(strcmp(trimmed, "help") == 0){ display_help(); free(trimmed); continue; }
        if(strcmp(trimmed, "credits") == 0){ display_credits(); free(trimmed); continue; }
        if(strcmp(trimmed, "license") == 0){ display_license(); free(trimmed); continue; }
        if(strcmp(trimmed, "-v") == 0 || strcmp(trimmed, "--version") == 0){ printf("%s\n", version_string()); free(trimmed); continue; }
        int stmt = statement_starts(trimmed);
        int ended = ends_with_semicolon_or_brace(trimmed);
        char* source = NULL;
        if(strncmp(trimmed, "extern", 6) == 0 || ended){ source = duplicate_string(trimmed); }
        else{
            source = malloc(strlen(trimmed) + 2);
            if(!source){ free(trimmed); fprintf(stderr, "Error: Out of memory\n"); continue; }
            strcpy(source, trimmed);
            strcat(source, ";");
            if(!stmt){
                free(source);
                char* accum = duplicate_string(trimmed);
                for(;;){
                    printf("... ");
                    fflush(stdout);
                    ssize_t m = getline(&line, &linecap, stdin);
                    if(m == -1) break;
                    if(m > 0 && line[m-1] == '\n') line[m-1] = '\0';
                    char* piece = trim_string(line);
                    if(!piece) break;
                    char* joined = join_lines(accum, piece);
                    free(accum);
                    free(piece);
                    accum = joined;
                    if(ends_with_semicolon_or_brace(accum)) break;
                }
                if(accum){ free(trimmed); trimmed = accum; source = duplicate_string(trimmed); }
                else{ free(trimmed); continue; }
            }
        }
        Node* program = parse_program(source);
        free(source);
        if(!program){ fprintf(stderr, "Error: Parse failed\n"); free(trimmed); continue; }
        int ok = execute_program(program, env);
        free_node(program);
        free(trimmed);
        if(ok != 0) fprintf(stderr, "Runtime error\n");
    }
    free(line);
    return 0;
}