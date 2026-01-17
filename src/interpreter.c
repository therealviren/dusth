#include "env.h"
#include "value.h"
#include "utils.h"
#include "builtins.h"
#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <math.h>

static Node* clone_node(Node* n){
    if(!n) return NULL;
    Node* c = malloc(sizeof(Node));
    if(!c) exit(1);
    c->type = n->type;
    c->childc = n->childc;
    c->capacity = n->capacity;
    c->num = n->num;
    c->text = n->text ? dh_strdup(n->text) : NULL;
    if(c->capacity){
        c->children = malloc(sizeof(Node*) * c->capacity);
        for(size_t i = 0; i < c->childc; ++i) c->children[i] = clone_node(n->children[i]);
        for(size_t i = c->childc; i < c->capacity; ++i) c->children[i] = NULL;
    } else {
        c->children = NULL;
    }
    return c;
}

Value value_func(char** params, size_t paramc, Node* body, Env* closure){
    Value v;
    v.type = V_FUNC;
    v.v.func.paramc = paramc;
    v.v.func.params = NULL;
    if(paramc){
        v.v.func.params = malloc(sizeof(char*) * paramc);
        for(size_t i = 0; i < paramc; ++i){
            v.v.func.params[i] = dh_strdup(params[i] ? params[i] : "");
        }
    } else {
        v.v.func.params = NULL;
    }
    v.v.func.body = body;
    if(closure) v.v.func.closure = env_clone_recursive(closure);
    else v.v.func.closure = NULL;
    return v;
}

static Env* g_env = NULL;
Env* global_env(){ if(!g_env) g_env = env_new(NULL); return g_env; }

static Value eval_node(Node* n, Env* env);
static Value eval_program(Node* n, Env* env);

static Value make_error_string(const char* msg){
    return value_string(msg ? msg : "");
}

static Value call_native(NativeFn fn, Env* env, Node** args, size_t argc){
    Value* argv = NULL;
    if(argc){
        argv = malloc(sizeof(Value) * argc);
        for(size_t i = 0; i < argc; ++i) argv[i] = eval_node(args[i], env);
    }
    Value out = fn(env, argv, argc);
    if(argv){
        for(size_t i = 0; i < argc; ++i) value_free(&argv[i]);
        free(argv);
    }
    return out;
}

static Value call_user_function(Value* fval, Env* env, Node** args, size_t argc){
    if(!fval || fval->type != V_FUNC) return value_null();
    size_t paramc = fval->v.func.paramc;
    char** params = fval->v.func.params;
    Node* body = (Node*)fval->v.func.body;
    Env* closure = fval->v.func.closure ? fval->v.func.closure : env;
    Env* local = env_new(closure);
    for(size_t i = 0; i < paramc; ++i){
        Value av = value_null();
        if(i < argc) av = eval_node(args[i], env);
        env_set(local, params[i], av);
        value_free(&av);
    }
    Value result = value_null();
    if(body){
        if(body->type == NODE_BLOCK){
            result = eval_program(body, local);
        } else {
            result = eval_node(body, local);
        }
    }
    Value out = value_clone(&result);
    value_free(&result);
    env_free(local);
    return out;
}

static Value eval_call(Node* cal, Env* env){
    if(!cal || !cal->text) return value_null();
    Value fnv;
    if(!env_get(env, cal->text, &fnv)){
        return make_error_string("undefined function");
    }
    if(fnv.type == V_NATIVE){
        Value out = call_native(fnv.v.native.fn, env, cal->children, cal->childc);
        value_free(&fnv);
        return out;
    }
    if(fnv.type == V_FUNC){
        Value out = call_user_function(&fnv, env, cal->children, cal->childc);
        value_free(&fnv);
        return out;
    }
    value_free(&fnv);
    return make_error_string("value not callable");
}

static Value eval_program(Node* n, Env* env){
    Value last = value_null();
    for(size_t i = 0; i < n->childc; ++i){
        if(i > 0) value_free(&last);
        Node* child = n->children[i];
        last = eval_node(child, env);
        if(child->type == NODE_RETURN){
            Value out = value_clone(&last);
            value_free(&last);
            return out;
        }
    }
    return last;
}

static Value eval_node(Node* n, Env* env){
    if(!n) return value_null();
    switch(n->type){
        case NODE_PROGRAM:
            return eval_program(n, env);
        case NODE_EXPR_STMT:
            return eval_node(n->children[0], env);
        case NODE_LET: {
            Value v = value_null();
            if(n->childc > 0) v = eval_node(n->children[0], env);
            env_set(env, n->text ? n->text : "", v);
            Value out = value_clone(&v);
            value_free(&v);
            return out;
        }
        case NODE_LITERAL:
            if(n->text) return value_string(n->text);
            else {
                double num = n->num;
                long long as_int = (long long)num;
                if((double)as_int == num) return value_int(as_int);
                return value_float(num);
            }
        case NODE_STRING:
            if(n->text) return value_string(n->text);
            return value_null();
        case NODE_RETURN: {
            if(n->childc > 0) return eval_node(n->children[0], env);
            return value_null();
        }
        case NODE_IDENT: {
            Value out;
            if(env_get(env, n->text, &out)) return out;
            return value_null();
        }
        case NODE_BINARY: {
            Value a = eval_node(n->children[0], env);
            Value b = eval_node(n->children[1], env);
            char* op = n->text;
            Value res = value_null();
            if(strcmp(op, "+") == 0){
                if(a.type == V_STRING || b.type == V_STRING){
                    char* sa = value_to_string(&a);
                    char* sb = value_to_string(&b);
                    char* c = dh_concat(sa, sb);
                    res = value_string(c);
                    free(sa); free(sb); free(c);
                } else if(a.type == V_INT && b.type == V_INT){
                    res = value_int(a.v.i + b.v.i);
                } else {
                    double av = (a.type==V_FLOAT? a.v.f : (double)(a.type==V_INT?a.v.i:0));
                    double bv = (b.type==V_FLOAT? b.v.f : (double)(b.type==V_INT?b.v.i:0));
                    res = value_float(av + bv);
                }
            } else if(strcmp(op, "-") == 0){
                if(a.type==V_INT && b.type==V_INT) res = value_int(a.v.i - b.v.i);
                else {
                    double av = (a.type==V_FLOAT? a.v.f : (double)(a.type==V_INT?a.v.i:0));
                    double bv = (b.type==V_FLOAT? b.v.f : (double)(b.type==V_INT?b.v.i:0));
                    res = value_float(av - bv);
                }
            } else if(strcmp(op, "*") == 0){
                if(a.type==V_INT && b.type==V_INT) res = value_int(a.v.i * b.v.i);
                else {
                    double av = (a.type==V_FLOAT? a.v.f : (double)(a.type==V_INT?a.v.i:0));
                    double bv = (b.type==V_FLOAT? b.v.f : (double)(b.type==V_INT?b.v.i:0));
                    res = value_float(av * bv);
                }
            } else if(strcmp(op, "/") == 0){
                double av = (a.type==V_FLOAT? a.v.f : (double)(a.type==V_INT?a.v.i:0));
                double bv = (b.type==V_FLOAT? b.v.f : (double)(b.type==V_INT?b.v.i:0));
                res = value_float(av / bv);
            } else if(strcmp(op, "%") == 0){
                if(a.type==V_INT && b.type==V_INT) res = value_int(a.v.i % b.v.i);
                else {
                    double av = (a.type==V_FLOAT? a.v.f : (double)(a.type==V_INT?a.v.i:0));
                    double bv = (b.type==V_FLOAT? b.v.f : (double)(b.type==V_INT?b.v.i:0));
                    res = value_float(fmod(av,bv));
                }
            } else if(strcmp(op, "==") == 0){
                int eq = 0;
                if(a.type==V_INT && b.type==V_INT) eq = (a.v.i == b.v.i);
                else {
                    double av = (a.type==V_FLOAT? a.v.f : (double)(a.type==V_INT?a.v.i:0));
                    double bv = (b.type==V_FLOAT? b.v.f : (double)(b.type==V_INT?b.v.i:0));
                    eq = (av == bv);
                }
                res = value_bool(eq);
            } else if(strcmp(op, "!=") == 0){
                int ne = 0;
                if(a.type==V_INT && b.type==V_INT) ne = (a.v.i != b.v.i);
                else {
                    double av = (a.type==V_FLOAT? a.v.f : (double)(a.type==V_INT?a.v.i:0));
                    double bv = (b.type==V_FLOAT? b.v.f : (double)(b.type==V_INT?b.v.i:0));
                    ne = (av != bv);
                }
                res = value_bool(ne);
            } else if(strcmp(op, "<") == 0){
                double av = (a.type==V_FLOAT? a.v.f : (double)(a.type==V_INT?a.v.i:0));
                double bv = (b.type==V_FLOAT? b.v.f : (double)(b.type==V_INT?b.v.i:0));
                res = value_bool(av < bv);
            } else if(strcmp(op, ">") == 0){
                double av = (a.type==V_FLOAT? a.v.f : (double)(a.type==V_INT?a.v.i:0));
                double bv = (b.type==V_FLOAT? b.v.f : (double)(b.type==V_INT?b.v.i:0));
                res = value_bool(av > bv);
            } else {
                res = value_null();
            }
            value_free(&a);
            value_free(&b);
            return res;
        }
        case NODE_FUNC: {
            size_t paramc = n->childc > 0 ? n->childc - 1 : 0;
            char** params = NULL;
            if(paramc){
                params = malloc(sizeof(char*) * paramc);
                for(size_t i = 0; i < paramc; ++i){
                    params[i] = dh_strdup(n->children[i]->text ? n->children[i]->text : "");
                }
            }
            Node* body = n->childc > 0 ? n->children[n->childc - 1] : NULL;
            Node* body_clone = body ? clone_node(body) : NULL;
            Value fval = value_func(params, paramc, body_clone, env);
            if(params){
                for(size_t i = 0; i < paramc; ++i) free(params[i]);
                free(params);
            }
            env_set(env, n->text ? n->text : "", fval);
            Value ret = value_clone(&fval);
            value_free(&fval);
            return ret;
        }
        case NODE_CALL:
            return eval_call(n, env);
        case NODE_BLOCK:
            return eval_program(n, env);
        case NODE_EXTERN: {
            extern int load_external_file_into_env(const char* path, Env* env);
            char* name = n->text;
            if(!name) return value_null();
            char* local = dh_concat("./", name);
            int ok = load_external_file_into_env(local, env);
            free(local);
            if(!ok){
                char* p = dh_concat("./extern_packages/", name);
                ok = load_external_file_into_env(p, env);
                free(p);
            }
            return value_null();
        }
        default:
            return value_null();
    }
}

int execute_program(Node* program, Env* env){
    if(!program) return 0;
    Value v = eval_node(program, env);
    value_free(&v);
    return 1;
}

int execute_file(const char* path, Env* env){
    char* src = read_file_to_string(path);
    if(!src) return 0;
    Node* n = parse_program(src);
    free(src);
    if(!n) return 0;
    execute_program(n, env);
    free_node(n);
    return 1;
}