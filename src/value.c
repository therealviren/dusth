#include "value.h"
#include "utils.h"
#include "env.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void list_grow(Value* list){
    if(!list) return;
    if(list->v.list.len + 1 > list->v.list.cap){
        size_t n = (list->v.list.cap == 0 ? 8 : list->v.list.cap * 2);
        Value** tmp = realloc(list->v.list.items, sizeof(Value*) * n);
        if(tmp) list->v.list.items = tmp;
        list->v.list.cap = n;
    }
}

Value value_null(void){
    Value v;
    v.type = V_NULL;
    return v;
}

Value value_bool(int b){
    Value v;
    v.type = V_BOOL;
    v.v.b = b;
    return v;
}

Value value_int(long long i){
    Value v;
    v.type = V_INT;
    v.v.i = i;
    return v;
}

Value value_float(double f){
    Value v;
    v.type = V_FLOAT;
    v.v.f = f;
    return v;
}

Value value_string(const char* s){
    Value v;
    v.type = V_STRING;
    v.v.s = dh_strdup(s ? s : "");
    return v;
}

Value value_list(void){
    Value v;
    v.type = V_LIST;
    v.v.list.cap = 8;
    v.v.list.len = 0;
    v.v.list.items = calloc(v.v.list.cap, sizeof(Value*));
    return v;
}

Value value_list_from_array(Value** items, size_t n){
    Value v = value_list();
    if(n > 0){
        if(n > v.v.list.cap){
            Value** tmp = realloc(v.v.list.items, sizeof(Value*) * n);
            if(tmp) v.v.list.items = tmp;
            v.v.list.cap = n;
        }
        for(size_t i = 0; i < n; ++i){
            v.v.list.items[i] = malloc(sizeof(Value));
            if(v.v.list.items[i]) *v.v.list.items[i] = value_clone(items[i]);
        }
        v.v.list.len = n;
    }
    return v;
}

Value value_native(NativeFn fn, const char* name){
    Value v;
    v.type = V_NATIVE;
    v.v.native.fn = fn;
    v.v.native.name = dh_strdup(name ? name : "native");
    return v;
}

Value value_map(void){
    Value v;
    v.type = V_MAP;
    v.v.map.keys = NULL;
    v.v.map.vals = NULL;
    v.v.map.len = 0;
    v.v.map.cap = 0;
    return v;
}

Value value_clone(const Value* v){
    Value r;
    r.type = v->type;
    switch(v->type){
        case V_NULL:
            break;
        case V_BOOL:
            r.v.b = v->v.b;
            break;
        case V_INT:
            r.v.i = v->v.i;
            break;
        case V_FLOAT:
            r.v.f = v->v.f;
            break;
        case V_STRING:
            r.v.s = dh_strdup(v->v.s ? v->v.s : "");
            break;
        case V_LIST:
            r.v.list.cap = v->v.list.cap;
            r.v.list.len = v->v.list.len;
            r.v.list.items = calloc(r.v.list.cap ? r.v.list.cap : 1, sizeof(Value*));
            for(size_t i = 0; i < r.v.list.len; ++i){
                r.v.list.items[i] = malloc(sizeof(Value));
                if(r.v.list.items[i]) *r.v.list.items[i] = value_clone(v->v.list.items[i]);
            }
            break;
        case V_MAP:
            r.v.map.cap = v->v.map.cap;
            r.v.map.len = v->v.map.len;
            r.v.map.keys = calloc(r.v.map.cap ? r.v.map.cap : 1, sizeof(char*));
            r.v.map.vals = calloc(r.v.map.cap ? r.v.map.cap : 1, sizeof(Value*));
            for(size_t i = 0; i < r.v.map.len; ++i){
                r.v.map.keys[i] = dh_strdup(v->v.map.keys[i] ? v->v.map.keys[i] : "");
                r.v.map.vals[i] = malloc(sizeof(Value));
                if(r.v.map.vals[i]) *r.v.map.vals[i] = value_clone(v->v.map.vals[i]);
            }
            break;
        case V_FUNC:
            r.v.func.paramc = v->v.func.paramc;
            if(r.v.func.paramc){
                r.v.func.params = malloc(sizeof(char*) * r.v.func.paramc);
                for(size_t i = 0; i < r.v.func.paramc; ++i) r.v.func.params[i] = dh_strdup(v->v.func.params[i] ? v->v.func.params[i] : "");
            } else {
                r.v.func.params = NULL;
            }
            r.v.func.body = v->v.func.body;
            r.v.func.closure = v->v.func.closure ? env_clone_recursive(v->v.func.closure) : NULL;
            break;
        case V_NATIVE:
            r.v.native.fn = v->v.native.fn;
            r.v.native.name = dh_strdup(v->v.native.name ? v->v.native.name : "");
            break;
        default:
            r.type = V_NULL;
            break;
    }
    return r;
}

void value_free(Value* v){
    if(!v) return;
    switch(v->type){
        case V_STRING:
            if(v->v.s) free(v->v.s);
            break;
        case V_LIST:
            if(v->v.list.items){
                for(size_t i = 0; i < v->v.list.len; ++i){
                    if(v->v.list.items[i]){
                        value_free(v->v.list.items[i]);
                        free(v->v.list.items[i]);
                    }
                }
                free(v->v.list.items);
            }
            break;
        case V_MAP:
            if(v->v.map.keys){
                for(size_t i = 0; i < v->v.map.len; ++i){
                    if(v->v.map.keys[i]) free(v->v.map.keys[i]);
                    if(v->v.map.vals[i]){
                        value_free(v->v.map.vals[i]);
                        free(v->v.map.vals[i]);
                    }
                }
                free(v->v.map.keys);
                free(v->v.map.vals);
            }
            break;
        case V_FUNC:
            if(v->v.func.params){
                for(size_t i = 0; i < v->v.func.paramc; ++i) if(v->v.func.params[i]) free(v->v.func.params[i]);
                free(v->v.func.params);
            }
            if(v->v.func.closure) env_free(v->v.func.closure);
            break;
        case V_NATIVE:
            if(v->v.native.name) free(v->v.native.name);
            break;
        default:
            break;
    }
    v->type = V_NULL;
}

char* value_to_string(const Value* v){
    switch(v->type){
        case V_NULL: return dh_strdup("null");
        case V_BOOL: return dh_strdup(v->v.b ? "true" : "false");
        case V_INT: return dh_from_int(v->v.i);
        case V_FLOAT: return dh_from_double(v->v.f);
        case V_STRING: return dh_strdup(v->v.s ? v->v.s : "");
        case V_LIST: {
            size_t total = 2;
            for(size_t i = 0; i < v->v.list.len; ++i){
                char* s = value_to_string(v->v.list.items[i]);
                total += strlen(s) + 2;
                free(s);
            }
            char* out = malloc(total + 1);
            size_t p = 0;
            out[p++] = '[';
            for(size_t i = 0; i < v->v.list.len; ++i){
                char* s = value_to_string(v->v.list.items[i]);
                size_t L = strlen(s);
                memcpy(out + p, s, L);
                p += L;
                if(i + 1 < v->v.list.len){
                    out[p++] = ',';
                    out[p++] = ' ';
                }
                free(s);
            }
            out[p++] = ']';
            out[p] = '\0';
            return out;
        }
        case V_MAP: return dh_strdup("{map}");
        case V_FUNC: return dh_strdup("<function>");
        case V_NATIVE: return dh_strdup("<native>");
        default: return dh_strdup("<unknown>");
    }
}