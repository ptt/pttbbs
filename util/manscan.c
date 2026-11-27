/* manscan: find (and optionally remove) announce (man) directories that
 * hold no articles at all, however deep they go.
 *
 * The tree is walked through the .DIR lists as mbbsd shows it (hidden and
 * BM only entries too). A directory is empty when no regular file is
 * reachable below it; the empty ones right under a non-empty directory (or
 * the board root) are the "empty trees" reported and removed.
 */
#define _UTIL_C_
#include "bbs.h"

#include <getopt.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#define MAX_SCAN_DEPTH  256
#define MAX_SCAN_PATH   4096

typedef struct {
    uint64_t dirs;      /* directories in the subtree, itself not counted */
    uint64_t files;     /* regular files (articles) in the subtree */
    int depth;          /* deepest directory below, relative to this one */
    int unknown;        /* not walked fully (too deep, unreadable): keep */
} tree_t;

typedef struct {
    uint64_t dirs, files, broken, bad, dir_bytes;
    uint64_t empty_dirs, empty_trees, removed_trees, removed_dirs;
    int max_depth;
    off_t index_bytes;
} board_t;

typedef struct {
    char filename[FNLEN];
    char title[TTLEN + 1];
    uint64_t dirs;      /* the empty directory itself included */
    int depth;
} empty_t;

static int opt_list = 0;
static int opt_remove = 0;
static uint64_t opt_min = 1;        /* boards with fewer empty dirs: quiet */
static uint64_t opt_threshold = 1;  /* -r only trees with this many dirs */
static board_t cur;
static board_t total;

static void usage(void)
{
    printf("Usage: manscan [-l] [-r] [-m N] [-t N] [board ...]\n"
           "\n"
           "Find announce (man) directories that hold no articles at all.\n"
           "Without boards, every board under man/boards is scanned.\n"
           "\n"
           "  -l    also list every empty tree: path, dirs, depth, title\n"
           "  -m N  only show boards with at least N empty dirs (default 1)\n"
           "  -r    remove the empty trees (drop the .DIR record, then the\n"
           "        directories); without it nothing is changed. Only the\n"
           "        boards with trees removed are shown\n"
           "  -t N  with -r, only remove trees of at least N dirs (default 1)\n"
           "\n"
           "One line per board, tab separated:\n"
           "  board dirs files empty_dirs empty_trees max_depth"
           " dir_bytes index_bytes\n"
           "dirs and files are reachable from the board's .DIR (hidden ones\n"
           "too); empty_dirs are the dirs inside empty trees; dir_bytes is the\n"
           "size of all .DIR files; index_bytes is the size of mandex's .index.\n"
           "Boards with removed trees get .rebuild so 'mandex -x' redoes them.\n");
}

/* Only subdirectories and .DIR files all the way down: removing it loses
 * no article, even one not listed in any .DIR. */
static int fs_only_dirs(const char *path, int depth)
{
    DIR *dir;
    struct dirent *de;
    int ok = 1;

    if (depth > MAX_SCAN_DEPTH || !(dir = opendir(path)))
        return 0;

    while (ok && (de = readdir(dir))) {
        char sub[MAX_SCAN_PATH];
        struct stat st;

        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        if (snprintf(sub, sizeof(sub), "%s/%s", path, de->d_name) >=
            (int)sizeof(sub) || lstat(sub, &st) != 0) {
            ok = 0;
            break;
        }
        if (S_ISDIR(st.st_mode))
            ok = fs_only_dirs(sub, depth + 1);
        else if (!S_ISREG(st.st_mode) ||
                 strncmp(de->d_name, FN_DIR, strlen(FN_DIR)) != 0)
            ok = 0;
    }
    closedir(dir);
    return ok;
}

/* Remove a tree checked by fs_only_dirs(): only .DIR files are unlinked,
 * so anything put there since is kept (and its directories with it). */
static void fs_remove(const char *path, int depth)
{
    DIR *dir;
    struct dirent *de;

    if (depth > MAX_SCAN_DEPTH || !(dir = opendir(path)))
        return;

    while ((de = readdir(dir))) {
        char sub[MAX_SCAN_PATH];
        struct stat st;

        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        if (snprintf(sub, sizeof(sub), "%s/%s", path, de->d_name) >=
            (int)sizeof(sub) || lstat(sub, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            fs_remove(sub, depth + 1);
        else if (S_ISREG(st.st_mode) &&
                 !strncmp(de->d_name, FN_DIR, strlen(FN_DIR)))
            unlink(sub);
    }
    closedir(dir);
    if (rmdir(path) != 0)
        fprintf(stderr, "[WARN] %s: not removed: %s\n", path, strerror(errno));
}

/* Under the flock every .DIR writer (append_record) takes. */
static int drop_records(const char *path, empty_t *list, int n)
{
    char dir_path[MAX_SCAN_PATH];
    fileheader_t fh;
    off_t kept = 0, idx = 0;
    int fd, dropped = 0;

    snprintf(dir_path, sizeof(dir_path), "%s/%s", path, FN_DIR);
    if ((fd = open(dir_path, O_RDWR)) < 0) {
        fprintf(stderr, "[WARN] %s: %s\n", dir_path, strerror(errno));
        return 0;
    }
    if (flock(fd, LOCK_EX) != 0) {
        fprintf(stderr, "[WARN] %s: cannot lock: %s\n", dir_path,
                strerror(errno));
        close(fd);
        return 0;
    }
    while (pread(fd, &fh, sizeof(fh), idx * (off_t)sizeof(fh)) ==
           (ssize_t)sizeof(fh)) {
        int drop = 0, i;

        idx++;
        for (i = 0; i < n; i++) {
            if (list[i].filename[0] &&
                !strncmp(fh.filename, list[i].filename, FNLEN)) {
                drop = 1;
                break;
            }
        }
        if (drop) {
            dropped++;
            continue;
        }
        /* Kept records move down over dropped ones, never past unread. */
        if (dropped && pwrite(fd, &fh, sizeof(fh), kept * (off_t)sizeof(fh))
            != (ssize_t)sizeof(fh)) {
            fprintf(stderr, "[ERROR] %s: cannot write: %s\n", dir_path,
                    strerror(errno));
            break;
        }
        kept++;
    }
    if (dropped && ftruncate(fd, kept * (off_t)sizeof(fh)) != 0)
        fprintf(stderr, "[ERROR] %s: cannot cut: %s\n", dir_path,
                strerror(errno));
    flock(fd, LOCK_UN);
    close(fd);
    return dropped;
}

/* Handle the empty trees right under path, a directory with articles (or
 * the board root). */
static void report_empty(const char *path, empty_t *list, int n)
{
    int i, nremove = 0;

    for (i = 0; i < n; i++) {
        cur.empty_trees++;
        cur.empty_dirs += list[i].dirs;
        if (opt_list)
            printf("  %s/%s\t%" PRIu64 "\t%d\t%s\n", path, list[i].filename,
                   list[i].dirs, list[i].depth, list[i].title);
    }
    if (!opt_remove)
        return;

    /* Keep (by blanking the name) what is too small or not really empty. */
    for (i = 0; i < n; i++) {
        char sub[MAX_SCAN_PATH];

        snprintf(sub, sizeof(sub), "%s/%s", path, list[i].filename);
        if (list[i].dirs < opt_threshold || !fs_only_dirs(sub, 0)) {
            list[i].filename[0] = '\0';
            continue;
        }
        nremove++;
    }
    if (!nremove || !drop_records(path, list, n))
        return;

    for (i = 0; i < n; i++) {
        char sub[MAX_SCAN_PATH];

        if (!list[i].filename[0])
            continue;
        snprintf(sub, sizeof(sub), "%s/%s", path, list[i].filename);
        fs_remove(sub, 0);
        cur.removed_trees++;
        cur.removed_dirs += list[i].dirs;
        printf("  removed %s\t%" PRIu64 "\n", sub, list[i].dirs);
    }
}

static void scan(const char *path, int depth, tree_t *t)
{
    char dir_path[MAX_SCAN_PATH];
    fileheader_t fh;
    struct stat st;
    empty_t *empty = NULL;
    int nempty = 0, aempty = 0;
    FILE *fp;

    memset(t, 0, sizeof(*t));
    if (depth > cur.max_depth)
        cur.max_depth = depth;

    snprintf(dir_path, sizeof(dir_path), "%s/%s", path, FN_DIR);
    if (!(fp = fopen(dir_path, "r"))) {
        if (errno != ENOENT)
            t->unknown = 1;
        return;
    }
    if (fstat(fileno(fp), &st) == 0)
        cur.dir_bytes += st.st_size;

    while (fread(&fh, sizeof(fh), 1, fp) == 1) {
        char sub[MAX_SCAN_PATH];
        tree_t child;

        if (!fh.filename[0])
            continue;
        /* Bad names (".", "..", ".DIR", "a/b") would loop or point at
         * index files: neither files nor directories. */
        if (fh.filename[0] == '.' || memchr(fh.filename, '/', FNLEN)) {
            cur.bad++;
            continue;
        }
        if (snprintf(sub, sizeof(sub), "%s/%.*s", path, FNLEN, fh.filename)
            >= (int)sizeof(sub)) {
            t->unknown = 1;
            continue;
        }
        /* Not followed: symlinks (like mandex's .index) count as files. */
        if (lstat(sub, &st) != 0) {
            cur.broken++;
            continue;
        }
        if (!S_ISDIR(st.st_mode)) {
            char link[16];
            ssize_t len;

            /* mandex's own entry (a link to .index) is not an article:
             * else no board with it would look empty. */
            if (S_ISLNK(st.st_mode) &&
                (len = readlink(sub, link, sizeof(link) - 1)) > 0) {
                link[len] = '\0';
                if (!strcmp(link, ".index"))
                    continue;
            }
            t->files++;
            cur.files++;
            continue;
        }

        cur.dirs++;
        t->dirs++;
        if (depth + 1 > MAX_SCAN_DEPTH) {
            t->unknown = 1;
            continue;
        }
        scan(sub, depth + 1, &child);
        t->dirs += child.dirs;
        t->files += child.files;
        t->unknown |= child.unknown;
        if (child.depth + 1 > t->depth)
            t->depth = child.depth + 1;

        if (child.files || child.unknown)
            continue;
        if (nempty == aempty) {
            aempty = aempty ? aempty * 2 : 16;
            empty = realloc(empty, aempty * sizeof(*empty));
            if (!empty) {
                perror("realloc");
                exit(1);
            }
        }
        strlcpy(empty[nempty].filename, fh.filename, FNLEN);
        strlcpy(empty[nempty].title, fh.title, sizeof(empty[nempty].title));
        empty[nempty].dirs = child.dirs + 1;
        empty[nempty].depth = child.depth + 1;
        nempty++;
    }
    fclose(fp);

    /* An empty directory under an empty one is part of the bigger tree. */
    if (nempty && (depth == 0 || t->files || t->unknown))
        report_empty(path, empty, nempty);
    free(empty);
}

static void scan_board(const char *bname)
{
    char path[PATHLEN], buf[PATHLEN];
    struct stat st;
    tree_t t;

    memset(&cur, 0, sizeof(cur));
    setapath(path, bname);
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
        return;

    scan(path, 0, &t);

    snprintf(buf, sizeof(buf), "%s/.index", path);
    if (stat(buf, &st) == 0)
        cur.index_bytes = st.st_size;

    if (cur.removed_trees) {
        FILE *fp;
        snprintf(buf, sizeof(buf), "%s/.rebuild", path);
        if ((fp = fopen(buf, "w")))
            fclose(fp);
    }

    total.dirs += cur.dirs;
    total.files += cur.files;
    total.broken += cur.broken;
    total.bad += cur.bad;
    total.dir_bytes += cur.dir_bytes;
    total.empty_dirs += cur.empty_dirs;
    total.empty_trees += cur.empty_trees;
    total.removed_trees += cur.removed_trees;
    total.removed_dirs += cur.removed_dirs;
    total.index_bytes += cur.index_bytes;
    if (cur.max_depth > total.max_depth)
        total.max_depth = cur.max_depth;

    /* With -r, only the boards changed; -m (and -t) say what to look at. */
    if (cur.empty_dirs < opt_min || (opt_remove && !cur.removed_trees))
        return;
    printf("%s\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%d"
           "\t%" PRIu64 "\t%jd\n", bname, cur.dirs, cur.files,
           cur.empty_dirs, cur.empty_trees, cur.max_depth, cur.dir_bytes,
           (intmax_t)cur.index_bytes);
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    int c, i;

    while ((c = getopt(argc, argv, "lrm:t:h")) != -1) {
        switch (c) {
        case 'l':
            opt_list = 1;
            break;
        case 'r':
            opt_remove = 1;
            break;
        case 'm':
            opt_min = strtoull(optarg, NULL, 10);
            break;
        case 't':
            opt_threshold = strtoull(optarg, NULL, 10);
            break;
        default:
            usage();
            return c == 'h' ? 0 : 1;
        }
    }
    argc -= optind;
    argv += optind;

    if (chdir(BBSHOME) != 0) {
        perror(BBSHOME);
        return 1;
    }
    nice(10);

    if (argc > 0) {
        for (i = 0; i < argc; i++)
            scan_board(argv[i]);
    } else {
        DIR *top = opendir("man/boards");
        struct dirent *de;

        if (!top) {
            perror("man/boards");
            return 1;
        }
        while ((de = readdir(top))) {
            char sub[PATHLEN];
            DIR *dir;
            struct dirent *be;

            if (de->d_name[0] == '.' || de->d_name[1])
                continue;
            snprintf(sub, sizeof(sub), "man/boards/%s", de->d_name);
            if (!(dir = opendir(sub)))
                continue;
            while ((be = readdir(dir))) {
                /* setapath() puts a board under its first letter. */
                if (be->d_name[0] == '.' || be->d_name[0] != de->d_name[0])
                    continue;
                scan_board(be->d_name);
            }
            closedir(dir);
        }
        closedir(top);
    }

    printf("# total\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
           "\t%d\t%" PRIu64 "\t%jd\n", total.dirs, total.files,
           total.empty_dirs, total.empty_trees, total.max_depth,
           total.dir_bytes, (intmax_t)total.index_bytes);
    if (total.broken)
        printf("# records without their file: %" PRIu64 "\n", total.broken);
    if (total.bad)
        printf("# records with bad names (skipped): %" PRIu64 "\n", total.bad);
    if (opt_remove)
        printf("# removed %" PRIu64 " trees, %" PRIu64 " dirs\n",
               total.removed_trees, total.removed_dirs);
    return 0;
}
