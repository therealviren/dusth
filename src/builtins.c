#include "builtins.h"
#include "utils.h"
#include "parser.h"
#include "env.h"
#include "interpreter.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>

static int dh_truthy(const Value* v){
    if(!v) return 0;
    switch(v->type){
        case V_NULL: return 0;
        case V_BOOL: return v->v.b;
        case V_INT: return v->v.i != 0;
        case V_FLOAT: return v->v.f != 0.0;
        case V_STRING: return v->v.s && v->v.s[0];
        case V_LIST: return v->v.list.len != 0;
        case V_MAP: return v->v.map.len != 0;
        default: return 1;
    }
}

static double dh_to_double(const Value* v){
    if(!v) return 0.0;
    if(v->type==V_FLOAT) return v->v.f;
    if(v->type==V_INT) return (double)v->v.i;
    if(v->type==V_BOOL) return (double)(v->v.b?1:0);
    if(v->type==V_STRING) return atof(v->v.s);
    return 0.0;
}

static Value bh_say(Env* env, Value* args, size_t argc){
    (void)env;
    for(size_t i=0;i<argc;i++){
        char* s=value_to_string(&args[i]);
        printf("%s\n",s);
        free(s);
    }
    return value_null();
}
static Value bh_print(Env* env, Value* args, size_t argc){
    (void)env;
    for(size_t i=0;i<argc;i++){
        char* s=value_to_string(&args[i]);
        printf("%s",s);
        free(s);
    }
    return value_null();
}
static Value bh_len(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_int(0);
    if(args[0].type==V_STRING) return value_int((long long)strlen(args[0].v.s));
    if(args[0].type==V_LIST) return value_int((long long)args[0].v.list.len);
    if(args[0].type==V_MAP) return value_int((long long)args[0].v.map.len);
    return value_int(0);
}
static Value bh_to_string(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_string("");
    char* s=value_to_string(&args[0]);
    Value r=value_string(s);
    free(s);
    return r;
}
static Value bh_to_int(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_int(0);
    if(args[0].type==V_INT) return value_int(args[0].v.i);
    if(args[0].type==V_FLOAT) return value_int((long long)args[0].v.f);
    if(args[0].type==V_STRING) return value_int((long long)atoll(args[0].v.s));
    if(args[0].type==V_BOOL) return value_int(args[0].v.b?1:0);
    return value_int(0);
}
static Value bh_to_float(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_float(0.0);
    if(args[0].type==V_FLOAT) return value_float(args[0].v.f);
    if(args[0].type==V_INT) return value_float((double)args[0].v.i);
    if(args[0].type==V_STRING) return value_float(atof(args[0].v.s));
    if(args[0].type==V_BOOL) return value_float(args[0].v.b?1.0:0.0);
    return value_float(0.0);
}
static Value bh_type_of(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_string("null");
    switch(args[0].type){
        case V_NULL: return value_string("null");
        case V_BOOL: return value_string("bool");
        case V_INT: return value_string("int");
        case V_FLOAT: return value_string("float");
        case V_STRING: return value_string("string");
        case V_LIST: return value_string("list");
        case V_MAP: return value_string("map");
        case V_FUNC: return value_string("function");
        case V_NATIVE: return value_string("native");
        default: return value_string("unknown");
    }
}
static Value bh_abs(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_int(0);
    if(args[0].type==V_INT) return value_int(llabs(args[0].v.i));
    return value_float(fabs((args[0].type==V_FLOAT?args[0].v.f:(double)args[0].v.i)));
}
static Value bh_powf(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<2) return value_float(0.0);
    double a=(args[0].type==V_FLOAT?args[0].v.f:(double)args[0].v.i);
    double b=(args[1].type==V_FLOAT?args[1].v.f:(double)args[1].v.i);
    return value_float(pow(a,b));
}
static Value bh_sqrtf(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_float(0.0);
    double a=(args[0].type==V_FLOAT?args[0].v.f:(double)args[0].v.i);
    return value_float(sqrt(a));
}
static Value bh_sinf(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_float(0.0);
    double a=(args[0].type==V_FLOAT?args[0].v.f:(double)args[0].v.i);
    return value_float(sin(a));
}
static Value bh_cosf(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_float(0.0);
    double a=(args[0].type==V_FLOAT?args[0].v.f:(double)args[0].v.i);
    return value_float(cos(a));
}
static Value bh_tanf(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_float(0.0);
    double a=(args[0].type==V_FLOAT?args[0].v.f:(double)args[0].v.i);
    return value_float(tan(a));
}
static Value bh_floorf(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_float(0.0);
    double a=(args[0].type==V_FLOAT?args[0].v.f:(double)args[0].v.i);
    return value_float(floor(a));
}
static Value bh_ceilf(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_float(0.0);
    double a=(args[0].type==V_FLOAT?args[0].v.f:(double)args[0].v.i);
    return value_float(ceil(a));
}
static Value bh_randn(Env* env, Value* args, size_t argc){
    (void)env; (void)args; (void)argc;
    return value_int((long long)rand());
}
static Value bh_srandn(Env* env, Value* args, size_t argc){
    (void)env;
    unsigned int seed=(unsigned int)time(NULL);
    if(argc>=1 && args[0].type==V_INT) seed=(unsigned int)args[0].v.i;
    srand(seed);
    return value_null();
}
static Value bh_range(Env* env, Value* args, size_t argc){
    (void)env;
    long long a=0,b=0;
    if(argc==1){ b=args[0].v.i; }
    else if(argc>=2){ a=args[0].v.i; b=args[1].v.i; }
    Value L = value_list();
    Value** items = NULL;
    size_t len = 0;
    for(long long i=a;i<b;i++){
        Value it = value_int(i);
        Value* p = malloc(sizeof(Value));
        if(!p){
            value_free(&it);
            for(size_t j=0;j<len;j++){ value_free(items[j]); free(items[j]); }
            free(items);
            return value_list();
        }
        *p = value_clone(&it);
        value_free(&it);
        Value** tmp = realloc(items, sizeof(Value*) * (len + 1));
        if(!tmp){
            value_free(p);
            free(p);
            for(size_t j=0;j<len;j++){ value_free(items[j]); free(items[j]); }
            free(items);
            return value_list();
        }
        items = tmp;
        items[len++] = p;
    }
    L.v.list.items = items;
    L.v.list.len = len;
    L.v.list.cap = len;
    return L;
}
static Value bh_push(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<2) return value_null();
    if(args[0].type!=V_LIST) return value_null();
    Value L = value_clone(&args[0]);
    Value val = value_clone(&args[1]);
    Value* p = malloc(sizeof(Value));
    if(!p){ value_free(&L); value_free(&val); return value_null(); }
    *p = value_clone(&val);
    value_free(&val);
    Value** tmp = realloc(L.v.list.items, sizeof(Value*)*(L.v.list.len+1));
    if(!tmp){ free(p); value_free(&L); return value_null(); }
    L.v.list.items = tmp;
    L.v.list.items[L.v.list.len++] = p;
    return L;
}
static Value bh_pop(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_null();
    if(args[0].type!=V_LIST) return value_null();
    if(args[0].v.list.len==0) return value_null();
    Value L = value_clone(&args[0]);
    Value* it = L.v.list.items[L.v.list.len-1];
    Value out = value_clone(it);
    value_free(it);
    free(it);
    L.v.list.len--;
    return out;
}
static Value bh_shift(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_null();
    if(args[0].type!=V_LIST) return value_null();
    if(args[0].v.list.len==0) return value_null();
    Value L = value_clone(&args[0]);
    Value* it = L.v.list.items[0];
    Value out = value_clone(it);
    value_free(it);
    free(it);
    for(size_t i=1;i<L.v.list.len;i++) L.v.list.items[i-1]=L.v.list.items[i];
    L.v.list.len--;
    return out;
}
static Value bh_unshift(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<2) return value_null();
    if(args[0].type!=V_LIST) return value_null();
    Value L = value_clone(&args[0]);
    Value* p=malloc(sizeof(Value));
    if(!p){ value_free(&L); return value_null(); }
    *p = value_clone(&args[1]);
    Value** tmp = realloc(L.v.list.items,sizeof(Value*)*(L.v.list.len+1));
    if(!tmp){ value_free(p); free(p); value_free(&L); return value_null(); }
    L.v.list.items = tmp;
    for(size_t i=L.v.list.len;i>0;i--) L.v.list.items[i]=L.v.list.items[i-1];
    L.v.list.items[0]=p;
    L.v.list.len++;
    return L;
}
static Value bh_mapf(Env* env, Value* args, size_t argc){
    if(argc<2) return value_list();
    if(args[0].type!=V_LIST) return value_list();
    if(args[1].type!=V_NATIVE) return value_list();
    NativeFn fn=args[1].v.native.fn;
    Value L=value_list();
    for(size_t i=0;i<args[0].v.list.len;i++){
        Value item=value_clone(args[0].v.list.items[i]);
        Value callarg=value_clone(&item);
        Value res = fn(env,&callarg,1);
        value_free(&callarg);
        Value* p=malloc(sizeof(Value));
        if(!p){ value_free(&item); continue; }
        *p=value_clone(&res);
        Value** arr=realloc(L.v.list.items,sizeof(Value*)*(L.v.list.len+1));
        if(!arr){ free(p); value_free(&res); value_free(&item); continue; }
        L.v.list.items=arr;
        L.v.list.items[L.v.list.len++]=p;
        value_free(&res);
        value_free(&item);
    }
    return L;
}
static Value bh_filterf(Env* env, Value* args, size_t argc){
    if(argc<2) return value_list();
    if(args[0].type!=V_LIST) return value_list();
    if(args[1].type!=V_NATIVE) return value_list();
    NativeFn fn=args[1].v.native.fn;
    Value L=value_list();
    for(size_t i=0;i<args[0].v.list.len;i++){
        Value item=value_clone(args[0].v.list.items[i]);
        Value callarg=value_clone(&item);
        Value res=fn(env,&callarg,1);
        int keep=0;
        if(res.type==V_BOOL) keep=res.v.b;
        else if(res.type==V_INT) keep=(res.v.i!=0);
        else if(res.type==V_FLOAT) keep=(res.v.f!=0.0);
        value_free(&callarg);
        if(keep){
            Value* p=malloc(sizeof(Value));
            if(p){
                *p=value_clone(&item);
                Value** arr=realloc(L.v.list.items,sizeof(Value*)*(L.v.list.len+1));
                if(arr){ L.v.list.items=arr; L.v.list.items[L.v.list.len++]=p; }
                else { value_free(p); free(p); }
            }
        }
        value_free(&res);
        value_free(&item);
    }
    return L;
}
static Value bh_reducef(Env* env, Value* args, size_t argc){
    if(argc<2) return value_null();
    if(args[0].type!=V_LIST) return value_null();
    if(args[1].type!=V_NATIVE) return value_null();
    NativeFn fn=args[1].v.native.fn;
    Value acc;
    size_t start=0;
    if(argc>=3){
        acc=value_clone(&args[2]);
        start=0;
    } else {
        if(args[0].v.list.len==0) return value_null();
        acc = value_clone(args[0].v.list.items[0]);
        start=1;
    }
    for(size_t i=start;i<args[0].v.list.len;i++){
        Value item=value_clone(args[0].v.list.items[i]);
        Value callargs[2];
        callargs[0]=value_clone(&acc);
        callargs[1]=value_clone(&item);
        Value res=fn(env,callargs,2);
        value_free(&callargs[0]);
        value_free(&callargs[1]);
        value_free(&acc);
        acc = value_clone(&res);
        value_free(&res);
        value_free(&item);
    }
    return acc;
}
static Value bh_read_file(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1||args[0].type!=V_STRING) return value_null();
    char* s=read_file_to_string(args[0].v.s);
    if(!s) return value_null();
    Value v=value_string(s);
    free(s);
    return v;
}
static Value bh_write_file(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<2||args[0].type!=V_STRING) return value_null();
    char* c=NULL;
    if(args[1].type==V_STRING) c=args[1].v.s;
    else { char* t=value_to_string(&args[1]); c=t; int ok=write_string_to_file(args[0].v.s,c); if(args[1].type!=V_STRING) free(c); return value_bool(ok); }
    int ok=write_string_to_file(args[0].v.s,c);
    return value_bool(ok);
}
static Value bh_file_exists(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1||args[0].type!=V_STRING) return value_bool(0);
    FILE* f=fopen(args[0].v.s,"rb");
    if(!f) return value_bool(0);
    fclose(f);
    return value_bool(1);
}
static Value bh_sleep_ms(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_null();
    long long ms=args[0].v.i;
    usleep((useconds_t)(ms*1000));
    return value_null();
}
static Value bh_time_unix(Env* env, Value* args, size_t argc){
    (void)args; (void)argc; (void)env;
    time_t t=time(NULL);
    return value_int((long long)t);
}
static Value bh_now_str(Env* env, Value* args, size_t argc){
    (void)args; (void)argc;
    char* s=dh_now_iso();
    Value v=value_string(s);
    free(s);
    return v;
}
static Value bh_getenvv(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_string("");
    char* v=getenv(args[0].v.s);
    if(!v) return value_string("");
    return value_string(v);
}
static Value bh_setenvv(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<2) return value_bool(0);
    setenv(args[0].v.s,args[1].v.s,1);
    return value_bool(1);
}
static Value bh_exitv(Env* env, Value* args, size_t argc){
    (void)env;
    int code=0;
    if(argc>=1 && args[0].type==V_INT) code=(int)args[0].v.i;
    exit(code);
    return value_null();
}
static Value bh_assertv(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_null();
    int ok=0;
    if(args[0].type==V_BOOL) ok=args[0].v.b;
    if(!ok){
        printf("Assertion failed\n");
        exit(1);
    }
    return value_null();
}
static Value bh_panicv(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc>=1){
        char* s=value_to_string(&args[0]);
        printf("Panic: %s\n",s);
        free(s);
    }
    exit(1);
    return value_null();
}
static Value bh_spawnv(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_int(-1);
    char* cmd=value_to_string(&args[0]);
    int st=system(cmd);
    free(cmd);
    return value_int((long long)st);
}
static Value bh_evalv(Env* env, Value* args, size_t argc){
    (void)argc;
    if(argc<1||args[0].type!=V_STRING) return value_null();
    char* src=args[0].v.s;
    Node* n=parse_program(src);
    if(!n) return value_null();
    execute_program(n,env);
    free_node(n);
    return value_null();
}
static Value bh_keys(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1||args[0].type!=V_MAP) return value_list();
    Value L=value_list();
    for(size_t i=0;i<args[0].v.map.len;i++){
        Value ks=value_string(args[0].v.map.keys[i]);
        Value* p=malloc(sizeof(Value));
        if(!p){ value_free(&ks); continue; }
        *p = ks;
        Value** arr=realloc(L.v.list.items,sizeof(Value*)*(L.v.list.len+1));
        if(!arr){ value_free(p); free(p); value_free(&ks); continue; }
        L.v.list.items=arr;
        L.v.list.items[L.v.list.len++]=p;
    }
    return L;
}
static Value bh_values(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1||args[0].type!=V_MAP) return value_list();
    Value L=value_list();
    for(size_t i=0;i<args[0].v.map.len;i++){
        Value vs=value_clone(args[0].v.map.vals[i]);
        Value* p=malloc(sizeof(Value));
        if(!p){ value_free(&vs); continue; }
        *p = vs;
        Value** arr=realloc(L.v.list.items,sizeof(Value*)*(L.v.list.len+1));
        if(!arr){ value_free(p); free(p); value_free(&vs); continue; }
        L.v.list.items=arr;
        L.v.list.items[L.v.list.len++]=p;
    }
    return L;
}
static Value bh_input(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc >= 1 && args[0].type == V_STRING){
        char* prompt = args[0].v.s;
        if(prompt && prompt[0]){ fputs(prompt, stdout); fflush(stdout); }
    }
    char* line = NULL;
    size_t len = 0;
    ssize_t n = getline(&line, &len, stdin);
    if(n <= 0){ if(line) free(line); return value_string(""); }
    if(n > 0 && line[n-1] == '\n') line[n-1] = '\0';
    Value v = value_string(line);
    free(line);
    return v;
}
static Value bh_os_call(Env* env, Value* args, size_t argc){
    if(argc < 1) return value_int(-1);
    char* cmd = value_to_string(&args[0]);
    int st = system(cmd);
    free(cmd);
    return value_int((long long)st);
}
static Value bh_sh(Env* env, Value* args, size_t argc){
    if(argc < 1) return value_int(-1);
    char* cmd = value_to_string(&args[0]);
    int st = system(cmd);
    free(cmd);
    return value_int((long long)st);
}
static Value bh_echo(Env* env, Value* args, size_t argc){
    (void)env;
    for(size_t i=0;i<argc;i++){
        char* s = value_to_string(&args[i]);
        printf("%s", s);
        free(s);
        if(i+1<argc) printf(" ");
    }
    printf("\n");
    return value_null();
}
static Value bh_random(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc==0){
        double v = (double)rand() / ((double)RAND_MAX + 1.0);
        return value_float(v);
    }
    if(argc==1){
        if(args[0].type==V_INT){
            long long n = args[0].v.i;
            if(n <= 0) return value_int(0);
            return value_int((long long)(rand() % n));
        }
        if(args[0].type==V_STRING){
            int n = atoi(args[0].v.s);
            if(n <= 0) return value_string("");
            char* out = malloc(n+1);
            for(int i=0;i<n;i++) out[i] = 'a' + (rand()%26);
            out[n]=0;
            Value r = value_string(out);
            free(out);
            return r;
        }
        return value_null();
    }
    if(argc>=2){
        if(args[0].type==V_INT && args[1].type==V_INT){
            long long a = args[0].v.i;
            long long b = args[1].v.i;
            if (a > b) { long long t=a; a=b; b=t; }
            long long range = b - a + 1;
            if(range <= 0) return value_int(a);
            long long r = (long long)(rand() % range) + a;
            return value_int(r);
        }
        if(args[0].type==V_STRING && (args[1].type==V_INT || args[1].type==V_STRING)){
            const char* letters = args[0].v.s;
            int n = (args[1].type==V_INT) ? (int)args[1].v.i : atoi(args[1].v.s);
            if(n <= 0) return value_string("");
            size_t pool = strlen(letters);
            if(pool == 0) return value_string("");
            char* out = malloc(n+1);
            for(int i=0;i<n;i++) out[i] = letters[rand() % pool];
            out[n]=0;
            Value r = value_string(out);
            free(out);
            return r;
        }
    }
    return value_null();
}

static Value bh_int_cast(Env* env, Value* args, size_t argc){
    (void)env;
    return bh_to_int(env,args,argc);
}
static Value bh_float_cast(Env* env, Value* args, size_t argc){
    (void)env;
    return bh_to_float(env,args,argc);
}
static Value bh_str_cast(Env* env, Value* args, size_t argc){
    (void)env;
    return bh_to_string(env,args,argc);
}
static Value bh_bool_cast(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_bool(0);
    return value_bool(dh_truthy(&args[0]));
}
static Value bh_list_cast(Env* env, Value* args, size_t argc){
    (void)env;
    Value L = value_list();
    for(size_t i=0;i<argc;i++){
        Value* p = malloc(sizeof(Value));
        if(!p) continue;
        *p = value_clone(&args[i]);
        Value** arr = realloc(L.v.list.items,sizeof(Value*)*(L.v.list.len+1));
        if(!arr){ free(p); value_free(p); continue; }
        L.v.list.items = arr;
        L.v.list.items[L.v.list.len++] = p;
    }
    return L;
}
static Value bh_tuple_cast(Env* env, Value* args, size_t argc){
    (void)env;
    return bh_list_cast(env,args,argc);
}
static Value bh_dict_cast(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc==0) return value_map();
    if(argc==1 && args[0].type==V_MAP) return value_clone(&args[0]);
    return value_map();
}
static Value bh_chr(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_string("");
    if(args[0].type==V_INT){
        int c=(int)args[0].v.i;
        char s[2]={0,0};
        s[0]=(char)c;
        return value_string(s);
    }
    return value_string("");
}
static Value bh_ord(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_int(0);
    if(args[0].type==V_STRING && args[0].v.s && args[0].v.s[0]) return value_int((long long)(unsigned char)args[0].v.s[0]);
    return value_int(0);
}
static Value bh_hex(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_string("");
    if(args[0].type==V_INT){
        char* s = dh_from_int_hex(args[0].v.i);
        Value r = value_string(s);
        free(s);
        return r;
    }
    return value_string("");
}
static Value bh_oct(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_string("");
    if(args[0].type==V_INT){
        char* s = dh_from_int_oct(args[0].v.i);
        Value r = value_string(s);
        free(s);
        return r;
    }
    return value_string("");
}
static Value bh_bin(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_string("");
    if(args[0].type==V_INT){
        long long v=args[0].v.i;
        char buf[66];
        buf[65]=0;
        for(int i=0;i<64;i++) buf[i]= '0';
        int pos=64;
        unsigned long long uv=(unsigned long long)v;
        if(uv==0){ return value_string("0"); }
        while(uv){
            buf[--pos] = (uv & 1) ? '1' : '0';
            uv >>= 1;
        }
        return value_string(buf+pos);
    }
    return value_string("");
}

static Value bh_repr(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_string("null");
    char* s=value_to_string(&args[0]);
    Value r=value_string(s);
    free(s);
    return r;
}
static Value bh_ascii(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_string("");
    char* s=value_to_string(&args[0]);
    size_t L=strlen(s);
    char* out=malloc(L*4+1);
    size_t p=0;
    for(size_t i=0;i<L;i++){
        unsigned char c=(unsigned char)s[i];
        if(c>=32 && c<127){ out[p++]=c; }
        else { int n=sprintf(out+p,"\\x%02x",c); p+=n; }
    }
    out[p]=0;
    Value r=value_string(out);
    free(out);
    free(s);
    return r;
}
static Value bh_format(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_string("");
    char* fmt=value_to_string(&args[0]);
    char buf[4096];
    if(argc==1){
        snprintf(buf,sizeof(buf),"%s",fmt);
    } else {
        char* arg=value_to_string(&args[1]);
        snprintf(buf,sizeof(buf),fmt,arg);
        free(arg);
    }
    Value r=value_string(buf);
    free(fmt);
    return r;
}
static Value bh_divmod(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<2) return value_null();
    if(args[0].type==V_INT && args[1].type==V_INT){
        long long a=args[0].v.i;
        long long b=args[1].v.i;
        if(b==0) return value_list();
        long long q=a/b;
        long long r=a%b;
        Value* qv=malloc(sizeof(Value));
        Value* rv=malloc(sizeof(Value));
        *qv=value_int(q);
        *rv=value_int(r);
        Value L=value_list();
        L.v.list.items = malloc(sizeof(Value*)*2);
        L.v.list.items[0]=qv;
        L.v.list.items[1]=rv;
        L.v.list.len=2;
        L.v.list.cap=2;
        return L;
    }
    double a=dh_to_double(&args[0]);
    double b=dh_to_double(&args[1]);
    if(b==0.0) return value_list();
    double q=floor(a/b);
    double r=a - b*q;
    Value* qv=malloc(sizeof(Value));
    Value* rv=malloc(sizeof(Value));
    *qv=value_float(q);
    *rv=value_float(r);
    Value L=value_list();
    L.v.list.items = malloc(sizeof(Value*)*2);
    L.v.list.items[0]=qv;
    L.v.list.items[1]=rv;
    L.v.list.len=2;
    L.v.list.cap=2;
    return L;
}
static Value bh_sum(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_int(0);
    if(args[0].type==V_LIST){
        double acc=0.0;
        int allint=1;
        for(size_t i=0;i<args[0].v.list.len;i++){
            Value* it=args[0].v.list.items[i];
            if(it->type==V_INT) acc += (double)it->v.i;
            else if(it->type==V_FLOAT) { acc += it->v.f; allint=0; }
            else { allint=0; }
        }
        if(allint) return value_int((long long)acc);
        return value_float(acc);
    }
    return value_int(0);
}
static Value bh_min(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc==0) return value_null();
    if(argc==1 && args[0].type==V_LIST){
        if(args[0].v.list.len==0) return value_null();
        Value* first = args[0].v.list.items[0];
        Value out = value_clone(first);
        for(size_t i=1;i<args[0].v.list.len;i++){
            Value* it=args[0].v.list.items[i];
            if(it->type==V_INT && out.type==V_INT){
                if(it->v.i < out.v.i){ value_free(&out); out = value_clone(it); }
            } else {
                double a= (out.type==V_FLOAT?out.v.f:(double)(out.type==V_INT?out.v.i:0));
                double b= (it->type==V_FLOAT?it->v.f:(double)(it->type==V_INT?it->v.i:0));
                if(b < a){ value_free(&out); out = value_clone(it); }
            }
        }
        return out;
    } else {
        Value out = value_clone(&args[0]);
        for(int i=1;i<(int)argc;i++){
            Value cand=value_clone(&args[i]);
            double a= (out.type==V_FLOAT?out.v.f:(double)(out.type==V_INT?out.v.i:0));
            double b= (cand.type==V_FLOAT?cand.v.f:(double)(cand.type==V_INT?cand.v.i:0));
            if(b < a){ value_free(&out); out = value_clone(&cand); }
            value_free(&cand);
        }
        return out;
    }
}
static Value bh_max(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc==0) return value_null();
    if(argc==1 && args[0].type==V_LIST){
        if(args[0].v.list.len==0) return value_null();
        Value* first = args[0].v.list.items[0];
        Value out = value_clone(first);
        for(size_t i=1;i<args[0].v.list.len;i++){
            Value* it=args[0].v.list.items[i];
            if(it->type==V_INT && out.type==V_INT){
                if(it->v.i > out.v.i){ value_free(&out); out = value_clone(it); }
            } else {
                double a= (out.type==V_FLOAT?out.v.f:(double)(out.type==V_INT?out.v.i:0));
                double b= (it->type==V_FLOAT?it->v.f:(double)(it->type==V_INT?it->v.i:0));
                if(b > a){ value_free(&out); out = value_clone(it); }
            }
        }
        return out;
    } else {
        Value out = value_clone(&args[0]);
        for(int i=1;i<(int)argc;i++){
            Value cand=value_clone(&args[i]);
            double a= (out.type==V_FLOAT?out.v.f:(double)(out.type==V_INT?out.v.i:0));
            double b= (cand.type==V_FLOAT?cand.v.f:(double)(cand.type==V_INT?cand.v.i:0));
            if(b > a){ value_free(&out); out = value_clone(&cand); }
            value_free(&cand);
        }
        return out;
    }
}
static Value bh_all(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_bool(1);
    if(args[0].type!=V_LIST) return value_bool(dh_truthy(&args[0]));
    for(size_t i=0;i<args[0].v.list.len;i++){
        if(!dh_truthy(args[0].v.list.items[i])) return value_bool(0);
    }
    return value_bool(1);
}
static Value bh_any(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_bool(0);
    if(args[0].type!=V_LIST) return value_bool(dh_truthy(&args[0]));
    for(size_t i=0;i<args[0].v.list.len;i++){
        if(dh_truthy(args[0].v.list.items[i])) return value_bool(1);
    }
    return value_bool(0);
}

static Value bh_enumerate(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_list();
    if(args[0].type!=V_LIST) return value_list();
    Value L=value_list();
    for(size_t i=0;i<args[0].v.list.len;i++){
        Value* pair1=malloc(sizeof(Value));
        Value* pair2=malloc(sizeof(Value));
        *pair1 = value_int((long long)i);
        *pair2 = value_clone(args[0].v.list.items[i]);
        Value* item=malloc(sizeof(Value));
        *item = value_list();
        item->v.list.items = malloc(sizeof(Value*)*2);
        item->v.list.items[0]=pair1;
        item->v.list.items[1]=pair2;
        item->v.list.len=2;
        item->v.list.cap=2;
        Value** arr=realloc(L.v.list.items,sizeof(Value*)*(L.v.list.len+1));
        if(!arr){ value_free(item); free(item); continue; }
        L.v.list.items=arr;
        L.v.list.items[L.v.list.len++]=item;
    }
    return L;
}
static Value bh_zip(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc==0) return value_list();
    size_t minlen = SIZE_MAX;
    for(size_t k=0;k<argc;k++){
        if(args[k].type!=V_LIST) return value_list();
        if(minlen==SIZE_MAX) minlen = args[k].v.list.len;
        else if(args[k].v.list.len < minlen) minlen = args[k].v.list.len;
    }
    Value L=value_list();
    for(size_t i=0;i<minlen;i++){
        Value* item=malloc(sizeof(Value));
        *item = value_list();
        item->v.list.items = malloc(sizeof(Value*)*argc);
        item->v.list.len = argc;
        item->v.list.cap = argc;
        for(size_t k=0;k<argc;k++){
            Value* e = malloc(sizeof(Value));
            *e = value_clone(args[k].v.list.items[i]);
            item->v.list.items[k]=e;
        }
        Value** arr=realloc(L.v.list.items,sizeof(Value*)*(L.v.list.len+1));
        if(!arr){ value_free(item); free(item); continue; }
        L.v.list.items=arr;
        L.v.list.items[L.v.list.len++]=item;
    }
    return L;
}
static Value bh_reversed(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_list();
    if(args[0].type!=V_LIST) return value_list();
    Value L = value_clone(&args[0]);
    for(size_t i=0;i<L.v.list.len/2;i++){
        Value* a = L.v.list.items[i];
        Value* b = L.v.list.items[L.v.list.len-1-i];
        Value* tmp=a;
        L.v.list.items[i]=b;
        L.v.list.items[L.v.list.len-1-i]=tmp;
    }
    return L;
}
static int value_compare_for_sort(const void* A, const void* B){
    Value* const * a = A;
    Value* const * b = B;
    double av = (*a)->type==V_FLOAT?(*a)->v.f:(double)((*a)->type==V_INT?(*a)->v.i:0);
    double bv = (*b)->type==V_FLOAT?(*b)->v.f:(double)((*b)->type==V_INT?(*b)->v.i:0);
    if(av < bv) return -1;
    if(av > bv) return 1;
    return 0;
}
static Value bh_sorted(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_list();
    if(args[0].type!=V_LIST) return value_list();
    Value L = value_clone(&args[0]);
    qsort(L.v.list.items, L.v.list.len, sizeof(Value*), value_compare_for_sort);
    return L;
}

static Value bh_callable(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_bool(0);
    return value_bool(args[0].type==V_FUNC || args[0].type==V_NATIVE);
}
static Value bh_dir(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_list();
    if(args[0].type==V_MAP){
        return bh_keys(env,args,argc);
    }
    return value_list();
}
static Value bh_hasattr(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<2) return value_bool(0);
    if(args[0].type!=V_MAP||args[1].type!=V_STRING) return value_bool(0);
    for(size_t i=0;i<args[0].v.map.len;i++){
        if(strcmp(args[0].v.map.keys[i], args[1].v.s)==0) return value_bool(1);
    }
    return value_bool(0);
}
static Value bh_getattr(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<2) return value_null();
    if(args[0].type!=V_MAP||args[1].type!=V_STRING) return value_null();
    for(size_t i=0;i<args[0].v.map.len;i++){
        if(strcmp(args[0].v.map.keys[i], args[1].v.s)==0) return value_clone(args[0].v.map.vals[i]);
    }
    if(argc>=3) return value_clone(&args[2]);
    return value_null();
}
static Value bh_setattr(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<3) return value_bool(0);
    if(args[0].type!=V_MAP||args[1].type!=V_STRING) return value_bool(0);
    Value v = value_clone(&args[2]);
    int found=0;
    for(size_t i=0;i<args[0].v.map.len;i++){
        if(strcmp(args[0].v.map.keys[i], args[1].v.s)==0){
            value_free(args[0].v.map.vals[i]);
            *args[0].v.map.vals[i] = value_clone(&v);
            found=1;
            break;
        }
    }
    if(!found){
        map_grow(&args[0]);
        args[0].v.map.keys = realloc(args[0].v.map.keys, sizeof(char*)*(args[0].v.map.len+1));
        args[0].v.map.vals = realloc(args[0].v.map.vals, sizeof(Value*)*(args[0].v.map.len+1));
        args[0].v.map.keys[args[0].v.map.len] = dh_strdup(args[1].v.s);
        Value* p=malloc(sizeof(Value));
        *p = value_clone(&v);
        args[0].v.map.vals[args[0].v.map.len] = p;
        args[0].v.map.len++;
    }
    value_free(&v);
    return value_bool(1);
}
static Value bh_delattr(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<2) return value_bool(0);
    if(args[0].type!=V_MAP||args[1].type!=V_STRING) return value_bool(0);
    for(size_t i=0;i<args[0].v.map.len;i++){
        if(strcmp(args[0].v.map.keys[i], args[1].v.s)==0){
            free(args[0].v.map.keys[i]);
            value_free(args[0].v.map.vals[i]);
            free(args[0].v.map.vals[i]);
            for(size_t j=i+1;j<args[0].v.map.len;j++){
                args[0].v.map.keys[j-1]=args[0].v.map.keys[j];
                args[0].v.map.vals[j-1]=args[0].v.map.vals[j];
            }
            args[0].v.map.len--;
            return value_bool(1);
        }
    }
    return value_bool(0);
}
static Value bh_id(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_int(0);
    const void* p = (const void*)&args[0];
    unsigned long long x = (unsigned long long)(uintptr_t)p;
    return value_int((long long)x);
}
static Value bh_isinstance(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<2) return value_bool(0);
    if(args[1].type==V_STRING){
        char* t = args[1].v.s;
        Value typ = value_string(t);
        Value tt = bh_type_of(NULL,&args[0],1);
        int r = 0;
        if(tt.type==V_STRING && strcmp(tt.v.s,t)==0) r=1;
        value_free(&tt);
        value_free(&typ);
        return value_bool(r);
    }
    return value_bool(0);
}

static Value bh_run_binary(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc < 1 || args[0].type != V_STRING) return value_null();

    const char* hex = args[0].v.s;
    size_t len = strlen(hex) / 2;
    if(len == 0) return value_null();

    uint8_t* code = mmap(NULL, len, PROT_READ | PROT_WRITE | PROT_EXEC,
                          MAP_ANON | MAP_PRIVATE, -1, 0);
    if(code == MAP_FAILED) return value_null();
    
    for(size_t i = 0; i < len; i++){
        sscanf(hex + 2*i, "%2hhx", &code[i]);
    }
    
    void (*func)(void) = (void(*)(void))code;
    func();

    munmap(code, len);
    return value_null();
}

void register_builtins(Env* e){
    env_set(e,"input", value_native(bh_input, "input"));
    env_set(e,"sh", value_native(bh_sh, "sh"));
    env_set(e,"input_int", value_native(bh_input, "input_int"));
    Value m = value_map();
    m.v.map.len = 3;
    m.v.map.cap = 3;
    m.v.map.keys = malloc(sizeof(char*) * 3);
    m.v.map.vals = malloc(sizeof(Value*) * 3);
    m.v.map.keys[0] = dh_strdup("sh");
    Value* p0 = malloc(sizeof(Value));
    *p0 = value_native(bh_sh, "sh");
    m.v.map.vals[0] = p0;
    m.v.map.keys[1] = dh_strdup("echo");
    Value* p1 = malloc(sizeof(Value));
    *p1 = value_native(bh_echo, "echo");
    m.v.map.vals[1] = p1;
    m.v.map.keys[2] = dh_strdup("call");
    Value* p2 = malloc(sizeof(Value));
    *p2 = value_native(bh_os_call, "call");
    m.v.map.vals[2] = p2;
    env_set(e, "os", m);
    env_set(e,"say",value_native(bh_say,"say"));
    env_set(e,"print",value_native(bh_print,"print"));
    env_set(e,"len",value_native(bh_len,"len"));
    env_set(e,"to_string",value_native(bh_to_string,"to_string"));
    env_set(e,"to_int",value_native(bh_to_int,"to_int"));
    env_set(e,"to_float",value_native(bh_to_float,"to_float"));
    env_set(e,"type_of",value_native(bh_type_of,"type_of"));
    env_set(e,"abs",value_native(bh_abs,"abs"));
    env_set(e,"pow",value_native(bh_powf,"pow"));
    env_set(e,"sqrt",value_native(bh_sqrtf,"sqrt"));
    env_set(e,"sin",value_native(bh_sinf,"sin"));
    env_set(e,"cos",value_native(bh_cosf,"cos"));
    env_set(e,"tan",value_native(bh_tanf,"tan"));
    env_set(e,"floor",value_native(bh_floorf,"floor"));
    env_set(e,"ceil",value_native(bh_ceilf,"ceil"));
    env_set(e,"rand",value_native(bh_randn,"rand"));
    env_set(e,"srand",value_native(bh_srandn,"srand"));
    env_set(e,"range",value_native(bh_range,"range"));
    env_set(e,"push",value_native(bh_push,"push"));
    env_set(e,"pop",value_native(bh_pop,"pop"));
    env_set(e,"shift",value_native(bh_shift,"shift"));
    env_set(e,"unshift",value_native(bh_unshift,"unshift"));
    env_set(e,"map",value_native(bh_mapf,"map"));
    env_set(e,"filter",value_native(bh_filterf,"filter"));
    env_set(e,"reduce",value_native(bh_reducef,"reduce"));
    env_set(e,"read_file",value_native(bh_read_file,"read_file"));
    env_set(e,"write_file",value_native(bh_write_file,"write_file"));
    env_set(e,"file_exists",value_native(bh_file_exists,"file_exists"));
    env_set(e,"sleep_ms",value_native(bh_sleep_ms,"sleep_ms"));
    env_set(e,"time_unix",value_native(bh_time_unix,"time_unix"));
    env_set(e,"now",value_native(bh_now_str,"now"));
    env_set(e,"getenv",value_native(bh_getenvv,"getenv"));
    env_set(e,"setenv",value_native(bh_setenvv,"setenv"));
    env_set(e,"exit",value_native(bh_exitv,"exit"));
    env_set(e,"assert",value_native(bh_assertv,"assert"));
    env_set(e,"panic",value_native(bh_panicv,"panic"));
    env_set(e,"spawn",value_native(bh_spawnv,"spawn"));
    env_set(e,"eval",value_native(bh_evalv,"eval"));
    env_set(e,"keys",value_native(bh_keys,"keys"));
    env_set(e,"values",value_native(bh_values,"values"));
    env_set(e,"random", value_native(bh_random, "random"));
    env_set(e,"int", value_native(bh_int_cast,"int"));
    env_set(e,"float", value_native(bh_float_cast,"float"));
    env_set(e,"str", value_native(bh_str_cast,"str"));
    env_set(e,"bool", value_native(bh_bool_cast,"bool"));
    env_set(e,"list", value_native(bh_list_cast,"list"));
    env_set(e,"tuple", value_native(bh_tuple_cast,"tuple"));
    env_set(e,"dict", value_native(bh_dict_cast,"dict"));
    env_set(e,"chr", value_native(bh_chr,"chr"));
    env_set(e,"ord", value_native(bh_ord,"ord"));
    env_set(e,"hex", value_native(bh_hex,"hex"));
    env_set(e,"oct", value_native(bh_oct,"oct"));
    env_set(e,"bin", value_native(bh_bin,"bin"));
    env_set(e,"repr", value_native(bh_repr,"repr"));
    env_set(e,"ascii", value_native(bh_ascii,"ascii"));
    env_set(e,"format", value_native(bh_format,"format"));
    env_set(e,"divmod", value_native(bh_divmod,"divmod"));
    env_set(e,"sum", value_native(bh_sum,"sum"));
    env_set(e,"min", value_native(bh_min,"min"));
    env_set(e,"max", value_native(bh_max,"max"));
    env_set(e,"all", value_native(bh_all,"all"));
    env_set(e,"any", value_native(bh_any,"any"));
    env_set(e,"enumerate", value_native(bh_enumerate,"enumerate"));
    env_set(e,"zip", value_native(bh_zip,"zip"));
    env_set(e,"reversed", value_native(bh_reversed,"reversed"));
    env_set(e,"sorted", value_native(bh_sorted,"sorted"));
    env_set(e,"callable", value_native(bh_callable,"callable"));
    env_set(e,"dir", value_native(bh_dir,"dir"));
    env_set(e,"hasattr", value_native(bh_hasattr,"hasattr"));
    env_set(e,"getattr", value_native(bh_getattr,"getattr"));
    env_set(e,"setattr", value_native(bh_setattr,"setattr"));
    env_set(e,"delattr", value_native(bh_delattr,"delattr"));
    env_set(e,"id", value_native(bh_id,"id"));
    env_set(e,"isinstance", value_native(bh_isinstance,"isinstance"));
    env_set(e, "code", value_native(bh_run_binary, "code"));
    Value ansi = value_map();
    ansi.v.map.len = 8;
    ansi.v.map.cap = 8;
    ansi.v.map.keys = malloc(sizeof(char*) * 8);
    ansi.v.map.vals = malloc(sizeof(Value*) * 8);
    ansi.v.map.keys[0] = dh_strdup("reset");
    Value* a0 = malloc(sizeof(Value));
    *a0 = value_string("\x1b[0m");
    ansi.v.map.vals[0] = a0;
    ansi.v.map.keys[1] = dh_strdup("red");
    Value* a1 = malloc(sizeof(Value));
    *a1 = value_string("\x1b[31m");
    ansi.v.map.vals[1] = a1;
    ansi.v.map.keys[2] = dh_strdup("green");
    Value* a2 = malloc(sizeof(Value));
    *a2 = value_string("\x1b[32m");
    ansi.v.map.vals[2] = a2;
    ansi.v.map.keys[3] = dh_strdup("yellow");
    Value* a3 = malloc(sizeof(Value));
    *a3 = value_string("\x1b[33m");
    ansi.v.map.vals[3] = a3;
    ansi.v.map.keys[4] = dh_strdup("blue");
    Value* a4 = malloc(sizeof(Value));
    *a4 = value_string("\x1b[34m");
    ansi.v.map.vals[4] = a4;
    ansi.v.map.keys[5] = dh_strdup("magenta");
    Value* a5 = malloc(sizeof(Value));
    *a5 = value_string("\x1b[35m");
    ansi.v.map.vals[5] = a5;
    ansi.v.map.keys[6] = dh_strdup("cyan");
    Value* a6 = malloc(sizeof(Value));
    *a6 = value_string("\x1b[36m");
    ansi.v.map.vals[6] = a6;
    ansi.v.map.keys[7] = dh_strdup("bold");
    Value* a7 = malloc(sizeof(Value));
    *a7 = value_string("\x1b[1m");
    ansi.v.map.vals[7] = a7;
    env_set(e, "ansi", ansi);
}