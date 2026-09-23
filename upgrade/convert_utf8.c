#define _GNU_SOURCE
#define _UTIL_C_
#include "bbs.h"
#include "common.h"
#include "sgr66.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern const uint16_t b2u_table[];
int max_printed_col = 0;

static int
write_all_fd(int fd, const uint8_t *buf, size_t len)
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

static int
mkdir_p(const char *path, mode_t mode)
{
    char tmp[PATH_MAX];
    int n = snprintf(tmp, sizeof(tmp), "%s", path);
    if (n < 0 || (size_t)n >= sizeof(tmp))
        return -1;

    size_t len = (size_t)n;
    while (len > 1 && tmp[len - 1] == '/')
        tmp[--len] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) < 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) < 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int
ensure_parent_dir(const char *filepath)
{
    char parent[PATH_MAX];
    const char *slash = strrchr(filepath, '/');
    if (!slash)
        return 0;
    size_t dlen = (size_t)(slash - filepath);
    if (dlen == 0)
        return 0;
    if (dlen >= sizeof(parent))
        return -1;
    memcpy(parent, filepath, dlen);
    parent[dlen] = '\0';
    return mkdir_p(parent, 0755);
}

/*
 * Writes buf to path (O_CREAT | O_TRUNC).
 * Returns 0 on success, -1 on error.
 */
static int
write_file(const char *path, mode_t mode,
           const uint8_t *buf, size_t len)
{
    if (ensure_parent_dir(path) < 0)
        return -1;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode & 0777);
    if (fd < 0) {
        perror(path);
        return -1;
    }

    if (write_all_fd(fd, buf, len) < 0) {
        perror(path);
        close(fd);
        unlink(path);
        return -1;
    }

    if (close(fd) < 0) {
        perror(path);
        unlink(path);
        return -1;
    }
    return 0;
}

static void
get_dirname(const char *path, char *out, size_t out_sz)
{
    const char *slash = strrchr(path, '/');
    if (!slash) {
        strlcpy(out, ".", out_sz);
        return;
    }
    size_t dlen = (size_t)(slash - path);
    if (dlen == 0) {
        strlcpy(out, "/", out_sz);
        return;
    }
    if (dlen >= out_sz)
        dlen = out_sz - 1;
    memcpy(out, path, dlen);
    out[dlen] = '\0';
}

static const char *
get_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int
is_dir_index_name(const char *name)
{
    return strcmp(name, ".DIR") == 0 || strncmp(name, ".DIR.", 5) == 0;
}

static int
is_brd_name(const char *name)
{
    return strcmp(name, FN_BOARD) == 0 || strcmp(name, ".BRD") == 0;
}

static int
is_passwd_name(const char *name)
{
    return strcmp(name, FN_PASSWD) == 0 ||
           strcmp(name, ".PASSWD") == 0 ||
           strcmp(name, ".PASSWDS") == 0;
}

static int
is_safe_rel_name(const char *name)
{
    if (!name || name[0] == '\0')
        return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 0;
    if (strchr(name, '/') != NULL)
        return 0;
    return 1;
}

static void
conv_field_b2u(char *field, size_t sz)
{
    if (sz == 0)
        return;
    size_t len = strnlen(field, sz);
    if (len == 0)
        return;
    if (!is_valid_utf8((const uint8_t *)field, len)) {
        char out[256];
        size_t max_out = sz < sizeof(out) ? sz : sizeof(out);
        size_t out_idx = 0;
        size_t i = 0;
        utf8_ctx ctx;

        while (i < len && out_idx + 1 < max_out) {
            uint8_t hi = (uint8_t)field[i];
            if (hi < 0x80) {
                out[out_idx++] = (char)hi;
                i++;
                continue;
            }
            if (i + 1 < len) {
                uint8_t lo = (uint8_t)field[i + 1];
                uint16_t b5 = ((uint16_t)hi << 8) | (uint16_t)lo;
                if (b2u_table[b5] != b5) {
                    int ulen = utf8_from_ucs(&ctx, b2u_table[b5]);
                    if (out_idx + (size_t)ulen >= max_out)
                        break;
                    out_idx += (size_t)utf8_to_mb(&ctx, out + out_idx);
                    i += 2;
                    continue;
                }
            }
            out[out_idx++] = '?';
            i++;
        }
        out[out_idx] = '\0';
        memcpy(field, out, out_idx + 1);
    }
    size_t new_len = strnlen(field, sz);
    if (new_len < sz)
        memset(field + new_len, 0, sz - new_len);
}

static void
convert_fileheader(fileheader_t *fh)
{
    conv_field_b2u(fh->owner, sizeof(fh->owner));
    conv_field_b2u(fh->date, sizeof(fh->date));
    conv_field_b2u(fh->title, sizeof(fh->title));
}

static void
convert_boardheader(boardheader_t *bh)
{
    conv_field_b2u(bh->title, sizeof(bh->title));
    conv_field_b2u(bh->BM, sizeof(bh->BM));
    conv_field_b2u(bh->posttype, sizeof(bh->posttype));
}

static void
convert_userec(userec_t *u)
{
    conv_field_b2u(u->realname, sizeof(u->realname));
    conv_field_b2u(u->nickname, sizeof(u->nickname));
    conv_field_b2u(u->address, sizeof(u->address));
    conv_field_b2u(u->career, sizeof(u->career));
    conv_field_b2u(u->justify, sizeof(u->justify));
}

static size_t
parse_sgr_seq(const uint8_t *buf, size_t len, size_t pos)
{
    if (pos + 2 >= len || buf[pos] != 0x1b || buf[pos + 1] != '[')
        return 0;
    size_t k = pos + 2;
    while (k < len && ((buf[k] >= '0' && buf[k] <= '9') || buf[k] == ';'))
        k++;
    if (k < len && buf[k] == 'm')
        return (k - pos) + 1;
    return 0;
}

static int
convert_big5_sgr66_tolerant(const uint8_t *buf, size_t len, bytebuf_t *out)
{
    int prev_was_sgr = 0;
    size_t i = 0;

    while (i < len) {
        size_t sgr_len = parse_sgr_seq(buf, len, i);
        if (sgr_len > 0) {
            if (buf_append(out, buf + i, sgr_len) < 0)
                return -1;
            prev_was_sgr = 1;
            i += sgr_len;
            continue;
        }

        if (buf[i] < 0x80) {
            if (buf_append(out, buf + i, 1) < 0)
                return -1;
            prev_was_sgr = 0;
            i++;
            continue;
        }

        uint8_t hi = buf[i];
        size_t j = i + 1;
        size_t sgr_count = 0;
        while (j < len) {
            size_t mid_sgr_len = parse_sgr_seq(buf, len, j);
            if (mid_sgr_len == 0)
                break;
            j += mid_sgr_len;
            sgr_count++;
        }

        if (j >= len) {
            if (buf_append(out, "?", 1) < 0)
                return -1;
            prev_was_sgr = 0;
            i++;
            continue;
        }

        uint8_t lo = buf[j];
        uint16_t b5 = ((uint16_t)hi << 8) | (uint16_t)lo;
        if (b2u_table[b5] == b5) {
            if (buf_append(out, "?", 1) < 0)
                return -1;
            prev_was_sgr = 0;
            i++;
            continue;
        }

        if (sgr_count > 0) {
            if (prev_was_sgr && out->len > 0 && out->data[out->len - 1] == 'm') {
                out->len--;
                if (buf_append(out, ";66", 3) < 0)
                    return -1;
            } else {
                if (buf_append(out, "\x1b[66", 4) < 0)
                    return -1;
            }

            size_t k = i + 1;
            while (k < j) {
                size_t mid_sgr_len = parse_sgr_seq(buf, len, k);
                if (buf_append(out, ";", 1) < 0)
                    return -1;
                if (buf_append(out, buf + k + 2, mid_sgr_len - 3) < 0)
                    return -1;
                k += mid_sgr_len;
            }

            if (buf_append(out, "m", 1) < 0)
                return -1;
        }

        utf8_ctx ctx;
        int ulen = utf8_from_ucs(&ctx, b2u_table[b5]);
        if (buf_append(out, ctx.buf, (size_t)ulen) < 0)
            return -1;

        prev_was_sgr = 0;
        i = j + 1;
    }

    return 0;
}

#define PROGRESS_PREFIX "Converting .DIR: "

static int
write_output(const char *dst_path, mode_t mode,
             const uint8_t *buf, size_t len)
{
    if (!dst_path)
        return write_all_fd(STDOUT_FILENO, buf, len);
    return write_file(dst_path, mode, buf, len);
}

static int
is_dest_dir(const char *dest)
{
    if (!dest || !dest[0])
        return 0;
    size_t len = strlen(dest);
    if (dest[len - 1] == '/')
        return 1;
    struct stat st;
    if (stat(dest, &st) == 0 && S_ISDIR(st.st_mode))
        return 1;
    return 0;
}

static int
resolve_dest_file(const char *src_path, const char *dest,
                  char *dst_file, size_t dst_file_sz,
                  char *dst_folder, size_t dst_folder_sz)
{
    if (!dest) {
        if (dst_file && dst_file_sz > 0)
            dst_file[0] = '\0';
        if (dst_folder && dst_folder_sz > 0)
            dst_folder[0] = '\0';
        return 0;
    }
    if (is_dest_dir(dest)) {
        if (dst_folder)
            strlcpy(dst_folder, dest, dst_folder_sz);
        if (dst_file) {
            size_t dlen = strlen(dest);
            while (dlen > 1 && dest[dlen - 1] == '/')
                dlen--;
            int n = snprintf(dst_file, dst_file_sz, "%.*s/%s",
                             (int)dlen, dest, get_basename(src_path));
            if (n < 0 || (size_t)n >= dst_file_sz)
                return -1;
        }
    } else {
        if (dst_file)
            strlcpy(dst_file, dest, dst_file_sz);
        if (dst_folder)
            get_dirname(dest, dst_folder, dst_folder_sz);
    }
    return 0;
}

static int
convert_content_file_ex(const char *src_path, const char *dst_path, int skip_if_exists)
{
    struct stat st;
    if (stat(src_path, &st) < 0)
        return -1;
    if (!S_ISREG(st.st_mode))
        return 0;

    if (dst_path && skip_if_exists && access(dst_path, F_OK) == 0)
        return 0;

    if (st.st_size <= 0)
        return write_output(dst_path, st.st_mode,
                            (const uint8_t *)"", 0);

    uint8_t *buf = NULL;
    size_t len = 0;
    if (read_file_all(src_path, (size_t)st.st_size, &buf, &len) < 0)
        return -1;

    if (has_2026_seq(buf, len))
        len = strip_2026_seq(buf, len, buf);

    if (is_valid_utf8(buf, len)) {
        int rc = write_output(dst_path, st.st_mode, buf, len);
        free(buf);
        return rc;
    }

    bytebuf_t out = { NULL, 0, len * 3 / 2 + 64 };
    out.data = malloc(out.cap);
    if (!out.data) {
        free(buf);
        return -1;
    }

    int has_dbcs = 0, has_split = 0;
    int conv_rc;
    if (scan_big5_uao(buf, len, &has_dbcs, &has_split) && has_dbcs) {
        conv_rc = convert_big5_sgr66(buf, len, 1, &out);
    } else {
        conv_rc = convert_big5_sgr66_tolerant(buf, len, &out);
    }

    if (conv_rc < 0) {
        free(out.data);
        free(buf);
        return -1;
    }

    int rc = write_output(dst_path, st.st_mode, out.data, out.len);
    free(out.data);
    free(buf);
    return rc;
}

static int
convert_content_file(const char *src_path, const char *dst_path)
{
    return convert_content_file_ex(src_path, dst_path, 1);
}

static int
handle_plain_file(const char *src_file, const char *dest)
{
    if (!dest)
        return convert_content_file_ex(src_file, NULL, 0);

    char dst_file[PATH_MAX];
    if (resolve_dest_file(src_file, dest, dst_file, sizeof(dst_file), NULL, 0) < 0)
        return -1;
    if (strcmp(src_file, dst_file) == 0) {
        fprintf(stderr, "Error: src and dest must not be the same (%s)\n", src_file);
        return -1;
    }
    return convert_content_file_ex(src_file, dst_file, 0);
}

static int convert_directory(const char *src_dir, const char *dst_dir);

static int
convert_dir_index_to(const char *src_dir_file, const char *dst_dir_file,
                     const char *dst_folder)
{
    struct stat st;
    if (stat(src_dir_file, &st) < 0) {
        perror(src_dir_file);
        return -1;
    }

    int fd = open(src_dir_file, O_RDONLY);
    if (fd < 0) {
        perror(src_dir_file);
        return -1;
    }

    if (dst_folder && dst_folder[0]) {
        if (mkdir_p(dst_folder, 0755) < 0) {
            perror(dst_folder);
            close(fd);
            return -1;
        }
    }
    int len = strlen(src_dir_file);
    if (len > max_printed_col)
        max_printed_col = len;
    if (dst_dir_file)
        fprintf(stderr, PROGRESS_PREFIX "%-*s\r", max_printed_col, src_dir_file);

    char src_folder[PATH_MAX];
    get_dirname(src_dir_file, src_folder, sizeof(src_folder));

    bytebuf_t dir_out = { NULL, 0, (size_t)st.st_size > 0 ? (size_t)st.st_size : 128 };
    dir_out.data = malloc(dir_out.cap);
    if (!dir_out.data) {
        close(fd);
        return -1;
    }

    int rc = 0;
    fileheader_t fh;
    while (read(fd, &fh, sizeof(fh)) == (ssize_t)sizeof(fh)) {
        char fn[FNLEN + 1];
        memcpy(fn, fh.filename, FNLEN);
        fn[FNLEN] = '\0';

        convert_fileheader(&fh);
        if (buf_append(&dir_out, &fh, sizeof(fh)) < 0) {
            rc = -1;
            break;
        }

        if (!dst_folder || !dst_folder[0] || !is_safe_rel_name(fn))
            continue;

        char src_item[PATH_MAX];
        char dst_item[PATH_MAX];
        int n1 = snprintf(src_item, sizeof(src_item), "%s/%s", src_folder, fn);
        int n2 = snprintf(dst_item, sizeof(dst_item), "%s/%s", dst_folder, fn);
        if (n1 < 0 || (size_t)n1 >= sizeof(src_item) ||
            n2 < 0 || (size_t)n2 >= sizeof(dst_item))
            continue;

        struct stat item_st;
        if (stat(src_item, &item_st) < 0)
            continue;

        if (S_ISREG(item_st.st_mode)) {
            if (convert_content_file(src_item, dst_item) < 0)
                rc = -1;
        } else if (S_ISDIR(item_st.st_mode)) {
            if (convert_directory(src_item, dst_item) < 0)
                rc = -1;
        }
    }

    close(fd);

    if (rc == 0) {
        if (write_output(dst_dir_file, st.st_mode,
                         dir_out.data, dir_out.len) < 0)
            rc = -1;
    }

    free(dir_out.data);
    return rc;
}

static int
handle_dir_index(const char *src_dir_file, const char *dest)
{
    if (!dest)
        return convert_dir_index_to(src_dir_file, NULL, NULL);

    char dst_dir_file[PATH_MAX];
    char dst_folder[PATH_MAX];
    int dest_dir = is_dest_dir(dest);
    if (resolve_dest_file(src_dir_file, dest,
                          dst_dir_file, sizeof(dst_dir_file),
                          dst_folder, sizeof(dst_folder)) < 0)
        return -1;

    return convert_dir_index_to(src_dir_file, dst_dir_file,
                                dest_dir ? dst_folder : NULL);
}

static int
handle_brd_file(const char *src_brd, const char *dest)
{
    struct stat st;
    if (stat(src_brd, &st) < 0) {
        perror(src_brd);
        return -1;
    }

    int fd = open(src_brd, O_RDONLY);
    if (fd < 0) {
        perror(src_brd);
        return -1;
    }

    char src_root[PATH_MAX];
    get_dirname(src_brd, src_root, sizeof(src_root));

    char dst_root[PATH_MAX] = "";
    char dst_brd[PATH_MAX] = "";
    int has_src_boards = 0;

    if (dest) {
        int dest_dir = is_dest_dir(dest);
        if (resolve_dest_file(src_brd, dest,
                              dst_brd, sizeof(dst_brd),
                              dst_root, sizeof(dst_root)) < 0) {
            close(fd);
            return -1;
        }
        if (dest_dir) {
            char src_boards_dir[PATH_MAX];
            struct stat sbst;
            int nsb = snprintf(src_boards_dir, sizeof(src_boards_dir), "%s/boards", src_root);
            if (nsb > 0 && (size_t)nsb < sizeof(src_boards_dir) &&
                stat(src_boards_dir, &sbst) == 0 && S_ISDIR(sbst.st_mode)) {
                has_src_boards = 1;
                char dst_boards_dir[PATH_MAX];
                int nb = snprintf(dst_boards_dir, sizeof(dst_boards_dir), "%s/boards", dst_root);
                if (nb < 0 || (size_t)nb >= sizeof(dst_boards_dir) ||
                    mkdir_p(dst_boards_dir, 0755) < 0) {
                    close(fd);
                    return -1;
                }
            }
        }
    }

    bytebuf_t brd_out = { NULL, 0, (size_t)st.st_size > 0 ? (size_t)st.st_size : 256 };
    brd_out.data = malloc(brd_out.cap);
    if (!brd_out.data) {
        close(fd);
        return -1;
    }

    int rc = 0;
    boardheader_t bh;
    while (read(fd, &bh, sizeof(bh)) == (ssize_t)sizeof(bh)) {
        char bname[IDLEN + 1];
        memcpy(bname, bh.brdname, IDLEN);
        bname[IDLEN] = '\0';

        convert_boardheader(&bh);
        if (buf_append(&brd_out, &bh, sizeof(bh)) < 0) {
            rc = -1;
            break;
        }

        if (!has_src_boards || !is_safe_rel_name(bname))
            continue;

        char dst_bdir[PATH_MAX];
        int n2 = snprintf(dst_bdir, sizeof(dst_bdir), "%s/boards/%c/%s",
                          dst_root, bname[0], bname);
        if (n2 < 0 || (size_t)n2 >= sizeof(dst_bdir))
            continue;

        if (mkdir_p(dst_bdir, 0755) < 0) {
            rc = -1;
            continue;
        }

        if (bh.brdattr & BRD_SYMBOLIC)
            continue;

        char src_bdir[PATH_MAX];
        int n1 = snprintf(src_bdir, sizeof(src_bdir), "%s/boards/%c/%s",
                          src_root, bname[0], bname);
        if (n1 < 0 || (size_t)n1 >= sizeof(src_bdir))
            continue;

        struct stat bst;
        if (stat(src_bdir, &bst) == 0 && S_ISDIR(bst.st_mode)) {
            if (convert_directory(src_bdir, dst_bdir) < 0)
                rc = -1;
        }
    }

    close(fd);

    if (rc == 0) {
        if (write_output(dest ? dst_brd : NULL, st.st_mode,
                         brd_out.data, brd_out.len) < 0)
            rc = -1;
    }

    free(brd_out.data);
    return rc;
}

static int
handle_passwd_file(const char *src_passwd, const char *dest)
{
    struct stat st;
    if (stat(src_passwd, &st) < 0) {
        perror(src_passwd);
        return -1;
    }

    int fd = open(src_passwd, O_RDONLY);
    if (fd < 0) {
        perror(src_passwd);
        return -1;
    }

    char src_root[PATH_MAX];
    get_dirname(src_passwd, src_root, sizeof(src_root));

    char dst_root[PATH_MAX] = "";
    char dst_passwd[PATH_MAX] = "";
    int has_src_home = 0;

    if (dest) {
        int dest_dir = is_dest_dir(dest);
        if (resolve_dest_file(src_passwd, dest,
                              dst_passwd, sizeof(dst_passwd),
                              dst_root, sizeof(dst_root)) < 0) {
            close(fd);
            return -1;
        }
        if (dest_dir) {
            char src_home_dir[PATH_MAX];
            struct stat shst;
            int nsh = snprintf(src_home_dir, sizeof(src_home_dir), "%s/home", src_root);
            if (nsh > 0 && (size_t)nsh < sizeof(src_home_dir) &&
                stat(src_home_dir, &shst) == 0 && S_ISDIR(shst.st_mode)) {
                has_src_home = 1;
                char dst_home_dir[PATH_MAX];
                int nh = snprintf(dst_home_dir, sizeof(dst_home_dir), "%s/home", dst_root);
                if (nh < 0 || (size_t)nh >= sizeof(dst_home_dir) ||
                    mkdir_p(dst_home_dir, 0755) < 0) {
                    close(fd);
                    return -1;
                }
            }
        }
    }

    bytebuf_t pwd_out = { NULL, 0, (size_t)st.st_size > 0 ? (size_t)st.st_size : sizeof(userec_t) };
    pwd_out.data = malloc(pwd_out.cap);
    if (!pwd_out.data) {
        close(fd);
        return -1;
    }

    int rc = 0;
    userec_t u;
    while (read(fd, &u, sizeof(u)) == (ssize_t)sizeof(u)) {
        char uid[IDLEN + 1];
        memcpy(uid, u.userid, IDLEN);
        uid[IDLEN] = '\0';

        convert_userec(&u);
        if (buf_append(&pwd_out, &u, sizeof(u)) < 0) {
            rc = -1;
            break;
        }

        if (!has_src_home || !is_safe_rel_name(uid))
            continue;

        char dst_udir[PATH_MAX];
        int n2 = snprintf(dst_udir, sizeof(dst_udir), "%s/home/%c/%s",
                          dst_root, uid[0], uid);
        if (n2 < 0 || (size_t)n2 >= sizeof(dst_udir))
            continue;

        if (mkdir_p(dst_udir, 0755) < 0) {
            rc = -1;
            continue;
        }

        char src_udir[PATH_MAX];
        int n1 = snprintf(src_udir, sizeof(src_udir), "%s/home/%c/%s",
                          src_root, uid[0], uid);
        if (n1 < 0 || (size_t)n1 >= sizeof(src_udir))
            continue;

        struct stat ust;
        if (stat(src_udir, &ust) == 0 && S_ISDIR(ust.st_mode)) {
            if (convert_directory(src_udir, dst_udir) < 0)
                rc = -1;
        }
    }

    close(fd);

    if (rc == 0) {
        if (write_output(dest ? dst_passwd : NULL, st.st_mode,
                         pwd_out.data, pwd_out.len) < 0)
            rc = -1;
    }

    free(pwd_out.data);
    return rc;
}

static int
convert_directory(const char *src_dir, const char *dst_dir)
{
    if (mkdir_p(dst_dir, 0755) < 0) {
        perror(dst_dir);
        return -1;
    }

    DIR *dp = opendir(src_dir);
    if (!dp) {
        perror(src_dir);
        return -1;
    }

    int rc = 0;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        char src_child[PATH_MAX];
        char dst_child[PATH_MAX];
        int n1 = snprintf(src_child, sizeof(src_child), "%s/%s", src_dir, de->d_name);
        int n2 = snprintf(dst_child, sizeof(dst_child), "%s/%s", dst_dir, de->d_name);
        if (n1 < 0 || (size_t)n1 >= sizeof(src_child) ||
            n2 < 0 || (size_t)n2 >= sizeof(dst_child))
            continue;

        struct stat st;
        if (stat(src_child, &st) < 0)
            continue;

        if (S_ISREG(st.st_mode)) {
            if (is_dir_index_name(de->d_name)) {
                if (convert_dir_index_to(src_child, dst_child, dst_dir) < 0)
                    rc = -1;
            } else if (is_brd_name(de->d_name)) {
                if (handle_brd_file(src_child, dst_dir) < 0)
                    rc = -1;
            } else if (is_passwd_name(de->d_name)) {
                if (handle_passwd_file(src_child, dst_dir) < 0)
                    rc = -1;
            }
        } else if (S_ISDIR(st.st_mode)) {
            if (convert_directory(src_child, dst_child) < 0)
                rc = -1;
        }
    }

    closedir(dp);
    return rc;
}

int
main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <src> [dest]\n\n"
                "The src can be one of:\n"
                " - path to a regular Big5 text file (writes to stdout if dest is omitted)\n"
                " - path to a .DIR (recursive if there are sub folders)\n"
                " - path to a directory containing a .DIR\n"
                " - path to the .BRD file (will create boards/ in dest if boards/ exists)\n"
                " - path to the .PASSWD/.PASSWDS file (will create home/ in dest if home/ exists)\n"
                , argv[0]);
        return 1;
    }

    const char *src = argv[1];
    const char *dest = (argc >= 3) ? argv[2] : NULL;

    if (dest && strcmp(src, dest) == 0) {
        fprintf(stderr, "Error: src and dest must not be the same (%s)\n", src);
        return 1;
    }

    struct stat st;
    int r = -1;
    if (stat(src, &st) < 0) {
        perror(src);
        return 1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (!dest) {
            fprintf(stderr, "Error: dest is required when src is a directory (%s)\n", src);
            return 1;
        }
        r = convert_directory(src, dest) < 0 ? 1 : 0;
    }
    else if (S_ISREG(st.st_mode)) {
        const char *base = get_basename(src);
        if (is_dir_index_name(base)) {
            r = handle_dir_index(src, dest) < 0 ? 1 : 0;
        }
        else if (is_brd_name(base)) {
            r = handle_brd_file(src, dest) < 0 ? 1 : 0;
        }
        else if (is_passwd_name(base)) {
            r = handle_passwd_file(src, dest) < 0 ? 1 : 0;
        }
        else {
            r = handle_plain_file(src, dest) < 0 ? 1 : 0;
        }
    }
    if (max_printed_col > 0)
        fprintf(stderr, "%-*s\n",
                (int)strlen(PROGRESS_PREFIX) + max_printed_col, "");
    return r;
}
