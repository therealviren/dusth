#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "env.h"
#include "parser.h"

int execute_file(const char* path, Env* env);
int execute_program(Node* program, Env* env);
int interpret_file(const char* path, Env* env);
Env* global_env(void);

#endif