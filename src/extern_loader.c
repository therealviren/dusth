#include "extern_loader.h"
#include "utils.h"
#include "parser.h"
#include "interpreter.h"
#include <stdlib.h>
int load_external_file_into_env(const char* path, Env* env){
    char* src=read_file_to_string(path);
    if(!src) return 0;
    Node* p=parse_program(src);
    free(src);
    if(!p) return 0;
    execute_program(p,env);
    free_node(p);
    return 1;
}