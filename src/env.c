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

static void env_ensure_capacity(Env* e){
    if(!e) return;
    if(e->count + 1 > e->capacity){
        size_t newcap = (e->capacity == 0 ? 8 : e->capacity * 2);
        char** nk = realloc(e->keys, sizeof(char*) * newcap);
        Value* nv = realloc(e->values, sizeof(Value) * newcap);
        if(!nk || !nv){
            return;
        }
        e->keys = nk;
        e->values = nv;
        e->capacity = newcap;
    }
}

Env* env_new(Env* parent){
    Env* e = malloc(sizeof(Env));
    if(!e) return NULL;
    e->parent = parent;
    e->count = 0;
    e->capacity = 8;
    e->keys = calloc(e->capacity, sizeof(char*));
    e->values = calloc(e->capacity, sizeof(Value));
    return e;
}

void env_free(Env* e){
    if(!e) return;
    for(size_t i = 0; i < e->count; ++i){
        if(e->keys[i]) free(e->keys[i]);
        value_free(&e->values[i]);
    }
    free(e->keys);
    free(e->values);
    free(e);
}

int env_set(Env* e, const char* name, Value v){
    if(!e || !name) return 0;
    for(Env* cur = e; cur; cur = cur->parent){
        for(size_t i = 0; i < cur->count; ++i){
            if(strcmp(cur->keys[i], name) == 0){
                value_free(&cur->values[i]);
                cur->values[i] = value_clone(&v);
                return 1;
            }
        }
    }
    env_ensure_capacity(e);
    if(e->count + 1 > e->capacity) return 0;
    e->keys[e->count] = dh_strdup(name);
    e->values[e->count] = value_clone(&v);
    e->count++;
    return 1;
}

int env_get(Env* e, const char* name, Value* out){
    if(!e || !name || !out) return 0;
    for(Env* cur = e; cur; cur = cur->parent){
        for(size_t i = 0; i < cur->count; ++i){
            if(strcmp(cur->keys[i], name) == 0){
                *out = value_clone(&cur->values[i]);
                return 1;
            }
        }
    }
    return 0;
}

Env* env_clone_recursive(Env* e){
    if(!e) return NULL;
    Env* parent_copy = env_clone_recursive(e->parent);
    Env* copy = env_new(parent_copy);
    if(!copy) return NULL;
    for(size_t i = 0; i < e->count; ++i){
        env_set(copy, e->keys[i], e->values[i]);
    }
    return copy;
}