#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

char* dh_strdup(const char* s){
    if (s == NULL) return NULL;
    size_t n = strlen(s);
    char* r = malloc(n + 1);
    if (r == NULL) return NULL;
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

char* dh_strndup(const char* s, size_t n){
    if (s == NULL) return NULL;
    size_t len = 0;
    while (len < n && s[len] != '\0') ++len;
    char* r = malloc(len + 1);
    if (r == NULL) return NULL;
    memcpy(r, s, len);
    r[len] = '\0';
    return r;
}

char* dh_concat(const char* a, const char* b){
    size_t la = a ? strlen(a) : 0;
    size_t lb = b ? strlen(b) : 0;
    char* r = malloc(la + lb + 1);
    if (r == NULL) return NULL;
    if (la) memcpy(r, a, la);
    if (lb) memcpy(r + la, b, lb);
    r[la + lb] = '\0';
    return r;
}

char* dh_from_double(double v){
    char stackbuf[64];
    int n = snprintf(stackbuf, sizeof(stackbuf), "%.12g", v);
    if (n < 0) return NULL;
    if ((size_t)n < sizeof(stackbuf)) return dh_strndup(stackbuf, (size_t)n);
    size_t need = (size_t)n;
    char* buf = malloc(need + 1);
    if (!buf) return NULL;
    int n2 = snprintf(buf, need + 1, "%.12g", v);
    if (n2 < 0) { free(buf); return NULL; }
    return buf;
}

char* dh_from_int(long long v){
    char stackbuf[64];
    int n = snprintf(stackbuf, sizeof(stackbuf), "%lld", v);
    if (n < 0) return NULL;
    if ((size_t)n < sizeof(stackbuf)) return dh_strndup(stackbuf, (size_t)n);
    size_t need = (size_t)n;
    char* buf = malloc(need + 1);
    if (!buf) return NULL;
    int n2 = snprintf(buf, need + 1, "%lld", v);
    if (n2 < 0) { free(buf); return NULL; }
    return buf;
}

char* dh_from_int_hex(long long v){
    char stackbuf[64];
    int n = snprintf(stackbuf, sizeof(stackbuf), "%llx", (unsigned long long)v);
    if (n < 0) return NULL;
    if ((size_t)n < sizeof(stackbuf)) return dh_strndup(stackbuf, (size_t)n);
    size_t need = (size_t)n;
    char* buf = malloc(need + 1);
    if (!buf) return NULL;
    int n2 = snprintf(buf, need + 1, "%llx", (unsigned long long)v);
    if (n2 < 0) { free(buf); return NULL; }
    return buf;
}

char* dh_from_int_oct(long long v){
    char stackbuf[64];
    int n = snprintf(stackbuf, sizeof(stackbuf), "%llo", (unsigned long long)v);
    if (n < 0) return NULL;
    if ((size_t)n < sizeof(stackbuf)) return dh_strndup(stackbuf, (size_t)n);
    size_t need = (size_t)n;
    char* buf = malloc(need + 1);
    if (!buf) return NULL;
    int n2 = snprintf(buf, need + 1, "%llo", (unsigned long long)v);
    if (n2 < 0) { free(buf); return NULL; }
    return buf;
}

char* dh_now_iso(void){
    time_t t = time(NULL);
    struct tm tmv;
#if defined(_MSC_VER)
    if (gmtime_s(&tmv, &t) != 0) return NULL;
#else
    if (gmtime_r(&t, &tmv) == NULL) return NULL;
#endif
    char buf[32];
    size_t r = strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    if (r == 0) return NULL;
    return dh_strdup(buf);
}

char* read_file_to_string(const char* path){
    if (path == NULL) return NULL;
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 4096;
    size_t len = 0;
    char* buf = malloc(cap);
    if (!buf) { fclose(f); return NULL; }
    for (;;) {
        size_t to_read = cap - len;
        if (to_read == 0) {
            size_t newcap = cap * 2;
            char* nb = realloc(buf, newcap);
            if (!nb) { free(buf); fclose(f); return NULL; }
            buf = nb;
            cap = newcap;
            to_read = cap - len;
        }
        size_t r = fread(buf + len, 1, to_read, f);
        if (r == 0) {
            if (feof(f)) break;
            if (ferror(f)) { free(buf); fclose(f); return NULL; }
        }
        len += r;
    }
    char* final = realloc(buf, len + 1);
    if (!final) {
        free(buf);
        fclose(f);
        return NULL;
    }
    final[len] = '\0';
    fclose(f);
    return final;
}

int write_string_to_file(const char* path, const char* content){
    if (path == NULL) return 0;
    const char* data = content ? content : "";
    size_t total = strlen(data);
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    size_t written = 0;
    while (written < total) {
        size_t w = fwrite(data + written, 1, total - written, f);
        if (w == 0) {
            if (ferror(f)) { fclose(f); return 0; }
            break;
        }
        written += w;
    }
    if (fclose(f) != 0) return 0;
    return written == total;
}

void map_grow(Value* map){
    if (!map) return;
    if (map->type != V_MAP) return;
    size_t cap = map->v.map.cap;
    if (cap == 0){
        size_t n = 8;
        char** nk = calloc(n, sizeof(char*));
        Value** nv = calloc(n, sizeof(Value*));
        if (!nk || !nv) {
            free(nk);
            free(nv);
            map->v.map.keys = NULL;
            map->v.map.vals = NULL;
            map->v.map.cap = 0;
            return;
        }
        for (size_t i = 0; i < n; ++i) { nk[i] = NULL; nv[i] = NULL; }
        map->v.map.keys = nk;
        map->v.map.vals = nv;
        map->v.map.cap = n;
        map->v.map.len = 0;
        return;
    }
    size_t n = cap * 2;
    char** nk = calloc(n, sizeof(char*));
    Value** nv = calloc(n, sizeof(Value*));
    if (!nk || !nv) {
        free(nk);
        free(nv);
        return;
    }
    for (size_t i = 0; i < cap; ++i) {
        nk[i] = map->v.map.keys ? map->v.map.keys[i] : NULL;
        nv[i] = map->v.map.vals ? map->v.map.vals[i] : NULL;
    }
    for (size_t i = cap; i < n; ++i) {
        nk[i] = NULL;
        nv[i] = NULL;
    }
    free(map->v.map.keys);
    free(map->v.map.vals);
    map->v.map.keys = nk;
    map->v.map.vals = nv;
    map->v.map.cap = n;
}