#define _GNU_SOURCE
#define _UTIL_C_
#include "bbs.h"
#include "common.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

int
buf_append(bytebuf_t *b, const void *src, size_t n)
{
    if (n == 0)
        return 0;
    if (b->len + n > b->cap) {
        size_t new_cap = b->cap ? b->cap * 2 : 4096;
        while (new_cap < b->len + n)
            new_cap *= 2;
        uint8_t *p = realloc(b->data, new_cap);
        if (!p)
            return -1;
        b->data = p;
        b->cap = new_cap;
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
    return 0;
}

int
has_2026_seq(const uint8_t *buf, size_t len)
{
    const uint8_t *p = buf;
    size_t rem = len;

    while (rem >= 7) {
        const uint8_t *hit = memmem(p, rem, "[?2026", 6);
        if (!hit)
            return 0;
        size_t off = (size_t)(hit - p);
        if (off + 6 < rem && (hit[6] == 'h' || hit[6] == 'l'))
            return 1;
        p = hit + 6;
        rem = len - (size_t)(p - buf);
    }
    return 0;
}

size_t
strip_2026_seq(const uint8_t *src, size_t len, uint8_t *dst)
{
    size_t out_len = 0;
    size_t i = 0;

    while (i < len) {
        if (src[i] == 0x1b && i + 7 < len &&
            memcmp(src + i + 1, "[?2026", 6) == 0 &&
            (src[i + 7] == 'h' || src[i + 7] == 'l')) {
            i += 8;
            continue;
        }
        if (src[i] == '[' && i + 6 < len &&
            memcmp(src + i, "[?2026", 6) == 0 &&
            (src[i + 6] == 'h' || src[i + 6] == 'l')) {
            i += 7;
            continue;
        }
        dst[out_len++] = src[i++];
    }

    return out_len;
}

int
is_valid_utf8(const uint8_t *buf, size_t len)
{
    size_t i = 0;
    while (i < len) {
        uint8_t c = buf[i];
        if (c < 0x80) {
            i++;
        } else if (c >= 0xC2 && c <= 0xDF) {
            if (i + 1 >= len || (buf[i + 1] & 0xC0) != 0x80)
                return 0;
            i += 2;
        } else if (c >= 0xE0 && c <= 0xEF) {
            if (i + 2 >= len ||
                (buf[i + 1] & 0xC0) != 0x80 ||
                (buf[i + 2] & 0xC0) != 0x80)
                return 0;
            if (c == 0xE0 && buf[i + 1] < 0xA0)
                return 0;
            if (c == 0xED && buf[i + 1] >= 0xA0)
                return 0;
            i += 3;
        } else if (c >= 0xF0 && c <= 0xF4) {
            if (i + 3 >= len ||
                (buf[i + 1] & 0xC0) != 0x80 ||
                (buf[i + 2] & 0xC0) != 0x80 ||
                (buf[i + 3] & 0xC0) != 0x80)
                return 0;
            if (c == 0xF0 && buf[i + 1] < 0x90)
                return 0;
            if (c == 0xF4 && buf[i + 1] >= 0x90)
                return 0;
            i += 4;
        } else {
            return 0;
        }
    }
    return 1;
}

static int
write_all(int fd, const uint8_t *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, buf + off, len - off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

int
read_file_all(const char *path, size_t expected_size,
              uint8_t **out_buf, size_t *out_len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    uint8_t *buf = malloc(expected_size ? expected_size : 1);
    if (!buf) {
        close(fd);
        return -1;
    }

    size_t off = 0;
    while (off < expected_size) {
        ssize_t r = read(fd, buf + off, expected_size - off);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            free(buf);
            close(fd);
            return -1;
        }
        if (r == 0)
            break;
        off += (size_t)r;
    }
    close(fd);

    *out_buf = buf;
    *out_len = off;
    return 0;
}

int
atomic_write_file(const char *path, mode_t mode,
                  const uint8_t *buf, size_t len)
{
    char tmppath[PATH_MAX];
    int n = snprintf(tmppath, sizeof(tmppath), "%s.tmp.XXXXXX", path);
    if (n < 0 || (size_t)n >= sizeof(tmppath))
        return -1;

    int fd = mkstemp(tmppath);
    if (fd < 0)
        return -1;

    if (fchmod(fd, mode & 0777) < 0 ||
        write_all(fd, buf, len) < 0 ||
        close(fd) < 0) {
        close(fd);
        unlink(tmppath);
        return -1;
    }

    if (rename(tmppath, path) < 0) {
        unlink(tmppath);
        return -1;
    }

    return 0;
}

static int
is_dir_index_path(const char *path)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    return (strcmp(base, ".DIR") == 0 || strncmp(base, ".DIR.", 5) == 0);
}

static int
process_dir_file(const char *dirpath_file, upgrade_file_handler_fn handler)
{
    int fd = open(dirpath_file, O_RDONLY);
    if (fd < 0) {
        perror(dirpath_file);
        return -1;
    }

    char basedir[PATH_MAX];
    const char *slash = strrchr(dirpath_file, '/');
    if (slash) {
        size_t dlen = (size_t)(slash - dirpath_file);
        if (dlen == 0)
            dlen = 1;
        if (dlen >= sizeof(basedir)) {
            close(fd);
            return -1;
        }
        memcpy(basedir, dirpath_file, dlen);
        basedir[dlen] = '\0';
    } else {
        strcpy(basedir, ".");
    }

    fileheader_t fh;
    while (read(fd, &fh, sizeof(fh)) == (ssize_t)sizeof(fh)) {
        char fn[FNLEN + 1];
        memcpy(fn, fh.filename, FNLEN);
        fn[FNLEN] = '\0';

        if (fn[0] == '\0' || strcmp(fn, ".") == 0 || strcmp(fn, "..") == 0)
            continue;

        char fullpath[PATH_MAX];
        int n = snprintf(fullpath, sizeof(fullpath), "%s/%s", basedir, fn);
        if (n < 0 || (size_t)n >= sizeof(fullpath))
            continue;

        struct stat st;
        if (stat(fullpath, &st) < 0)
            continue;

        if (S_ISREG(st.st_mode) && access(fullpath, R_OK | W_OK) == 0)
            handler(fullpath, &st);
    }

    close(fd);
    return 0;
}

int
process_file_or_dir(const char *path, upgrade_file_handler_fn handler)
{
    struct stat st;
    if (stat(path, &st) < 0) {
        perror(path);
        return -1;
    }

    if (!S_ISREG(st.st_mode))
        return 0;

    if (is_dir_index_path(path))
        return process_dir_file(path, handler);

    if (access(path, R_OK | W_OK) < 0)
        return -1;

    return handler(path, &st);
}
