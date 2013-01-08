#include <stdio.h>
#include <sys/types.h>
#include "bbs.h"

void
ensure(size_t sz1, size_t sz2, const char *name, const char *reason) {
    if (sz1 == sz2) {
        printf("sizeof(%s): %zu (OK)\n", name, sz1);
        return;
    }
    printf("%s: size unmatch (expected: %zu, actual: %zu).\n",
           name, sz1, sz2);
    if (reason && *reason)
        printf(" *** %s\n", reason);
    exit(1);
}

void
check(size_t sz, const char *name) {
    printf("sizeof(%s): %zu\n", name, sz);
}

#define ENSURE(x, y) ensure(sizeof(x), y, #x, "")
#define ENSURE3(x, y, reason) ensure(sizeof(x), y, #x, reason)
#define CHECK(x) check(sizeof(x), #x)

int main()
{
    // System type length.
    CHECK(size_t);
    CHECK(size_t);
    CHECK(off_t);
    CHECK(int);
    CHECK(long);
    CHECK(time_t);

    // Per-site data length
    CHECK(userinfo_t);
    CHECK(msgque_t);
    CHECK(SHM_t);
    printf("SHMSIZE = %lu\n", SHMSIZE);

    // Data types that need to be checked.
    ENSURE3(time4_t, 4, "Please define TIMET64 in your pttbbs.conf.");
    ENSURE(userec_t, 512);
    ENSURE(fileheader_t, 128);
    ENSURE(boardheader_t, 256);
    ENSURE(chicken_t, 128);
    return 0;
}
