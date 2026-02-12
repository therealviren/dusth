#ifndef DUSTH_ENV_H
#define DUSTH_ENV_H

#include <stddef.h>
#include "value.h"

typedef struct Env Env;

Env* env_new(Env* parent);
void env_free(Env* e);

int env_set(Env* e, const char* name, Value v);
int env_get(Env* e, const char* name, Value* out);

Env* env_clone_recursive(Env* e);

#endif