#include "../include/bbs.h"

#define PORT 2003

#ifdef STRLEN
    #undef STRLEN
#endif
#define STRLEN 128

#define ERROR	0x1
#define SUCCESS	0x2
#define TIMEOUT	0x4

typedef int response;

typedef struct request{
    int money;
    char userid[IDLEN];
}request;
