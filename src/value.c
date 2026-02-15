#include "value.h"
#include "utils.h"
#include "env.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void list_grow(Value* list) {
    if (!list) return;
    if (list->v.list.len + 1 <= list->v.list.cap) return;
    size_t old = list->v.list.cap;
    size_t n = old == 0 ? 8 : old * 2;
    Value **new_items = calloc(n, sizeof(Value *));
    if (!new_items) return;
    for (size_t i = 0; i < old; ++i) new_items[i] = list->v.list.items ? list->v.list.items[i] : NULL;
    free(list->v.list.items);
    list->v.list.items = new_items;
    list->v.list.cap = n;
}

Value value_null(void) {
    Value v;
    v.type = V_NULL;
    return v;
}

Value value_bool(int b) {
    Value v;
    v.type = V_BOOL;
    v.v.b = b ? 1 : 0;
    return v;
}

Value value_int(long long i) {
    Value v;
    v.type = V_INT;
    v.v.i = i;
    return v;
}

Value value_float(double f) {
    Value v;
    v.type = V_FLOAT;
    v.v.f = f;
    return v;
}

Value value_string(const char* s) {
    Value v;
    v.type = V_STRING;
    v.v.s = dh_strdup(s ? s : "");
    return v;
}

Value value_list(void) {
    Value v;
    v.type = V_LIST;
    v.v.list.cap = 8;
    v.v.list.len = 0;
    v.v.list.items = calloc(v.v.list.cap, sizeof(Value*));
    if (!v.v.list.items) {
        v.v.list.cap = 0;
        v.v.list.items = NULL;
    }
    return v;
}

Value value_list_from_array(Value** items, size_t n) {
    Value v = value_list();
    if (n == 0) return v;
    size_t need = n;
    Value **new_items = calloc(need, sizeof(Value*));
    if (!new_items) {
        return v;
    }
    for (size_t i = 0; i < n; ++i) new_items[i] = NULL;
    for (size_t i = 0; i < n; ++i) {
        Value *elem = malloc(sizeof(Value));
        if (!elem) {
            for (size_t j = 0; j < i; ++j) {
                if (new_items[j]) {
                    value_free(new_items[j]);
                    free(new_items[j]);
                }
            }
            free(new_items);
            return v;
        }
        *elem = value_clone(items[i]);
        new_items[i] = elem;
    }
    free(v.v.list.items);
    v.v.list.items = new_items;
    v.v.list.cap = need;
    v.v.list.len = n;
    return v;
}

Value value_native(NativeFn fn, const char* name) {
    Value v;
    v.type = V_NATIVE;
    v.v.native.fn = fn;
    v.v.native.name = dh_strdup(name ? name : "native");
    return v;
}

Value value_map(void) {
    Value v;
    v.type = V_MAP;
    v.v.map.keys = NULL;
    v.v.map.vals = NULL;
    v.v.map.len = 0;
    v.v.map.cap = 0;
    return v;
}

static int clone_list_contents(Value* dest, const Value* src) {
    dest->v.list.cap = src->v.list.cap;
    dest->v.list.len = src->v.list.len;
    if (dest->v.list.cap == 0) {
        dest->v.list.items = NULL;
        return 1;
    }
    dest->v.list.items = calloc(dest->v.list.cap, sizeof(Value*));
    if (!dest->v.list.items) return 0;
    for (size_t i = 0; i < dest->v.list.len; ++i) dest->v.list.items[i] = NULL;
    for (size_t i = 0; i < dest->v.list.len; ++i) {
        Value *item = malloc(sizeof(Value));
        if (!item) {
            for (size_t j = 0; j < i; ++j) {
                if (dest->v.list.items[j]) {
                    value_free(dest->v.list.items[j]);
                    free(dest->v.list.items[j]);
                }
            }
            free(dest->v.list.items);
            dest->v.list.items = NULL;
            dest->v.list.cap = 0;
            dest->v.list.len = 0;
            return 0;
        }
        *item = value_clone(src->v.list.items[i]);
        dest->v.list.items[i] = item;
    }
    return 1;
}

static int clone_map_contents(Value* dest, const Value* src) {
    dest->v.map.cap = src->v.map.cap;
    dest->v.map.len = src->v.map.len;
    if (dest->v.map.cap == 0) {
        dest->v.map.keys = NULL;
        dest->v.map.vals = NULL;
        return 1;
    }
    dest->v.map.keys = calloc(dest->v.map.cap, sizeof(char*));
    dest->v.map.vals = calloc(dest->v.map.cap, sizeof(Value*));
    if (!dest->v.map.keys || !dest->v.map.vals) {
        free(dest->v.map.keys);
        free(dest->v.map.vals);
        dest->v.map.keys = NULL;
        dest->v.map.vals = NULL;
        dest->v.map.cap = 0;
        dest->v.map.len = 0;
        return 0;
    }
    for (size_t i = 0; i < dest->v.map.len; ++i) {
        dest->v.map.keys[i] = NULL;
        dest->v.map.vals[i] = NULL;
    }
    for (size_t i = 0; i < dest->v.map.len; ++i) {
        dest->v.map.keys[i] = dh_strdup(src->v.map.keys[i] ? src->v.map.keys[i] : "");
        if (!dest->v.map.keys[i]) {
            for (size_t j = 0; j < i; ++j) {
                if (dest->v.map.keys[j]) free(dest->v.map.keys[j]);
                if (dest->v.map.vals[j]) {
                    value_free(dest->v.map.vals[j]);
                    free(dest->v.map.vals[j]);
                }
            }
            free(dest->v.map.keys);
            free(dest->v.map.vals);
            dest->v.map.keys = NULL;
            dest->v.map.vals = NULL;
            dest->v.map.cap = 0;
            dest->v.map.len = 0;
            return 0;
        }
        Value *val = malloc(sizeof(Value));
        if (!val) {
            free(dest->v.map.keys[i]);
            for (size_t j = 0; j < i; ++j) {
                if (dest->v.map.keys[j]) free(dest->v.map.keys[j]);
                if (dest->v.map.vals[j]) {
                    value_free(dest->v.map.vals[j]);
                    free(dest->v.map.vals[j]);
                }
            }
            free(dest->v.map.keys);
            free(dest->v.map.vals);
            dest->v.map.keys = NULL;
            dest->v.map.vals = NULL;
            dest->v.map.cap = 0;
            dest->v.map.len = 0;
            return 0;
        }
        *val = value_clone(src->v.map.vals[i]);
        dest->v.map.vals[i] = val;
    }
    return 1;
}

Value value_clone(const Value* v) {
    Value r;
    if (!v) return value_null();
    r.type = v->type;
    switch (v->type) {
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
            if (!r.v.s) r.type = V_NULL;
            break;
        case V_LIST:
            if (!clone_list_contents(&r, v)) r.type = V_NULL;
            break;
        case V_MAP:
            if (!clone_map_contents(&r, v)) r.type = V_NULL;
            break;
        case V_FUNC:
            r.v.func.paramc = v->v.func.paramc;
            if (r.v.func.paramc) {
                r.v.func.params = malloc(sizeof(char*) * r.v.func.paramc);
                if (!r.v.func.params) {
                    r.type = V_NULL;
                    break;
                }
                for (size_t i = 0; i < r.v.func.paramc; ++i) {
                    r.v.func.params[i] = dh_strdup(v->v.func.params[i] ? v->v.func.params[i] : "");
                    if (!r.v.func.params[i]) {
                        for (size_t j = 0; j < i; ++j) if (r.v.func.params[j]) free(r.v.func.params[j]);
                        free(r.v.func.params);
                        r.v.func.params = NULL;
                        r.type = V_NULL;
                        break;
                    }
                }
                if (r.type == V_NULL) break;
            } else {
                r.v.func.params = NULL;
            }
            r.v.func.body = v->v.func.body;
            r.v.func.closure = v->v.func.closure ? env_clone_recursive(v->v.func.closure) : NULL;
            if (v->v.func.closure && !r.v.func.closure) {
                if (r.v.func.params) {
                    for (size_t i = 0; i < r.v.func.paramc; ++i) if (r.v.func.params[i]) free(r.v.func.params[i]);
                    free(r.v.func.params);
                    r.v.func.params = NULL;
                }
                r.type = V_NULL;
            }
            break;
        case V_NATIVE:
            r.v.native.fn = v->v.native.fn;
            r.v.native.name = dh_strdup(v->v.native.name ? v->v.native.name : "");
            if (!r.v.native.name) r.type = V_NULL;
            break;
        default:
            r.type = V_NULL;
            break;
    }
    return r;
}

void value_free(Value* v) {
    if (!v) return;
    switch (v->type) {
        case V_STRING:
            if (v->v.s) free(v->v.s);
            break;
        case V_LIST:
            if (v->v.list.items) {
                for (size_t i = 0; i < v->v.list.len; ++i) {
                    if (v->v.list.items[i]) {
                        value_free(v->v.list.items[i]);
                        free(v->v.list.items[i]);
                    }
                }
                free(v->v.list.items);
            }
            v->v.list.items = NULL;
            v->v.list.len = 0;
            v->v.list.cap = 0;
            break;
        case V_MAP:
            if (v->v.map.keys) {
                for (size_t i = 0; i < v->v.map.len; ++i) {
                    if (v->v.map.keys[i]) free(v->v.map.keys[i]);
                    if (v->v.map.vals[i]) {
                        value_free(v->v.map.vals[i]);
                        free(v->v.map.vals[i]);
                    }
                }
                free(v->v.map.keys);
                free(v->v.map.vals);
            }
            v->v.map.keys = NULL;
            v->v.map.vals = NULL;
            v->v.map.len = 0;
            v->v.map.cap = 0;
            break;
        case V_FUNC:
            if (v->v.func.params) {
                for (size_t i = 0; i < v->v.func.paramc; ++i) if (v->v.func.params[i]) free(v->v.func.params[i]);
                free(v->v.func.params);
            }
            v->v.func.params = NULL;
            v->v.func.paramc = 0;
            if (v->v.func.closure) {
                env_free(v->v.func.closure);
                v->v.func.closure = NULL;
            }
            v->v.func.body = NULL;
            break;
        case V_NATIVE:
            if (v->v.native.name) free(v->v.native.name);
            v->v.native.name = NULL;
            v->v.native.fn = NULL;
            break;
        default:
            break;
    }
    v->type = V_NULL;
}

char* value_to_string(const Value* v) {
    if (!v) return dh_strdup("<null>");
    switch (v->type) {
        case V_NULL:
            return dh_strdup("null");
        case V_BOOL:
            return dh_strdup(v->v.b ? "true" : "false");
        case V_INT:
            return dh_from_int(v->v.i);
        case V_FLOAT:
            return dh_from_double(v->v.f);
        case V_STRING:
            return dh_strdup(v->v.s ? v->v.s : "");
        case V_LIST: {
            size_t total = 2;
            char** parts = malloc(sizeof(char*) * (v->v.list.len ? v->v.list.len : 1));
            if (!parts) return dh_strdup("[error]");
            for (size_t i = 0; i < v->v.list.len; ++i) parts[i] = NULL;
            for (size_t i = 0; i < v->v.list.len; ++i) {
                parts[i] = value_to_string(v->v.list.items[i]);
                if (!parts[i]) parts[i] = dh_strdup("<err>");
                total += strlen(parts[i]);
                if (i + 1 < v->v.list.len) total += 2;
            }
            char* out = malloc(total + 1);
            if (!out) {
                for (size_t i = 0; i < v->v.list.len; ++i) if (parts[i]) free(parts[i]);
                free(parts);
                return dh_strdup("[error]");
            }
            size_t p = 0;
            out[p++] = '[';
            for (size_t i = 0; i < v->v.list.len; ++i) {
                size_t L = strlen(parts[i]);
                memcpy(out + p, parts[i], L);
                p += L;
                if (i + 1 < v->v.list.len) {
                    out[p++] = ',';
                    out[p++] = ' ';
                }
                free(parts[i]);
            }
            out[p++] = ']';
            out[p] = '\0';
            free(parts);
            return out;
        }
        case V_MAP:
            return dh_strdup("{map}");
        case V_FUNC:
            return dh_strdup("<function>");
        case V_NATIVE:
            return dh_strdup("<native>");
        default:
            return dh_strdup("<unknown>");
    }
}