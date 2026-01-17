#ifndef DUSTH_INTERPRETER_H
#define DUSTH_INTERPRETER_H
#include "parser.h"
#include "value.h"
Env* global_env();
void register_builtins(Env* e);
int execute_program(Node* program, Env* env);
int execute_file(const char* path, Env* env);
#endif