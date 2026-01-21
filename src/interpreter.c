#include "env.h"
#include "value.h"
#include "utils.h"
#include "builtins.h"
#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

extern int load_external_file_into_env(const char* path, Env* env);

static Value eval_node(Node* n, Env* env);
static Value eval_program(Node* n, Env* env);
static Value eval_call(Node* cal, Env* env);
static Node* clone_node(Node* n);
static void register_symbols_from_ast(Node* ast, Env* env);
static Value perform_binary_op(const char* op, const Value* a, const Value* b);

Value value_func(char** params, size_t paramc, Node* body, Env* closure) {
    Value v;
    v.type = V_FUNC;
    v.v.func.paramc = paramc;
    v.v.func.params = NULL;
    if (paramc) {
        v.v.func.params = malloc(sizeof(char*) * paramc);
        for (size_t i = 0; i < paramc; ++i) v.v.func.params[i] = dh_strdup(params[i] ? params[i] : "");
    }
    v.v.func.body = body;
    v.v.func.closure = closure ? env_clone_recursive(closure) : NULL;
    return v;
}

static Env* g_env = NULL;
Env* global_env(void) {
    if (!g_env) g_env = env_new(NULL);
    return g_env;
}

int execute_program(Node* program, Env* env);
int interpret_file(const char* path, Env* env) {
    if (!path || !env) return 0;
    char* src = read_file_to_string(path);
    if (!src) return 0;
    Node* program = parse_program(src);
    free(src);
    if (!program) return 0;
    register_symbols_from_ast(program, env);
    int exec_result = execute_program(program, env);
    free_node(program);
    return exec_result == 0 ? 1 : 0;
}

static void register_symbols_from_ast(Node* ast, Env* env) {
    if (!ast) return;
    for (size_t i = 0; i < ast->childc; ++i) {
        Node* child = ast->children[i];
        if (!child) continue;
        if (child->type == NODE_FUNC) {
            size_t paramc = child->childc > 0 ? child->childc - 1 : 0;
            char** params = NULL;
            if (paramc) {
                params = malloc(sizeof(char*) * paramc);
                for (size_t j = 0; j < paramc; ++j) params[j] = dh_strdup(child->children[j]->text ? child->children[j]->text : "");
            }
            Node* body = child->childc > 0 ? child->children[child->childc - 1] : NULL;
            Node* body_clone = body ? clone_node(body) : NULL;
            Value fval = value_func(params, paramc, body_clone, env);
            if (params) {
                for (size_t j = 0; j < paramc; ++j) free(params[j]);
                free(params);
            }
            env_set(env, child->text ? child->text : "", fval);
            value_free(&fval);
        } else if (child->type == NODE_EXTERN) {
            if (!child->text) continue;
            char* local = dh_concat("./", child->text);
            int ok = load_external_file_into_env(local, env);
            free(local);
            if (!ok) {
                char* alt = dh_concat("./extern_packages/", child->text);
                load_external_file_into_env(alt, env);
                free(alt);
            }
        } else if (child->type == NODE_IMPORT) {
            if (!child->text) continue;
            char* full = dh_strdup(child->text);
            interpret_file(full, env);
            free(full);
        }
    }
}

static Node* clone_node(Node* n) {
    if (!n) return NULL;
    Node* c = malloc(sizeof(Node));
    if (!c) exit(1);
    c->type = n->type;
    c->childc = n->childc;
    c->capacity = n->capacity;
    c->num = n->num;
    c->text = n->text ? dh_strdup(n->text) : NULL;
    if (c->capacity) {
        c->children = malloc(sizeof(Node*) * c->capacity);
        for (size_t i = 0; i < c->childc; ++i) c->children[i] = clone_node(n->children[i]);
        for (size_t i = c->childc; i < c->capacity; ++i) c->children[i] = NULL;
    } else {
        c->children = NULL;
    }
    return c;
}

static Value make_error_string(const char* msg) {
    return value_string(msg ? msg : "");
}

static Value call_native(NativeFn fn, Env* env, Node** args, size_t argc) {
    Value* argv = NULL;
    if (argc) {
        argv = malloc(sizeof(Value) * argc);
        for (size_t i = 0; i < argc; ++i) argv[i] = eval_node(args[i], env);
    }
    Value out = fn(env, argv, argc);
    if (argv) {
        for (size_t i = 0; i < argc; ++i) value_free(&argv[i]);
        free(argv);
    }
    return out;
}

static Value call_user_function(Value* fval, Env* env, Node** args, size_t argc) {
    if (!fval || fval->type != V_FUNC) return value_null();
    size_t paramc = fval->v.func.paramc;
    char** params = fval->v.func.params;
    Node* body = (Node*)fval->v.func.body;
    Env* closure = fval->v.func.closure ? fval->v.func.closure : env;
    Env* local = env_new(closure);
    for (size_t i = 0; i < paramc; ++i) {
        Value av = value_null();
        if (i < argc) av = eval_node(args[i], env);
        env_set(local, params[i], av);
        value_free(&av);
    }
    Value result = value_null();
    if (body) {
        if (body->type == NODE_BLOCK) result = eval_program(body, local);
        else result = eval_node(body, local);
    }
    Value out = value_clone(&result);
    value_free(&result);
    env_free(local);
    return out;
}

static Value eval_call(Node* cal, Env* env) {
    if (!cal) return value_null();
    Value fnv = value_null();
    Node** argnodes = NULL;
    size_t argc = 0;
    if (cal->text) {
        if (!env_get(env, cal->text, &fnv)) return make_error_string("undefined function");
        argnodes = cal->children;
        argc = cal->childc;
    } else if (cal->childc > 0) {
        fnv = eval_node(cal->children[0], env);
        argnodes = cal->children + 1;
        argc = cal->childc > 0 ? cal->childc - 1 : 0;
    } else {
        return value_null();
    }
    if (fnv.type == V_NATIVE) {
        Value out = call_native(fnv.v.native.fn, env, argnodes, argc);
        value_free(&fnv);
        return out;
    }
    if (fnv.type == V_FUNC) {
        Value out = call_user_function(&fnv, env, argnodes, argc);
        value_free(&fnv);
        return out;
    }
    value_free(&fnv);
    return make_error_string("value not callable");
}

static Value eval_program(Node* n, Env* env) {
    Value last = value_null();
    for (size_t i = 0; i < n->childc; ++i) {
        if (i > 0) value_free(&last);
        Node* child = n->children[i];
        last = eval_node(child, env);
        if (child->type == NODE_RETURN) {
            Value out = value_clone(&last);
            value_free(&last);
            return out;
        }
    }
    return last;
}

static Value perform_binary_op(const char* op, const Value* a, const Value* b) {
    if (!op) return value_null();
    if (strcmp(op, "+") == 0) {
        if (a->type == V_STRING || b->type == V_STRING) {
            char* sa = value_to_string(a);
            char* sb = value_to_string(b);
            char* c = dh_concat(sa, sb);
            Value r = value_string(c);
            free(sa); free(sb); free(c);
            return r;
        } else if (a->type == V_INT && b->type == V_INT) {
            return value_int(a->v.i + b->v.i);
        } else {
            double av = (a->type == V_FLOAT ? a->v.f : (double)(a->type == V_INT ? a->v.i : 0));
            double bv = (b->type == V_FLOAT ? b->v.f : (double)(b->type == V_INT ? b->v.i : 0));
            return value_float(av + bv);
        }
    } else if (strcmp(op, "-") == 0) {
        if (a->type == V_INT && b->type == V_INT) return value_int(a->v.i - b->v.i);
        double av = (a->type == V_FLOAT ? a->v.f : (double)(a->type == V_INT ? a->v.i : 0));
        double bv = (b->type == V_FLOAT ? b->v.f : (double)(b->type == V_INT ? b->v.i : 0));
        return value_float(av - bv);
    } else if (strcmp(op, "*") == 0) {
        if (a->type == V_INT && b->type == V_INT) return value_int(a->v.i * b->v.i);
        double av = (a->type == V_FLOAT ? a->v.f : (double)(a->type == V_INT ? a->v.i : 0));
        double bv = (b->type == V_FLOAT ? b->v.f : (double)(b->type == V_INT ? b->v.i : 0));
        return value_float(av * bv);
    } else if (strcmp(op, "/") == 0) {
        double av = (a->type == V_FLOAT ? a->v.f : (double)(a->type == V_INT ? a->v.i : 0));
        double bv = (b->type == V_FLOAT ? b->v.f : (double)(b->type == V_INT ? b->v.i : 0));
        return value_float(av / bv);
    } else if (strcmp(op, "%") == 0) {
        if (a->type == V_INT && b->type == V_INT) return value_int(a->v.i % b->v.i);
        double av = (a->type == V_FLOAT ? a->v.f : (double)(a->type == V_INT ? a->v.i : 0));
        double bv = (b->type == V_FLOAT ? b->v.f : (double)(b->type == V_INT ? b->v.i : 0));
        return value_float(fmod(av, bv));
    }
    return value_null();
}

static Value eval_node(Node* n, Env* env) {
    if (!n) return value_null();
    switch (n->type) {
        case NODE_PROGRAM:
            return eval_program(n, env);
        case NODE_EXPR_STMT:
            return eval_node(n->children[0], env);
        case NODE_LET: {
            Value v = value_null();
            if (n->childc > 0) v = eval_node(n->children[0], env);
            env_set(env, n->text ? n->text : "", v);
            Value out = value_clone(&v);
            value_free(&v);
            return out;
        }
        case NODE_LITERAL:
            if (n->text) return value_string(n->text);
            else {
                double num = n->num;
                long long as_int = (long long)num;
                if ((double)as_int == num) return value_int(as_int);
                return value_float(num);
            }
        case NODE_STRING:
            if (n->text) return value_string(n->text);
            return value_null();
        case NODE_RETURN:
            if (n->childc > 0) return eval_node(n->children[0], env);
            return value_null();
        case NODE_IDENT: {
            Value out;
            if (env_get(env, n->text, &out)) return out;
            return value_null();
        }
        case NODE_INDEX: {
            if (n->childc < 2) return value_null();
            Value container = eval_node(n->children[0], env);
            Value index = eval_node(n->children[1], env);
            if (container.type == V_MAP && index.type == V_STRING) {
                for (size_t i = 0; i < container.v.map.len; ++i) {
                    if (strcmp(container.v.map.keys[i], index.v.s) == 0) {
                        Value out = value_clone(container.v.map.vals[i]);
                        value_free(&container);
                        value_free(&index);
                        return out;
                    }
                }
                value_free(&container);
                value_free(&index);
                return value_null();
            }
            if (container.type == V_LIST && index.type == V_INT) {
                long long idx = index.v.i;
                if (idx >= 0 && (size_t)idx < container.v.list.len) {
                    Value out = value_clone(container.v.list.items[idx]);
                    value_free(&container);
                    value_free(&index);
                    return out;
                }
            }
            value_free(&container);
            value_free(&index);
            return value_null();
        }
        case NODE_UNARY: {
            Value v = value_null();
            if (n->childc > 0) v = eval_node(n->children[0], env);
            Value r = value_null();
            if (n->text && strcmp(n->text, "-") == 0) {
                if (v.type == V_INT) r = value_int(-v.v.i);
                else if (v.type == V_FLOAT) r = value_float(-v.v.f);
            } else if (n->text && strcmp(n->text, "!") == 0) {
                int b = 0;
                if (v.type == V_BOOL) b = !v.v.b;
                else if (v.type == V_INT) b = !(v.v.i != 0);
                else if (v.type == V_FLOAT) b = !(v.v.f != 0.0);
                r = value_bool(b);
            }
            value_free(&v);
            return r;
        }
        case NODE_ASSIGN: {
            if (n->childc < 2) return value_null();
            Node* left = n->children[0];
            if (!left || left->type != NODE_IDENT) return value_null();
            const char* name = left->text ? left->text : "";
            Value rhs = eval_node(n->children[1], env);
            if (!n->text || strcmp(n->text, "=") == 0) {
                env_set(env, name, rhs);
                Value out = value_clone(&rhs);
                value_free(&rhs);
                return out;
            } else {
                Value cur;
                if (!env_get(env, name, &cur)) cur = value_null();
                Value res = value_null();
                if (strcmp(n->text, "+=") == 0) res = perform_binary_op("+", &cur, &rhs);
                else if (strcmp(n->text, "-=") == 0) res = perform_binary_op("-", &cur, &rhs);
                else if (strcmp(n->text, "*=") == 0) res = perform_binary_op("*", &cur, &rhs);
                else if (strcmp(n->text, "/=") == 0) res = perform_binary_op("/", &cur, &rhs);
                else if (strcmp(n->text, "%=") == 0) res = perform_binary_op("%", &cur, &rhs);
                else res = value_clone(&rhs);
                env_set(env, name, res);
                value_free(&cur);
                value_free(&rhs);
                Value out = value_clone(&res);
                value_free(&res);
                return out;
            }
        }
        case NODE_BINARY: {
            Value a = eval_node(n->children[0], env);
            Value b = eval_node(n->children[1], env);
            char* op = n->text;
            Value res = value_null();
            if (op && strcmp(op, "+") == 0) res = perform_binary_op("+", &a, &b);
            else if (op && strcmp(op, "-") == 0) res = perform_binary_op("-", &a, &b);
            else if (op && strcmp(op, "*") == 0) res = perform_binary_op("*", &a, &b);
            else if (op && strcmp(op, "/") == 0) res = perform_binary_op("/", &a, &b);
            else if (op && strcmp(op, "%") == 0) res = perform_binary_op("%", &a, &b);
            else if (op && strcmp(op, "==") == 0) {
                int eq = 0;
                if (a.type == V_INT && b.type == V_INT) eq = (a.v.i == b.v.i);
                else {
                    double av = (a.type == V_FLOAT ? a.v.f : (double)(a.type == V_INT ? a.v.i : 0));
                    double bv = (b.type == V_FLOAT ? b.v.f : (double)(b.type == V_INT ? b.v.i : 0));
                    eq = (av == bv);
                }
                res = value_bool(eq);
            } else if (op && strcmp(op, "!=") == 0) {
                int ne = 0;
                if (a.type == V_INT && b.type == V_INT) ne = (a.v.i != b.v.i);
                else {
                    double av = (a.type == V_FLOAT ? a.v.f : (double)(a.type == V_INT ? a.v.i : 0));
                    double bv = (b.type == V_FLOAT ? b.v.f : (double)(b.type == V_INT ? b.v.i : 0));
                    ne = (av != bv);
                }
                res = value_bool(ne);
            } else if (op && strcmp(op, "<") == 0) {
                double av = (a.type == V_FLOAT ? a.v.f : (double)(a.type == V_INT ? a.v.i : 0));
                double bv = (b.type == V_FLOAT ? b.v.f : (double)(b.type == V_INT ? b.v.i : 0));
                res = value_bool(av < bv);
            } else if (op && strcmp(op, ">") == 0) {
                double av = (a.type == V_FLOAT ? a.v.f : (double)(a.type == V_INT ? a.v.i : 0));
                double bv = (b.type == V_FLOAT ? b.v.f : (double)(b.type == V_INT ? b.v.i : 0));
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
            if (paramc) {
                params = malloc(sizeof(char*) * paramc);
                for (size_t i = 0; i < paramc; ++i) params[i] = dh_strdup(n->children[i]->text ? n->children[i]->text : "");
            }
            Node* body = n->childc > 0 ? n->children[n->childc - 1] : NULL;
            Node* body_clone = body ? clone_node(body) : NULL;
            Value fval = value_func(params, paramc, body_clone, env);
            if (params) {
                for (size_t i = 0; i < paramc; ++i) free(params[i]);
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
        case NODE_IF: {
            Value cond = eval_node(n->children[0], env);
            int truth = 0;
            if (cond.type == V_BOOL) truth = cond.v.b;
            else if (cond.type == V_INT) truth = cond.v.i != 0;
            else if (cond.type == V_FLOAT) truth = cond.v.f != 0.0;
            value_free(&cond);
            if (truth) return eval_node(n->children[1], env);
            if (n->childc > 2) return eval_node(n->children[2], env);
            return value_null();
        }
        case NODE_LOOP: {
            Value out = value_null();
            while (1) {
                Value cond = eval_node(n->children[0], env);
                int truth = 0;
                if (cond.type == V_BOOL) truth = cond.v.b;
                else if (cond.type == V_INT) truth = cond.v.i != 0;
                else if (cond.type == V_FLOAT) truth = cond.v.f != 0.0;
                value_free(&cond);
                if (!truth) break;
                value_free(&out);
                out = eval_node(n->children[1], env);
            }
            return out;
        }
        case NODE_EXTERN: {
            if (!n->text) return value_null();
            char* local = dh_concat("./", n->text);
            int ok = load_external_file_into_env(local, env);
            free(local);
            if (!ok) {
                char* alt = dh_concat("./extern_packages/", n->text);
                load_external_file_into_env(alt, env);
                free(alt);
            }
            return value_null();
        }
        case NODE_IMPORT: {
            if (!n->text) return value_null();
            int ok = interpret_file(n->text, env);
            if (!ok) {
                char* alt = dh_concat("./extern_packages/", n->text);
                interpret_file(alt, env);
                free(alt);
            }
            return value_null();
        }
        default:
            return value_null();
    }
}

int execute_program(Node* program, Env* env) {
    if (!program) return 1;
    Value v = eval_node(program, env);
    value_free(&v);
    return 0;
}

int execute_file(const char* path, Env* env) {
    char* src = read_file_to_string(path);
    if (!src) return 1;
    Node* n = parse_program(src);
    free(src);
    if (!n) return 1;
    int r = execute_program(n, env);
    free_node(n);
    return r;
}