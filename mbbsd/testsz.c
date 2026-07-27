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
    ENSURE(time4_t, 4);
    ENSURE(time8_t, 8);
    ENSURE(userec_t, 512);
    ENSURE(fileheader_t, 128);
    ENSURE(boardheader_t, 256);
    ENSURE(chicken_t, 128);

    // Y2038 overflow verification test (Year 2040 & Year 2100)
    time4_t t2040 = (time4_t)2208988800U; // 2040-01-01 00:00:00 UTC
    struct tm *tm2040 = localtime4(&t2040);
    if (!tm2040 || tm2040->tm_year != 140) {
        fprintf(stderr, "Y2038 verification failed for Year 2040!\n");
        return 1;
    }
    printf("Y2038 Year 2040 test passed: %s\n", Cdate(&t2040));

    time4_t t2100 = (time4_t)4102444800U; // 2100-01-01 00:00:00 UTC
    struct tm *tm2100 = localtime4(&t2100);
    if (!tm2100 || tm2100->tm_year != 200) {
        fprintf(stderr, "Y2038 verification failed for Year 2100!\n");
        return 1;
    }
    printf("Y2038 Year 2100 test passed: %s\n", Cdate(&t2100));

    return 0;
}
