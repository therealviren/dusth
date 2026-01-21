#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

char* dh_strdup(const char* s){
    if(!s) return NULL;
    size_t n = strlen(s) + 1;
    char* r = malloc(n);
    memcpy(r, s, n);
    return r;
}

char* dh_strndup(const char* s, size_t n){
    char* r = malloc(n + 1);
    memcpy(r, s, n);
    r[n] = 0;
    return r;
}

char* dh_concat(const char* a, const char* b){
    size_t la = strlen(a);
    size_t lb = strlen(b);
    char* r = malloc(la + lb + 1);
    memcpy(r, a, la);
    memcpy(r + la, b, lb);
    r[la + lb] = 0;
    return r;
}

char* dh_from_double(double v){
    char buf[64];
    int n = snprintf(buf, 64, "%.12g", v);
    return dh_strndup(buf, n);
}

char* dh_from_int(long long v){
    char buf[64];
    int n = snprintf(buf, 64, "%lld", v);
    return dh_strndup(buf, n);
}

char* dh_now_iso(){
    time_t t = time(NULL);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    char buf[64];
    strftime(buf, 64, "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return dh_strdup(buf);
}

char* read_file_to_string(const char* path){
    FILE* f = fopen(path, "rb");
    if(!f) return NULL;
    if(fseek(f, 0, SEEK_END) != 0){
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if(sz < 0){
        fclose(f);
        return NULL;
    }
    if(fseek(f, 0, SEEK_SET) != 0){
        fclose(f);
        return NULL;
    }
    char* buf = malloc((size_t)sz + 1);
    if(!buf){
        fclose(f);
        return NULL;
    }
    size_t read = 0;
    if(sz > 0) read = fread(buf, 1, (size_t)sz, f);
    buf[read] = 0;
    fclose(f);
    return buf;
}

int write_string_to_file(const char* path, const char* content){
    FILE* f = fopen(path, "wb");
    if(!f) return 0;
    size_t w = fwrite(content, 1, strlen(content), f);
    fclose(f);
    return (w == strlen(content));
}