#define _UTIL_C_
#include "bbs.h"
#include "sgr66.h"

static int
handle_convert_sgr66(const char *path, const struct stat *st)
{
    if (st->st_size <= 0)
        return 0;

    uint8_t *buf = NULL;
    size_t len = 0;
    if (read_file_all(path, (size_t)st->st_size, &buf, &len) < 0)
        return -1;

    int has_dbcs = 0, has_split = 0;
    if (!scan_big5_uao(buf, len, &has_dbcs, &has_split) || !has_split) {
        free(buf);
        return 0;
    }

    bytebuf_t out = { NULL, 0, len + 64 };
    out.data = malloc(out.cap);
    if (!out.data) {
        free(buf);
        return -1;
    }

    if (convert_big5_sgr66(buf, len, 0, &out) < 0) {
        free(out.data);
        free(buf);
        return -1;
    }

    int rc = atomic_write_file(path, st->st_mode, out.data, out.len);
    free(out.data);
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
        if (process_file_or_dir(argv[i], handle_convert_sgr66) < 0)
            rc = 1;
    }
    return rc;
}
