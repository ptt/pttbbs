#include <stdio.h>
#include <sys/types.h>
#include "bbs.h"

int main()
{
    printf("sizeof(size_t)     = %lu\n", sizeof(size_t));
    printf("sizeof(off_t)      = %lu\n", sizeof(off_t));
    printf("sizeof(int)        = %lu\n", sizeof(int));
    printf("sizeof(long)       = %lu\n", sizeof(long));
    printf("sizeof(time_t)     = %lu\n", sizeof(time_t));
    printf("sizeof(time4_t)    = %lu %s\n", sizeof(time4_t), sizeof(time4_t) == 4 ? "" : "ERROR!!!!!");
    printf("sizeof(userec_t)   = %lu\n", sizeof(userec_t));
    printf("sizeof(fileheader_t)   = %lu\n", sizeof(fileheader_t));
    printf("sizeof(boardheader_t)  = %lu\n", sizeof(boardheader_t));
    printf("sizeof(chicken_t)  = %lu\n", sizeof(chicken_t));
    printf("sizeof(userinfo_t) = %lu\n", sizeof(userinfo_t));
    printf("sizeof(msgque_t)   = %lu\n", sizeof(msgque_t));
    printf("sizeof(SHM_t)      = %lu\n", sizeof(SHM_t));
    return 0;
}
