#define _UTIL_C_
#include "bbs.h"
#include "common.h"

static int
handle_fix_2026h(const char *path, const struct stat *st)
{
    if (st->st_size < 7)
        return 0;

    uint8_t *buf = NULL;
    size_t len = 0;
    if (read_file_all(path, (size_t)st->st_size, &buf, &len) < 0)
        return -1;

    if (!has_2026_seq(buf, len)) {
        free(buf);
        return 0;
    }

    size_t out_len = strip_2026_seq(buf, len, buf);
    int rc = atomic_write_file(path, st->st_mode, buf, out_len);
    free(buf);
    return rc;
}

int
main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file | .DIR> ...\n", argv[0]);
        return 1;
    }

    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (process_file_or_dir(argv[i], handle_fix_2026h) < 0)
            rc = 1;
    }
    return rc;
}
