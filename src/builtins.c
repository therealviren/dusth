#include "builtins.h"
#include "utils.h"
#include "parser.h"
#include "env.h"
#include "interpreter.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

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
    return value_int(0);
}
static Value bh_to_float(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_float(0.0);
    if(args[0].type==V_FLOAT) return value_float(args[0].v.f);
    if(args[0].type==V_INT) return value_float((double)args[0].v.i);
    if(args[0].type==V_STRING) return value_float(atof(args[0].v.s));
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
static Value bh_pown(Env* env, Value* args, size_t argc){
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
    Value L=value_list();
    for(long long i=a;i<b;i++){
        Value it=value_int(i);
        Value* p=malloc(sizeof(Value));
        *p=it;
        Value** arr=realloc(L.v.list.items,sizeof(Value*)*(L.v.list.len+1));
        L.v.list.items=arr;
        L.v.list.items[L.v.list.len++]=p;
    }
    return L;
}
static Value bh_push(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<2) return value_null();
    if(args[0].type!=V_LIST) return value_null();
    Value val=value_clone(&args[1]);
    Value* p=malloc(sizeof(Value));
    *p=val;
    Value** arr=realloc(args[0].v.list.items,sizeof(Value*)*(args[0].v.list.len+1));
    args[0].v.list.items=arr;
    args[0].v.list.items[args[0].v.list.len++]=p;
    return value_int((long long)args[0].v.list.len);
}
static Value bh_pop(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_null();
    if(args[0].type!=V_LIST) return value_null();
    if(args[0].v.list.len==0) return value_null();
    Value* it=args[0].v.list.items[args[0].v.list.len-1];
    Value out=value_clone(it);
    value_free(it);
    free(it);
    args[0].v.list.len--;
    return out;
}
static Value bh_shift(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<1) return value_null();
    if(args[0].type!=V_LIST) return value_null();
    if(args[0].v.list.len==0) return value_null();
    Value* it=args[0].v.list.items[0];
    Value out=value_clone(it);
    value_free(it);
    free(it);
    for(size_t i=1;i<args[0].v.list.len;i++) args[0].v.list.items[i-1]=args[0].v.list.items[i];
    args[0].v.list.len--;
    return out;
}
static Value bh_unshift(Env* env, Value* args, size_t argc){
    (void)env;
    if(argc<2) return value_null();
    if(args[0].type!=V_LIST) return value_null();
    Value* p=malloc(sizeof(Value));
    *p=value_clone(&args[1]);
    args[0].v.list.items=realloc(args[0].v.list.items,sizeof(Value*)*(args[0].v.list.len+1));
    for(size_t i=args[0].v.list.len;i>0;i--) args[0].v.list.items[i]=args[0].v.list.items[i-1];
    args[0].v.list.items[0]=p;
    args[0].v.list.len++;
    return value_int((long long)args[0].v.list.len);
}
static Value bh_mapf(Env* env, Value* args, size_t argc){
    if(argc<2) return value_list();
    if(args[0].type!=V_LIST) return value_list();
    if(args[1].type!=V_NATIVE) return value_list();
    NativeFn fn=args[1].v.native.fn;
    Value L=value_list();
    for(size_t i=0;i<args[0].v.list.len;i++){
        Value item=value_clone(args[0].v.list.items[i]);
        Value callarg;
        callarg = value_clone(&item);
        Value res = fn(env,&callarg,1);
        value_free(&callarg);
        Value* p=malloc(sizeof(Value));
        *p=value_clone(&res);
        Value** arr=realloc(L.v.list.items,sizeof(Value*)*(L.v.list.len+1));
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
            *p=value_clone(&item);
            Value** arr=realloc(L.v.list.items,sizeof(Value*)*(L.v.list.len+1));
            L.v.list.items=arr;
            L.v.list.items[L.v.list.len++]=p;
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
        *p=ks;
        Value** arr=realloc(L.v.list.items,sizeof(Value*)*(L.v.list.len+1));
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
        *p=vs;
        Value** arr=realloc(L.v.list.items,sizeof(Value*)*(L.v.list.len+1));
        L.v.list.items=arr;
        L.v.list.items[L.v.list.len++]=p;
    }
    return L;
}
void register_builtins(Env* e){
    env_set(e,"say",value_native(bh_say,"say"));
    env_set(e,"print",value_native(bh_print,"print"));
    env_set(e,"len",value_native(bh_len,"len"));
    env_set(e,"to_string",value_native(bh_to_string,"to_string"));
    env_set(e,"to_int",value_native(bh_to_int,"to_int"));
    env_set(e,"to_float",value_native(bh_to_float,"to_float"));
    env_set(e,"type_of",value_native(bh_type_of,"type_of"));
    env_set(e,"abs",value_native(bh_abs,"abs"));
    env_set(e,"pow",value_native(bh_pown,"pow"));
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
}