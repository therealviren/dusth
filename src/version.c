#include "version.h"
#include <stdio.h>
#include <stdlib.h>
static const char* ver = "1.3.2";
static const char* build_info = __DATE__ " " __TIME__ " " __VERSION__;
const char* version_string(void){ return ver; }
const char* version_build(void){ return build_info; }