#ifndef DUSTH_VALUE_H
#define DUSTH_VALUE_H

#include <stddef.h>

typedef struct Env Env;
typedef struct Node Node;

typedef struct Value Value;
typedef Value (*NativeFn)(Env* env, Value* args, size_t argc);

typedef enum {
    V_NULL,
    V_BOOL,
    V_INT,
    V_FLOAT,
    V_STRING,
    V_LIST,
    V_MAP,
    V_FUNC,
    V_NATIVE
} ValueType;

struct Value {
    ValueType type;
    union {
        int b;
        long long i;
        double f;
        char* s;
        struct {
            Value** items;
            size_t len;
            size_t cap;
        } list;
        struct {
            char** keys;
            Value** vals;
            size_t len;
            size_t cap;
        } map;
        struct {
            size_t paramc;
            char** params;
            Node* body;
            Env* closure;
        } func;
        struct {
            NativeFn fn;
            char* name;
        } native;
    } v;
};

Value value_null(void);
Value value_bool(int b);
Value value_int(long long i);
Value value_float(double f);
Value value_string(const char* s);
Value value_list(void);
Value value_map(void);
Value value_list_from_array(Value** items, size_t n);
Value value_native(NativeFn fn, const char* name);
Value value_clone(const Value* v);
void value_free(Value* v);
char* value_to_string(const Value* v);

#endif