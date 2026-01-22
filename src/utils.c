#include "utils.h"
#include "value.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

char* dh_strdup(const char* s){
    if(!s) return NULL;
    size_t n = strlen(s) + 1;
    char* r = malloc(n);
    if(!r) return NULL;
    memcpy(r, s, n);
    return r;
}

char* dh_strndup(const char* s, size_t n){
    char* r = malloc(n + 1);
    if(!r) return NULL;
    memcpy(r, s, n);
    r[n] = 0;
    return r;
}

char* dh_concat(const char* a, const char* b){
    size_t la = a ? strlen(a) : 0;
    size_t lb = b ? strlen(b) : 0;
    char* r = malloc(la + lb + 1);
    if(!r) return NULL;
    if(la) memcpy(r, a, la);
    if(lb) memcpy(r + la, b, lb);
    r[la + lb] = 0;
    return r;
}

char* dh_from_double(double v){
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%.12g", v);
    if(n < 0) n = 0;
    return dh_strndup(buf, (size_t)n);
}

char* dh_from_int(long long v){
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%lld", v);
    if(n < 0) n = 0;
    return dh_strndup(buf, (size_t)n);
}

char* dh_from_int_hex(long long v){
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%llx", (unsigned long long)v);
    if(n < 0) n = 0;
    return dh_strndup(buf, (size_t)n);
}

char* dh_from_int_oct(long long v){
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%llo", (unsigned long long)v);
    if(n < 0) n = 0;
    return dh_strndup(buf, (size_t)n);
}

char* dh_now_iso(){
    time_t t = time(NULL);
    struct tm tmv;
#if defined(_MSC_VER)
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    char buf[64];
    if(strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv) == 0) return dh_strdup("");
    return dh_strdup(buf);
}

char* read_file_to_string(const char* path){
    if(!path) return NULL;
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
    if(!path) return 0;
    FILE* f = fopen(path, "wb");
    if(!f) return 0;
    size_t w = fwrite(content ? content : "", 1, content ? strlen(content) : 0, f);
    fclose(f);
    return (w == (size_t)(content ? strlen(content) : 0));
}

void map_grow(Value* map){
    if(!map) return;
    if(map->type != V_MAP) return;
    size_t cap = map->v.map.cap;
    if(cap == 0){
        size_t n = 8;
        map->v.map.keys = calloc(n, sizeof(char*));
        map->v.map.vals = calloc(n, sizeof(Value*));
        if(!map->v.map.keys || !map->v.map.vals){
            free(map->v.map.keys);
            free(map->v.map.vals);
            map->v.map.keys = NULL;
            map->v.map.vals = NULL;
            map->v.map.cap = 0;
            return;
        }
        map->v.map.cap = n;
    } else {
        size_t n = cap * 2;
        char** nk = realloc(map->v.map.keys, sizeof(char*) * n);
        Value** nv = realloc(map->v.map.vals, sizeof(Value*) * n);
        if(!nk || !nv){
            if(nk) { map->v.map.keys = nk; }
            if(nv) { map->v.map.vals = nv; }
            return;
        }
        for(size_t i = cap; i < n; ++i) nk[i] = NULL;
        for(size_t i = cap; i < n; ++i) nv[i] = NULL;
        map->v.map.keys = nk;
        map->v.map.vals = nv;
        map->v.map.cap = n;
    }
}