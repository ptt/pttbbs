#ifndef INCLUDE_UTIL_H
#define INCLUDE_UTIL_H
int passwd_mmap(void);
int passwd_apply(int (*fptr)(int, userec_t *));

#endif // INCLUDE_UTIL_H
