#ifndef DUSTH_VALUE_H
#define DUSTH_VALUE_H
#include <stddef.h>
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
typedef struct Value Value;
typedef struct Env Env;
typedef Value (*NativeFn)(Env* env, Value* args, size_t argc);
struct Value {
    ValueType type;
    union {
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
            char** params;
            size_t paramc;
            void* body;
            Env* closure;
        } func;
        struct {
            NativeFn fn;
            char* name;
        } native;
        int b;
    } v;
};
Env* env_new(Env* parent);
void env_free(Env* e);
void env_set(Env* e, const char* key, Value val);
int env_has(Env* e, const char* key);
int env_get(Env* e, const char* key, Value* out);
Value value_null();
Value value_bool(int b);
Value value_int(long long i);
Value value_float(double f);
Value value_string(const char* s);
Value value_list();
Value value_list_from_array(Value** items, size_t n);
Value value_native(NativeFn fn, const char* name);
Value value_clone(const Value* v);
void value_free(Value* v);
char* value_to_string(const Value* v);
#endif