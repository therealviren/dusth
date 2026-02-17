#ifndef DUSTH_UTILS_H
#define DUSTH_UTILS_H

#include <stddef.h>
#include "value.h"

char* dh_strdup(const char* s);
char* dh_strndup(const char* s, size_t n);
char* dh_concat(const char* a, const char* b);
char* dh_from_double(double v);
char* dh_from_int(long long v);
char* dh_from_int_hex(long long v);
char* dh_from_int_oct(long long v);
char* dh_now_iso(void);
char* read_file_to_string(const char* path);
int write_string_to_file(const char* path, const char* content);
void map_grow(Value* map);

#endif