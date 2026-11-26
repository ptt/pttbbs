// The client of gem.svc (service/gem/protocol): one command per connection,
// a header line and its payload, then "OK ..." and a payload, or
// "ERR <code> [detail]". See include/gem.h.

#include "bbs.h"
#include "gem.h"

#include <poll.h>

#define GEM_TIMEOUT_MS  (5000)

static char last_error[256];
static const char *gem_socket = GET_SVC_SOCKET(gem);

void
gem_set_socket(const char *path)
{
    gem_socket = path ? path : GET_SVC_SOCKET(gem);
}

const char *
gem_last_error(void)
{
    return last_error;
}

int
gem_last_error_is(const char *code)
{
    size_t n = strlen(code);
    return strncmp(last_error, code, n) == 0 &&
           (last_error[n] == '\0' || last_error[n] == ' ');
}

static void
set_error(const char *fmt, ...) GCC_CHECK_FORMAT(1, 2);

static void
set_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(last_error, sizeof(last_error), fmt, ap);
    va_end(ap);
}

// --------------------------------------------------------------------------
// Paths of man/

// The links of etc/ into gem areas (util/initbbs.c).
static const struct {
    const char *link, *target;
} etc_links[] = {
    { "etc/SONGBOOK", "man/boards/N/Note/SONGBOOK" },
    { "etc/SONGO", "man/boards/N/Note/SONGO" },
};

// Whether s starts with the path p (followed by '/' or the end); the rest
// in *rest (without the '/').
static int
path_prefix(const char *s, const char *p, const char **rest)
{
    size_t n = strlen(p);
    if (strncmp(s, p, n) != 0 || (s[n] && s[n] != '/'))
        return 0;
    *rest = s[n] ? s + n + 1 : s + n;
    return 1;
}

// Copies the next component of *s (up to '/') into out; advances *s.
static int
next_name(const char **s, char *out, size_t sz)
{
    const char *p = *s, *e = strchr(p, '/');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    if (n == 0 || n >= sz)
        return 0;
    memcpy(out, p, n);
    out[n] = '\0';
    *s = e ? e + 1 : p + n;
    return 1;
}

int
gem_path_parse(const char *path, char *root, size_t rootsz,
               char *sub, size_t subsz)
{
    char buf[PATHLEN], name[IDLEN + 2], letter[4];
    const char *rest;

    if (!path)
        return 0;
    if (path_prefix(path, BBSHOME, &rest))
        path = rest;
    for (size_t i = 0; i < ARRAY_SIZE(etc_links); i++) {
        if (path_prefix(path, etc_links[i].link, &rest)) {
            snprintf(buf, sizeof(buf), "%s%s%s", etc_links[i].target,
                     *rest ? "/" : "", rest);
            path = buf;
            break;
        }
    }

    if (path_prefix(path, "trash", &rest)) {
        // trash/<root>[/sub]: the trash of root.
        char area[GEM_ROOT_SIZE];
        if (!next_name(&rest, area, sizeof(area)) ||
            !(strcmp(area, "site") == 0 || strncmp(area, "board:", 6) == 0 ||
              strncmp(area, "user:", 5) == 0))
            return 0;
        snprintf(root, rootsz, GEM_TRASH_PREFIX "%s", area);
    } else if (path_prefix(path, "man/boards", &rest)) {
        // man/boards/<c>/<board>[/sub]
        if (!next_name(&rest, letter, sizeof(letter)) ||
            !next_name(&rest, name, sizeof(name)))
            return 0;
        snprintf(root, rootsz, "board:%s", name);
    } else if (path_prefix(path, "man", &rest)) {
        strlcpy(root, "site", rootsz);
    } else if (path_prefix(path, "home", &rest)) {
        // home/<c>/<id>/man[/sub]
        if (!next_name(&rest, letter, sizeof(letter)) ||
            !next_name(&rest, name, sizeof(name)) ||
            !path_prefix(rest, "man", &rest))
            return 0;
        snprintf(root, rootsz, "user:%s", name);
    } else {
        return 0;
    }
    strlcpy(sub, rest, subsz);
    // No trailing slash.
    size_t n = strlen(sub);
    while (n > 0 && sub[n - 1] == '/')
        sub[--n] = '\0';
    return 1;
}

// --------------------------------------------------------------------------
// IPC

typedef struct {
    int fd;
    char line[512];     // the reply line, without "OK " and the newline
    char *words[16];
    int nwords;
    char buf[8192];     // read ahead
    size_t start, end;
} reply_t;

// The sub path as gem.svc takes it ("." for the top).
static const char *
wire_path(const char *sub)
{
    return (sub && *sub) ? sub : ".";
}

static int
fill(reply_t *r)
{
    struct pollfd pfd = { .fd = r->fd, .events = POLLIN };
    int pr;
    ssize_t n;

    if (r->start == r->end)
        r->start = r->end = 0;
    if (r->end == sizeof(r->buf))
        return 0;
    do {
        pr = poll(&pfd, 1, GEM_TIMEOUT_MS);
    } while (pr < 0 && errno == EINTR);
    if (pr <= 0)
        return -1;
    n = read(r->fd, r->buf + r->end, sizeof(r->buf) - r->end);
    if (n <= 0)
        return -1;
    r->end += n;
    return 0;
}

// Reads exactly n bytes of the payload into out (NULL: skips them).
static int
read_exact(reply_t *r, void *out, size_t n)
{
    char *p = out;
    while (n > 0) {
        if (r->start == r->end && fill(r) < 0) {
            set_error("bbs_gem_unavailable short reply");
            return -1;
        }
        size_t k = r->end - r->start;
        if (k > n)
            k = n;
        if (p) {
            memcpy(p, r->buf + r->start, k);
            p += k;
        }
        r->start += k;
        n -= k;
    }
    return 0;
}

// Sends hdr (a line, without its newline) and the payloads, and reads the
// reply line. Returns 0 with r open on OK (close with reply_close()), or -1
// (the error set, r closed).
static int
call(reply_t *r, const char *hdr, const void *p1, size_t l1,
     const void *p2, size_t l2)
{
    char line[1024];
    int n;

    memset(r, 0, sizeof(*r));
    r->fd = -1;
    n = snprintf(line, sizeof(line), "%s\n", hdr);
    if (n < 0 || n >= (int)sizeof(line)) {
        set_error("bbs_gem_bad_request too long");
        return -1;
    }
    if ((r->fd = toconnect3(gem_socket, 0, SVC_CONNECT_TIMEOUT_US * 10)) < 0) {
        set_error("bbs_gem_unavailable cannot connect to gem.svc");
        return -1;
    }
    if (towrite(r->fd, line, n) != n ||
        (p1 && l1 && towrite(r->fd, p1, l1) != (int)l1) ||
        (p2 && l2 && towrite(r->fd, p2, l2) != (int)l2)) {
        set_error("bbs_gem_unavailable cannot write to gem.svc");
        close(r->fd);
        r->fd = -1;
        return -1;
    }
    // The reply line.
    for (;;) {
        char *nl = memchr(r->buf + r->start, '\n', r->end - r->start);
        if (nl) {
            size_t len = nl - (r->buf + r->start);
            if (len >= sizeof(r->line))
                len = sizeof(r->line) - 1;
            memcpy(r->line, r->buf + r->start, len);
            r->line[len] = '\0';
            r->start = nl + 1 - r->buf;
            break;
        }
        if (fill(r) < 0) {
            set_error("bbs_gem_unavailable no reply from gem.svc");
            close(r->fd);
            r->fd = -1;
            return -1;
        }
    }
    if (strncmp(r->line, "ERR ", 4) == 0) {
        set_error("%s", r->line + 4);
        close(r->fd);
        r->fd = -1;
        return -1;
    }
    if (strcmp(r->line, "PONG") != 0 && strcmp(r->line, "OK") != 0 &&
        strncmp(r->line, "OK ", 3) != 0) {
        set_error("bbs_gem_unavailable bad reply");
        close(r->fd);
        r->fd = -1;
        return -1;
    }
    // Split the words after OK.
    char *p = r->line + 2, *save = NULL, *w;
    for (w = strtok_r(p, " ", &save); w && r->nwords < (int)ARRAY_SIZE(r->words);
         w = strtok_r(NULL, " ", &save))
        r->words[r->nwords++] = w;
    last_error[0] = '\0';
    return 0;
}

static void
reply_close(reply_t *r)
{
    if (r->fd >= 0)
        close(r->fd);
    r->fd = -1;
}

static long long
word(const reply_t *r, int i)
{
    return i < r->nwords ? atoll(r->words[i]) : 0;
}

// Sends a command without payload in reply; 0 if OK.
static int
simple(const char *hdr, const void *p1, size_t l1, const void *p2, size_t l2)
{
    reply_t r;
    if (call(&r, hdr, p1, l1, p2, l2) < 0)
        return -1;
    reply_close(&r);
    return 0;
}

// Reads a record of an entry following a reply.
static int
read_entry(reply_t *r, gem_entry_t *out)
{
    gem_entry_t tmp;
    return read_exact(r, out ? out : &tmp, sizeof(gem_entry_t));
}

// --------------------------------------------------------------------------
// Calls

int
gem_list(const char *root, const char *sub, int offset, int limit,
         gem_entry_t *out, int *count, gem_totals_t *totals)
{
    char hdr[PATHLEN + 64];
    reply_t r;
    int n;

    snprintf(hdr, sizeof(hdr), "LIST %s %s %d %d", root, wire_path(sub),
             offset, limit);
    if (call(&r, hdr, NULL, 0, NULL, 0) < 0) {
        if (gem_last_error_is(GEM_ERR_NO_ROOT) && (!sub || !*sub)) {
            // An area not made yet is empty.
            if (count)
                *count = 0;
            if (totals)
                memset(totals, 0, sizeof(*totals));
            return 0;
        }
        return -1;
    }
    if (count)
        *count = (int)word(&r, 0);
    n = (int)word(&r, 1);
    if (totals) {
        totals->files = word(&r, 2);
        totals->dirs = word(&r, 3);
        totals->links = word(&r, 4);
        totals->missing = word(&r, 5);
        totals->bytes = word(&r, 6);
        totals->depth = word(&r, 7);
    }
    if (n > limit && limit >= 0)
        n = limit;
    for (int i = 0; i < n; i++) {
        if (read_entry(&r, out ? &out[i] : NULL) < 0) {
            reply_close(&r);
            return -1;
        }
    }
    reply_close(&r);
    return n;
}

int
gem_entry(const char *root, const char *sub, int idx, const char *name,
          gem_entry_t *out, int *index, char *target, size_t targetsz)
{
    char hdr[PATHLEN + 64];
    reply_t r;

    snprintf(hdr, sizeof(hdr), "ENTRY %s %s %d %s", root, wire_path(sub),
             idx, name);
    if (call(&r, hdr, NULL, 0, NULL, 0) < 0)
        return -1;
    size_t tlen = (size_t)word(&r, 1);
    if (index)
        *index = (int)word(&r, 0);
    if (read_entry(&r, out) < 0) {
        reply_close(&r);
        return -1;
    }
    if (target && targetsz) {
        size_t k = tlen < targetsz - 1 ? tlen : targetsz - 1;
        if (read_exact(&r, target, k) < 0) {
            reply_close(&r);
            return -1;
        }
        target[k] = '\0';
    }
    reply_close(&r);
    return 0;
}

char *
gem_get(const char *root, const char *sub, int idx, const char *name,
        size_t *len)
{
    char hdr[PATHLEN + 64];
    reply_t r;
    char *data;

    snprintf(hdr, sizeof(hdr), "GET %s %s %d %s", root, wire_path(sub), idx,
             name);
    if (call(&r, hdr, NULL, 0, NULL, 0) < 0)
        return NULL;
    size_t n = (size_t)word(&r, 0);
    if (!(data = malloc(n + 1))) {
        set_error("bbs_gem_unavailable out of memory");
        reply_close(&r);
        return NULL;
    }
    if (read_exact(&r, data, n) < 0) {
        free(data);
        reply_close(&r);
        return NULL;
    }
    data[n] = '\0';
    reply_close(&r);
    if (len)
        *len = n;
    return data;
}

int
gem_get_to_file(const char *root, const char *sub, int idx, const char *name,
                const char *fpath)
{
    size_t len;
    char *data = gem_get(root, sub, idx, name, &len);
    int fd, ok;

    if (!data)
        return -1;
    if ((fd = open(fpath, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
        set_error("bbs_gem_unavailable cannot write %s", fpath);
        free(data);
        return -1;
    }
    ok = (len == 0 || towrite(fd, data, len) == (int)len);
    close(fd);
    free(data);
    if (!ok) {
        set_error("bbs_gem_unavailable cannot write %s", fpath);
        return -1;
    }
    return 0;
}

static const char *
kind_name(int kind)
{
    switch (kind) {
    case GEM_KIND_FILE:
        return "file";
    case GEM_KIND_DIR:
        return "dir";
    }
    return "?";
}

int
gem_add(int kind, const char *root, const char *sub, int pos,
        const fileheader_t *h, const void *data, size_t len, gem_entry_t *out)
{
    char hdr[PATHLEN + 64];
    reply_t r;
    int at;

    snprintf(hdr, sizeof(hdr), "ADD %s %s %s %d %zu", kind_name(kind), root,
             wire_path(sub), pos, data ? len : 0);
    if (call(&r, hdr, h, sizeof(*h), data, data ? len : 0) < 0)
        return -1;
    at = (int)word(&r, 0);
    if (read_entry(&r, out) < 0) {
        reply_close(&r);
        return -1;
    }
    reply_close(&r);
    return at;
}

// The content of fpath, to free(); NULL if it cannot be read.
static char *
slurp(const char *fpath, size_t *len)
{
    struct stat st;
    char *data;
    int fd = open(fpath, O_RDONLY);

    if (fd < 0 || fstat(fd, &st) < 0 || !(data = malloc(st.st_size + 1))) {
        if (fd >= 0)
            close(fd);
        set_error("bbs_gem_unavailable cannot read %s", fpath);
        return NULL;
    }
    if (st.st_size && toread(fd, data, st.st_size) != st.st_size) {
        close(fd);
        free(data);
        set_error("bbs_gem_unavailable cannot read %s", fpath);
        return NULL;
    }
    close(fd);
    data[st.st_size] = '\0';
    *len = st.st_size;
    return data;
}

int
gem_add_file(const char *root, const char *sub, int pos, const fileheader_t *h,
             const char *fpath, gem_entry_t *out)
{
    size_t len;
    char *data = slurp(fpath, &len);
    int at;

    if (!data)
        return -1;
    at = gem_add(GEM_KIND_FILE, root, sub, pos, h, data, len, out);
    free(data);
    return at;
}

int
gem_set_header(const char *root, const char *sub, int idx, const char *name,
               const fileheader_t *h, gem_entry_t *out)
{
    char hdr[PATHLEN + 64];
    reply_t r;

    snprintf(hdr, sizeof(hdr), "SET_HEADER %s %s %d %s", root,
             wire_path(sub), idx, name);
    if (call(&r, hdr, h, sizeof(*h), NULL, 0) < 0)
        return -1;
    if (read_entry(&r, out) < 0) {
        reply_close(&r);
        return -1;
    }
    reply_close(&r);
    return 0;
}

int
gem_put(const char *root, const char *sub, int idx, const char *name,
        const void *data, size_t len)
{
    char hdr[PATHLEN + 64];
    snprintf(hdr, sizeof(hdr), "PUT %s %s %d %s %zu", root, wire_path(sub),
             idx, name, len);
    return simple(hdr, data, len, NULL, 0);
}

int
gem_put_file(const char *root, const char *sub, int idx, const char *name,
             const char *fpath)
{
    size_t len;
    char *data = slurp(fpath, &len);
    int rv;

    if (!data)
        return -1;
    rv = gem_put(root, sub, idx, name, data, len);
    free(data);
    return rv;
}

int
gem_remove(const char *root, const char *sub, int idx, const char *name)
{
    char hdr[PATHLEN + 64];
    snprintf(hdr, sizeof(hdr), "REMOVE %s %s %d %s", root, wire_path(sub),
             idx, name);
    return simple(hdr, NULL, 0, NULL, 0);
}

int
gem_is_trash(const char *root)
{
    return strncmp(root, GEM_TRASH_PREFIX, strlen(GEM_TRASH_PREFIX)) == 0;
}

int
gem_delete(const char *root, const char *sub, int idx, const char *name,
           const char *by)
{
    char hdr[PATHLEN + 64];
    snprintf(hdr, sizeof(hdr), "DELETE %s %s %d %s %s", root, wire_path(sub),
             idx, name, (by && *by) ? by : "-");
    return simple(hdr, NULL, 0, NULL, 0);
}

// Reads the line following a reply into out (NUL terminated, cut to sz).
static int
read_line(reply_t *r, char *out, size_t sz)
{
    size_t n = 0;
    for (;;) {
        if (r->start == r->end && fill(r) < 0) {
            set_error("bbs_gem_unavailable short reply");
            return -1;
        }
        char c = r->buf[r->start++];
        if (c == '\n')
            break;
        if (out && n + 1 < sz)
            out[n++] = c;
    }
    if (out && sz)
        out[n] = '\0';
    return 0;
}

// The path of a reply line as a sub path ("" for the top).
static void
sub_path(char *path)
{
    if (strcmp(path, ".") == 0)
        path[0] = '\0';
}

int
gem_trash_from(const char *root, const char *name, char *path, size_t pathsz)
{
    char hdr[GEM_ROOT_SIZE + FNSIZE + 32];
    reply_t r;
    int idx;

    snprintf(hdr, sizeof(hdr), "TRASH_ENTRY %s %s", root, name);
    if (call(&r, hdr, NULL, 0, NULL, 0) < 0)
        return -1;
    idx = (int)word(&r, 0);
    if (read_line(&r, path, pathsz) < 0) {
        reply_close(&r);
        return -1;
    }
    reply_close(&r);
    if (path)
        sub_path(path);
    return idx;
}

int
gem_restore(const char *root, const char *name, char *path, size_t pathsz)
{
    char hdr[GEM_ROOT_SIZE + FNSIZE + 32];
    reply_t r;
    int idx;

    snprintf(hdr, sizeof(hdr), "RESTORE %s %s", root, name);
    if (call(&r, hdr, NULL, 0, NULL, 0) < 0)
        return -1;
    idx = (int)word(&r, 0);
    if (read_line(&r, path, pathsz) < 0) {
        reply_close(&r);
        return -1;
    }
    reply_close(&r);
    if (path)
        sub_path(path);
    return idx;
}

int
gem_reorder(const char *root, const char *sub, int idx, const char *name,
            int to)
{
    char hdr[PATHLEN + 64];
    reply_t r;
    int at;

    snprintf(hdr, sizeof(hdr), "REORDER %s %s %d %s %d", root, wire_path(sub),
             idx, name, to);
    if (call(&r, hdr, NULL, 0, NULL, 0) < 0)
        return -1;
    at = (int)word(&r, 0);
    reply_close(&r);
    return at;
}

int
gem_copy(int move, const char *root, const char *sub, int idx,
         const char *name, const char *droot, const char *dsub, int pos,
         const fileheader_t *h, gem_entry_t *out)
{
    char hdr[PATHLEN * 2 + 64];
    reply_t r;
    int at;

    snprintf(hdr, sizeof(hdr), "%s %s %s %d %s %s %s %d %d",
             move ? "MOVE" : "COPY", root, wire_path(sub), idx, name, droot,
             wire_path(dsub), pos, h ? 1 : 0);
    if (call(&r, hdr, h, h ? sizeof(*h) : 0, NULL, 0) < 0)
        return -1;
    at = (int)word(&r, 0);
    if (read_entry(&r, out) < 0) {
        reply_close(&r);
        return -1;
    }
    reply_close(&r);
    return at;
}

char *
gem_index(const char *root, size_t *len)
{
    char hdr[GEM_ROOT_SIZE + 16];
    reply_t r;
    char *data;

    snprintf(hdr, sizeof(hdr), "INDEX %s", root);
    if (call(&r, hdr, NULL, 0, NULL, 0) < 0)
        return NULL;
    size_t n = (size_t)word(&r, 0);
    if (!(data = malloc(n + 1)) || read_exact(&r, data, n) < 0) {
        free(data);
        reply_close(&r);
        return NULL;
    }
    data[n] = '\0';
    reply_close(&r);
    if (len)
        *len = n;
    return data;
}

int
gem_stat(const char *root, gem_totals_t *t)
{
    char hdr[GEM_ROOT_SIZE + 16];
    reply_t r;

    snprintf(hdr, sizeof(hdr), "STAT %s", root);
    if (call(&r, hdr, NULL, 0, NULL, 0) < 0)
        return -1;
    if (t) {
        t->files = word(&r, 0);
        t->dirs = word(&r, 1);
        t->links = word(&r, 2);
        t->missing = word(&r, 3);
        t->bytes = word(&r, 4);
        t->depth = word(&r, 5);
    }
    reply_close(&r);
    return 0;
}

int
gem_root_exists(const char *root)
{
    return gem_stat(root, NULL) == 0;
}

int
gem_user_folder_exists(const char *userid)
{
    char root[GEM_ROOT_SIZE];
    snprintf(root, sizeof(root), "user:%s", userid);
    return gem_root_exists(root);
}

int
gem_root_create(const char *root)
{
    char hdr[GEM_ROOT_SIZE + 16];
    snprintf(hdr, sizeof(hdr), "CREATE_ROOT %s", root);
    return simple(hdr, NULL, 0, NULL, 0);
}

int
gem_root_delete(const char *root)
{
    char hdr[GEM_ROOT_SIZE + 16];
    snprintf(hdr, sizeof(hdr), "DELETE_ROOT %s", root);
    return simple(hdr, NULL, 0, NULL, 0);
}

int
gem_root_rename(const char *from, const char *to)
{
    char hdr[GEM_ROOT_SIZE * 2 + 16];
    snprintf(hdr, sizeof(hdr), "RENAME_ROOT %s %s", from, to);
    return simple(hdr, NULL, 0, NULL, 0);
}
