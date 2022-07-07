#pragma once

#ifdef BAZELTEST
#include <stdio.h>

ssize_t mock_getrandom(void *buf, size_t buflen, unsigned int flags __attribute__((unused)));
#define getrandom mock_getrandom

#endif //BAZELTEST
