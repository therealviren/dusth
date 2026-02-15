#include "env.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>

struct Env {
    Env* parent;
    size_t count;
    size_t capacity;
    char** keys;
    Value* values;
};

static int env_ensure_capacity(Env* e) {
    if (!e) return 0;
    if (e->count + 1 <= e->capacity) return 1;
    size_t newcap = (e->capacity == 0) ? 8 : e->capacity * 2;
    char** nk = calloc(newcap, sizeof(char*));
    if (!nk) return 0;
    Value* nv = calloc(newcap, sizeof(Value));
    if (!nv) {
        free(nk);
        return 0;
    }
    if (e->keys) memcpy(nk, e->keys, sizeof(char*) * e->count);
    if (e->values) memcpy(nv, e->values, sizeof(Value) * e->count);
    free(e->keys);
    free(e->values);
    e->keys = nk;
    e->values = nv;
    e->capacity = newcap;
    return 1;
}

Env* env_new(Env* parent) {
    Env* e = malloc(sizeof(Env));
    if (!e) return NULL;
    e->parent = parent;
    e->count = 0;
    e->capacity = 8;
    e->keys = calloc(e->capacity, sizeof(char*));
    e->values = calloc(e->capacity, sizeof(Value));
    if (!e->keys || !e->values) {
        free(e->keys);
        free(e->values);
        free(e);
        return NULL;
    }
    return e;
}

void env_free(Env* e) {
    if (!e) return;
    for (size_t i = 0; i < e->count; ++i) {
        if (e->keys[i]) free(e->keys[i]);
        value_free(&e->values[i]);
    }
    free(e->keys);
    free(e->values);
    free(e);
}

int env_set_local(Env* e, const char* name, Value v) {
    if (!e || !name) return 0;
    if (!env_ensure_capacity(e)) return 0;
    char* dup = dh_strdup(name);
    if (!dup) return 0;
    Value cloned = value_clone(&v);
    e->keys[e->count] = dup;
    e->values[e->count] = cloned;
    e->count++;
    return 1;
}

int env_set(Env* e, const char* name, Value v) {
    if (!e || !name) return 0;
    for (Env* cur = e; cur; cur = cur->parent) {
        for (size_t i = 0; i < cur->count; ++i) {
            if (!cur->keys[i]) continue;
            if (strcmp(cur->keys[i], name) == 0) {
                value_free(&cur->values[i]);
                cur->values[i] = value_clone(&v);
                return 1;
            }
        }
    }
    return env_set_local(e, name, v);
}

int env_get(Env* e, const char* name, Value* out) {
    if (!e || !name || !out) return 0;
    for (Env* cur = e; cur; cur = cur->parent) {
        for (size_t i = 0; i < cur->count; ++i) {
            if (!cur->keys[i]) continue;
            if (strcmp(cur->keys[i], name) == 0) {
                *out = value_clone(&cur->values[i]);
                return 1;
            }
        }
    }
    return 0;
}

Env* env_clone_recursive(Env* e) {
    if (!e) return NULL;
    Env* parent_copy = env_clone_recursive(e->parent);
    Env* copy = env_new(parent_copy);
    if (!copy) {
        env_free(parent_copy);
        return NULL;
    }
    for (size_t i = 0; i < e->count; ++i) {
        if (!e->keys[i]) continue;
        if (!env_set_local(copy, e->keys[i], e->values[i])) {
            env_free(copy);
            env_free(parent_copy);
            return NULL;
        }
    }
    return copy;
}