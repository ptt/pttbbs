#define _UTIL_C_
#include "bbs.h"

#include <getopt.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#define MAX_RECURSE_DEPTH 64
#define MAX_SCAN_PATH 4096
#define CHUNK_RECORDS 64

/* Names are checked over all that fileheader_t.filename was (LEGACY_FNSIZE:
 * filename, then reserved): older binaries may still write names that long,
 * and reserved must stay zero for them. */
#define NAME_SPAN LEGACY_FNSIZE
#define FH_NAME(fh) ((char *)(fh)->filename)

typedef struct {
    uint64_t total_dirs;
    uint64_t empty_dirs;
    uint64_t total_records;
    uint64_t invalid_size_dirs;
    uint64_t count_gt_threshold;
    uint64_t count_eq_threshold;
    uint64_t len_hist[NAME_SPAN + 1];
    int max_len;
    char max_len_fn[NAME_SPAN + 1];
    char max_len_path[MAX_SCAN_PATH];
    int max_len_idx;

    /* Repair statistics */
    uint64_t need_repair_renamed;
    uint64_t need_repair_trailing;
    uint64_t repaired_renamed;
    uint64_t repaired_trailing;

    /* --rename: long names given the standard M.<time>.A.<hex> */
    uint64_t need_shorten;
    uint64_t shortened;
    uint64_t shorten_aid_changed;
    uint64_t shorten_skipped;
    /* --clear-reserved: records with reserved not 0 */
    uint64_t need_clear_reserved;
    uint64_t cleared_reserved;
    uint64_t reserved_long;     /* the name runs into reserved */
    /* Records with bad names left alone, as a file has that name. */
    uint64_t bad_name_file_exists;
    /* --prune-missing: announce and mail records without their file */
    uint64_t need_prune;
    uint64_t pruned;
    /* Spare space: how many records have each byte of fileheader_t not 0
     * (as read, before any repair), and the longest of the other strings. */
    uint64_t byte_nz[sizeof(fileheader_t)];
    int max_owner_len;
    int max_date_len;
    int max_title_len;
} section_stats_t;

/* The first record seen with each byte of fileheader_t not 0, as
 * "path #idx", to show where the rare ones come from. */
#define NZ_EXAMPLE_LEN 256
static char nz_example[sizeof(fileheader_t)][NZ_EXAMPLE_LEN];

typedef struct ino_node {
    dev_t dev;
    ino_t ino;
    struct ino_node *next;
} ino_node_t;

#define INO_HASH_SIZE 65536
static ino_node_t *ino_hash_table[INO_HASH_SIZE];

static int has_visited_dir(dev_t dev, ino_t ino)
{
    uint64_t h = (((uint64_t)dev << 32) ^ (uint64_t)ino);
    h ^= (h >> 16);
    h *= 0x85ebca6b;
    h ^= (h >> 13);
    size_t idx = (size_t)(h & (INO_HASH_SIZE - 1));

    for (ino_node_t *node = ino_hash_table[idx]; node; node = node->next) {
        if (node->dev == dev && node->ino == ino)
            return 1;
    }
    return 0;
}

static void mark_visited_dir(dev_t dev, ino_t ino)
{
    uint64_t h = (((uint64_t)dev << 32) ^ (uint64_t)ino);
    h ^= (h >> 16);
    h *= 0x85ebca6b;
    h ^= (h >> 13);
    size_t idx = (size_t)(h & (INO_HASH_SIZE - 1));

    ino_node_t *node = (ino_node_t *)malloc(sizeof(ino_node_t));
    if (!node)
        return;
    node->dev = dev;
    node->ino = ino;
    node->next = ino_hash_table[idx];
    ino_hash_table[idx] = node;
}

static int opt_threshold = FNLEN;   /* longest name that fits */
static int opt_verbose = 0;
static int opt_quiet = 0;
static int opt_all = 0;
static int opt_repair = 0;
static int opt_scan_boards = 0;
static int opt_scan_mail = 0;
static int opt_scan_man = 0;
static int opt_include_backup = 0;
static int opt_include_digest = 0;
static int opt_rename = 0;
static int opt_prune_missing = 0;
static int opt_clear_reserved = 0;
static char opt_bbshome[MAX_SCAN_PATH] = "";

/* --rename: the current .DIR's directory and the names its records use or
 * were given, so no new name is taken twice. */
static char cur_dirname[MAX_SCAN_PATH];
/* Whether the .DIR being scanned may be pruned: an announce list or a
 * mailbox, where entries are deleted rather than left as tombstones. */
static int cur_prunable;
static char (*cur_names)[NAME_SPAN];
static size_t cur_nnames, cur_cap;

static void print_usage(const char *progname)
{
    printf("Usage: %s [options] [path1 [path2 ...]]\n\n", progname);
    printf("Scan BBS .DIR files to detect filename lengths and optionally repair invalid filenames.\n");
    printf("Also reports the spare space in fileheader_t: the bytes no record sets, and how\n");
    printf("far filename[] could shrink; and warns of a legacy outgoing mail queue (out/.DIR).\n\n");
    printf("Options:\n");
    printf("  -b, --bbshome <dir>     Set BBS home directory (default: BBSHOME or auto-detect)\n");
    printf("  -t, --threshold <len>   Filename length threshold, without the NUL (default: %d,\n", FNLEN);
    printf("                            FNLEN; up to %d, LEGACY_FNSIZE)\n", LEGACY_FNSIZE);
    printf("  -r, --repair            Apply repairs to .DIR files (default is dry-run inspection):\n");
    printf("                            1. Bad path (., .., ./, ../, /...) -> tombstone\n");
    printf("                            2. Contains non-ASCII              -> tombstone\n");
    printf("                            3. Contains space                  -> tombstone\n");
    printf("                            4. Not NUL-terminated              -> tombstone\n");
    printf("                            5. 'non-exist' or 'non-xist'       -> tombstone\n");
    printf("                            6. Not a BBS file name (a letter, then letters, digits,\n");
    printf("                               '.', '_', '-'; e.g. '?@host')  -> tombstone\n");
    printf("                            *  All trailing bytes in filename are zeroed out\n");
    printf("                            A tombstone is what mbbsd leaves of a removed post: file\n");
    printf("                            name '.deleted', owner '-', title '(removed) <owner>'.\n");
    printf("                            2-6 are left alone (to fix by hand) if a file has that name.\n");
    printf("                            With nest.svc, the .DIR of boards (its cache) are not repaired\n");
    printf("      --rename            With -r, give names longer than the threshold the standard\n");
    printf("                            [MG].<time>.A.<hex> (the hex the AID already has), renaming\n");
    printf("                            the file and fixing the record; another hex (a new AID) if\n");
    printf("                            that name is taken. Other names are only reported.\n");
    printf("                            Backups (.DIR.old, ...) keep the old names.\n");
    printf("      --prune-missing     With -r, drop the records of announce (man) directories\n");
    printf("                            and mailboxes (home/) whose file or directory does not\n");
    printf("                            exist, whatever the name (or whose name is a bad path or\n");
    printf("                            not NUL-terminated), as the announce menu and the mailbox\n");
    printf("                            delete entries; instead of tombstones or renames.\n");
    printf("                            Boards are not pruned. Run mandex after.\n");
    printf("      --clear-reserved    Only zero fileheader_t.reserved (the %d bytes after\n",
           LEGACY_FNSIZE - FNSIZE);
    printf("                            filename, which older binaries read as the end of the\n");
    printf("                            name), nothing else; writes with -r. A name running\n");
    printf("                            into reserved is reported: rename it with --rename.\n");
    printf("      --boards            Scan only boards/ directory\n");
    printf("      --mail, --home      Scan only home/ (mailboxes) directory\n");
    printf("      --man, --announce   Scan only man/ and man/boards/ directory\n");
    printf("      --include-backup    Also scan backup files (.DIR.old, .DIR.x, .DIR.bak)\n");
    printf("      --include-digest    Also scan board digest files (.Names)\n");
    printf("  -v, --verbose           Verbose output (print each directory scanned)\n");
    printf("  -q, --quiet             Quiet mode (only print violations and summary)\n");
    printf("  -a, --all               Print all non-empty records encountered\n");
    printf("  -h, --help              Display this help message and exit\n\n");
    printf("Arguments:\n");
    printf("  [path ...]              Optional specific paths or .DIR files to scan.\n");
    printf("                          If omitted, scans boards/, home/, and man/ under BBS home.\n");
}

static void escape_str(char *dest, size_t dest_size, const char *src, size_t src_len)
{
    size_t d = 0;
    size_t s = 0;
    while (s < src_len && src[s] != '\0' && d + 5 < dest_size) {
        unsigned char c = (unsigned char)src[s];
        if (c >= 0x20 && c != '\\' && c != '\'') {
            dest[d++] = (char)c;
        } else if (c == '\\') {
            dest[d++] = '\\';
            dest[d++] = '\\';
        } else if (c == '\'') {
            dest[d++] = '\\';
            dest[d++] = '\'';
        } else {
            snprintf(&dest[d], 5, "\\x%02X", c);
            d += 4;
        }
        s++;
    }
    dest[d] = '\0';
}

static const char *fmt_num(uint64_t n, char buf[32])
{
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%" PRIu64, n);
    int len = (int)strlen(tmp);
    int commas = (len - 1) / 3;
    int out_len = len + commas;
    buf[out_len] = '\0';

    int r = out_len - 1;
    int count = 0;
    for (int i = len - 1; i >= 0; i--) {
        buf[r--] = tmp[i];
        count++;
        if (count == 3 && i > 0) {
            buf[r--] = ',';
            count = 0;
        }
    }
    return buf;
}

static void init_stats(section_stats_t *s)
{
    memset(s, 0, sizeof(*s));
    s->max_len = -1;
}

static void merge_stats(section_stats_t *dst, const section_stats_t *src)
{
    dst->total_dirs += src->total_dirs;
    dst->empty_dirs += src->empty_dirs;
    dst->total_records += src->total_records;
    dst->invalid_size_dirs += src->invalid_size_dirs;
    dst->count_gt_threshold += src->count_gt_threshold;
    dst->count_eq_threshold += src->count_eq_threshold;
    dst->need_repair_renamed += src->need_repair_renamed;
    dst->need_repair_trailing += src->need_repair_trailing;
    dst->repaired_renamed += src->repaired_renamed;
    dst->repaired_trailing += src->repaired_trailing;
    dst->need_clear_reserved += src->need_clear_reserved;
    dst->cleared_reserved += src->cleared_reserved;
    dst->reserved_long += src->reserved_long;
    dst->need_shorten += src->need_shorten;
    dst->shortened += src->shortened;
    dst->shorten_aid_changed += src->shorten_aid_changed;
    dst->shorten_skipped += src->shorten_skipped;
    dst->bad_name_file_exists += src->bad_name_file_exists;
    dst->need_prune += src->need_prune;
    dst->pruned += src->pruned;

    for (int i = 0; i <= NAME_SPAN; i++) {
        dst->len_hist[i] += src->len_hist[i];
    }
    for (size_t i = 0; i < sizeof(fileheader_t); i++) {
        dst->byte_nz[i] += src->byte_nz[i];
    }
    if (src->max_owner_len > dst->max_owner_len)
        dst->max_owner_len = src->max_owner_len;
    if (src->max_date_len > dst->max_date_len)
        dst->max_date_len = src->max_date_len;
    if (src->max_title_len > dst->max_title_len)
        dst->max_title_len = src->max_title_len;
    if (src->max_len > dst->max_len) {
        dst->max_len = src->max_len;
        memcpy(dst->max_len_fn, src->max_len_fn, sizeof(dst->max_len_fn));
        memcpy(dst->max_len_path, src->max_len_path, sizeof(dst->max_len_path));
        dst->max_len_idx = src->max_len_idx;
    }
}

static int is_dir_file_name(const char *name)
{
    if (strcmp(name, FN_DIR) == 0)
        return 1;
    if (opt_include_digest && strcmp(name, ".Names") == 0)
        return 1;
    if (opt_include_backup) {
        if (strcmp(name, ".DIR.old") == 0 ||
            strcmp(name, ".DIR.x") == 0 ||
            strcmp(name, ".DIR.bak") == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_bad_leading_path(const char *fn, size_t len)
{
    if (len == 0)
        return 0;

    /* Starts with '/' */
    if (fn[0] == '/')
        return 1;

    /* Starts with "./" */
    if (len >= 2 && fn[0] == '.' && fn[1] == '/')
        return 1;

    /* Starts with "../" */
    if (len >= 3 && fn[0] == '.' && fn[1] == '.' && fn[2] == '/')
        return 1;

    /* Fully matches "." */
    if (len == 1 && fn[0] == '.')
        return 1;

    /* Fully matches ".." */
    if (len == 2 && fn[0] == '.' && fn[1] == '.')
        return 1;

    return 0;
}

/* --rename: the names the records of the current .DIR use. */
static int cur_names_has(const char *name)
{
    for (size_t i = 0; i < cur_nnames; i++) {
        if (strncmp(cur_names[i], name, NAME_SPAN) == 0)
            return 1;
    }
    return 0;
}

static void cur_names_add(const char *name)
{
    if (cur_nnames == cur_cap) {
        size_t cap = cur_cap ? cur_cap * 2 : 1024;
        char (*more)[NAME_SPAN] = realloc(cur_names, cap * NAME_SPAN);
        if (!more)
            return;
        cur_names = more;
        cur_cap = cap;
    }
    strlcpy(cur_names[cur_nnames++], name, NAME_SPAN);
}

static void cur_names_load(int fd)
{
    fileheader_t buf[CHUNK_RECORDS];
    ssize_t n;
    off_t off = 0;

    cur_nnames = 0;
    while ((n = pread(fd, buf, sizeof(buf), off)) > 0) {
        size_t count = (size_t)n / sizeof(fileheader_t);
        for (size_t i = 0; i < count; i++) {
            char name[NAME_SPAN];
            memcpy(name, FH_NAME(&buf[i]), NAME_SPAN);
            name[NAME_SPAN - 1] = '\0';
            if (name[0])
                cur_names_add(name);
        }
        off += n;
    }
}

/* Names that could lead out of cur_dirname are never looked up. */
static int cur_dir_has_file(const char *name)
{
    char path[MAX_SCAN_PATH];
    struct stat st;

    if (!name[0] || strchr(name, '/') || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0)
        return 0;
    snprintf(path, sizeof(path), "%s/%s", cur_dirname, name);
    return lstat(path, &st) == 0;
}

/* [MG].<time>.A[.<any>] -> [MG].<time>.A.<hex>, keeping the AID's hex
 * (get_fhdr_stamp_hex()) unless taken, else the next free one (the AID
 * changes). Returns 0 if fn has another form or no hex is free. */
static int short_name(const char *fn, char *out, size_t sz, int *aid_changed)
{
    char *end;

    *aid_changed = 0;
    if ((fn[0] != 'M' && fn[0] != 'G') || fn[1] != '.' ||
        !isdigit((unsigned char)fn[2]))
        return 0;
    errno = 0;
    unsigned long ts = strtoul(fn + 2, &end, 10);
    if (errno || ts == 0 || ts > UINT32_MAX ||
        strncmp(end, ".A", 2) != 0 || (end[2] != '\0' && end[2] != '.'))
        return 0;

    unsigned int hex = get_fhdr_stamp_hex(fn);
    for (unsigned int i = 0; i <= 0xFFF; i++) {
        unsigned int h = (hex + i) & 0xFFF;
        snprintf(out, sz, "%c.%lu.A.%03X", fn[0], ts, h);
        if (strcmp(out, fn) != 0 && !cur_names_has(out) &&
            !cur_dir_has_file(out)) {
            *aid_changed = i != 0;
            return 1;
        }
    }
    return 0;
}

static int is_printable(const char *s, size_t n)
{
    for (size_t i = 0; i < n && s[i]; i++) {
        if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] >= 0x7F)
            return 0;
    }
    return 1;
}

/* Whether name is made of what the BBS names files of (as
 * bof_bbs_valid_post_file(), but any length: long names are --rename's). */
static int is_bbs_name(const char *name)
{
    if (!isalpha((unsigned char)name[0]))
        return 0;
    for (const char *p = name; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '.' && *p != '_' && *p != '-')
            return 0;
    }
    return 1;
}

/* Same tombstone as mbbsd's; an unprintable owner is kept out of the title. */
static void make_tombstone(fileheader_t *fh)
{
    const char *title = NULL;

    if (!is_printable(fh->owner, sizeof(fh->owner)) ||
        !memchr(fh->owner, '\0', sizeof(fh->owner)))
        title = STR_SAFEREMOVE_TITLE;
    memset(FH_NAME(fh), 0, NAME_SPAN);  /* reserved too */
    bof_files_set_tombstone(fh, title, "chkfnlen");
}

/* Whether path is in an announce (man) tree: it has a "man" directory that
 * is not a board or user named man (boards/m/man, home/m/man). */
static int is_announce_path(const char *path)
{
    static const char *const hashed[] = { "boards/", "home/" };

    for (const char *p = path; (p = strstr(p, "man/")) != NULL; p++) {
        if (p != path && p[-1] != '/')
            continue;
        int named = 0;
        /* "<hashed>X/" right before: a board or a user named man. */
        for (size_t k = 0; k < sizeof(hashed) / sizeof(hashed[0]); k++) {
            size_t n = strlen(hashed[k]);
            const char *q = p - 2 - n;  /* start of "<hashed>X/" */
            if (p - path >= (ptrdiff_t)(n + 2) && p[-1] == '/' &&
                strncmp(q, hashed[k], n) == 0 && (q == path || q[-1] == '/'))
                named = 1;
        }
        if (!named)
            return 1;
    }
    return 0;
}

/* Whether path is in a home directory (home/X/<userid>/...): mailboxes. */
static int is_mail_path(const char *path)
{
    for (const char *p = path; (p = strstr(p, "home/")) != NULL; p++) {
        if ((p == path || p[-1] == '/') && p[5] && p[5] != '/' &&
            p[6] == '/' && p[7] && p[7] != '/' && strchr(p + 7, '/'))
            return 1;
    }
    return 0;
}

/* Repairs are written at record_offset; *drop (--prune-missing) leaves the
 * record out, and the caller rewrites the .DIR. */
static void process_record(fileheader_t *fh, const char *dir_path, int rec_idx,
                           int fd, off_t record_offset, section_stats_t *stats,
                           int *drop)
{
    *drop = 0;
    stats->total_records++;

    /* Spare space, from the record as it is on disk. */
    const unsigned char *raw = (const unsigned char *)fh;
    for (size_t i = 0; i < sizeof(*fh); i++) {
        if (!raw[i])
            continue;
        if (!stats->byte_nz[i]++ && !nz_example[i][0])
            snprintf(nz_example[i], NZ_EXAMPLE_LEN, "%s #%d", dir_path, rec_idx);
    }
    int slen = (int)strnlen(fh->owner, sizeof(fh->owner));
    if (slen > stats->max_owner_len)
        stats->max_owner_len = slen;
    slen = (int)strnlen(fh->date, sizeof(fh->date));
    if (slen > stats->max_date_len)
        stats->max_date_len = slen;
    slen = (int)strnlen(fh->title, sizeof(fh->title));
    if (slen > stats->max_title_len)
        stats->max_title_len = slen;

    /* Save original filename for logging/diff */
    char orig_fn[NAME_SPAN + 1];
    memcpy(orig_fn, FH_NAME(fh), NAME_SPAN);
    orig_fn[NAME_SPAN] = '\0';

    char orig_escaped[NAME_SPAN * 4 + 1];
    escape_str(orig_escaped, sizeof(orig_escaped), FH_NAME(fh), NAME_SPAN);

    /* Check validity according to repair rules */
    int cond1_bad_path = 0;
    int cond2_non_ascii = 0;
    int cond3_has_space = 0;
    int cond4_unterminated = 0;
    int cond5_dirty_trailing = 0;
    int cond6_non_exist = 0;
    int cond7_bad_chars = 0;

    const char *nul_ptr = memchr(FH_NAME(fh), '\0', NAME_SPAN);
    size_t len = 0;

    if (!nul_ptr) {
        cond4_unterminated = 1;
        len = NAME_SPAN;
    } else {
        len = (size_t)(nul_ptr - FH_NAME(fh));
    }

    int need_repair = 0;

    // --clear-reserved does only this; other problems are left alone.
    if (opt_clear_reserved) {
        int dirty = 0;
        for (size_t i = FNSIZE; i < NAME_SPAN && !dirty; i++)
            dirty = FH_NAME(fh)[i] != '\0';
        if (dirty && len > FNLEN) {
            stats->reserved_long++;
            printf("[NEED-RENAME] %s #%d: '%s' (len=%zu) runs into reserved\n",
                   dir_path, rec_idx, orig_escaped, len);
            fflush(stdout);
        } else if (dirty && opt_repair) {
            memset(FH_NAME(fh) + FNSIZE, 0, NAME_SPAN - FNSIZE);
            if (pwrite(fd, fh, sizeof(*fh), record_offset) != (ssize_t)sizeof(*fh))
                fprintf(stderr, "[ERROR] Clearing reserved failed at %s #%d: %s\n",
                        dir_path, rec_idx, strerror(errno));
            else
                stats->cleared_reserved++;
        } else if (dirty) {
            stats->need_clear_reserved++;
        }
        if (dirty && len <= FNLEN && opt_verbose)
            printf("[%s] %s #%d: '%s'\n", opt_repair ? "CLEARED-RESERVED" :
                   "NEED-CLEAR-RESERVED", dir_path, rec_idx, orig_escaped);
        goto record_stats;
    }

    if (len > 0) {
        cond1_bad_path = is_bad_leading_path(FH_NAME(fh), len);
        if (strcmp(FH_NAME(fh), "non-exist") == 0 || strcmp(FH_NAME(fh), "non-xist") == 0)
            cond6_non_exist = 1;

        for (size_t i = 0; i < len; i++) {
            unsigned char c = (unsigned char)FH_NAME(fh)[i];
            if (c < 0x20 || c >= 0x7F)
                cond2_non_ascii = 1;
            if (c == ' ')
                cond3_has_space = 1;
        }

        /* Printable but not a name the BBS makes (a letter, then letters,
         * digits, '.', '_', '-'), e.g. a host name from a broken record.
         * Tombstones (.deleted, sync-dir's .d<stamp>) are names too. */
        if (!cond1_bad_path && !cond2_non_ascii && !cond3_has_space &&
            !cond4_unterminated && !cond6_non_exist &&
            !is_bbs_name(FH_NAME(fh)) &&
            strcmp(FH_NAME(fh), FN_SAFEDEL) != 0 &&
            !(FH_NAME(fh)[0] == '.' && FH_NAME(fh)[1] == 'd' &&
              isdigit((unsigned char)FH_NAME(fh)[2])))
            cond7_bad_chars = 1;

        /* Check if bytes after '\0' are dirty */
        if (!cond4_unterminated) {
            for (size_t i = len + 1; i < NAME_SPAN; i++) {
                if (FH_NAME(fh)[i] != '\0') {
                    cond5_dirty_trailing = 1;
                    break;
                }
            }
        }
    } else {
        /* Empty filename: check if bytes 1..NAME_SPAN-1 have non-zero garbage */
        for (size_t i = 1; i < NAME_SPAN; i++) {
            if (FH_NAME(fh)[i] != '\0') {
                cond5_dirty_trailing = 1;
                break;
            }
        }
    }

    int need_rename = (cond1_bad_path || cond2_non_ascii || cond3_has_space || cond4_unterminated || cond6_non_exist ||
                       cond7_bad_chars);
    need_repair = (need_rename || cond5_dirty_trailing);

    /* Dropped as the announce menu and the mailbox delete entries: no
     * tombstones there. Any name, as long as there is no such file. */
    if (opt_prune_missing && cur_prunable && len > 0 &&
        (cond1_bad_path || cond4_unterminated ||
         !cur_dir_has_file(FH_NAME(fh)))) {
        char title[sizeof(fh->title) * 4 + 1];
        escape_str(title, sizeof(title), fh->title, sizeof(fh->title));
        *drop = 1;
        if (opt_repair)
            stats->pruned++;
        else
            stats->need_prune++;
        printf("[%s] %s #%d\n", opt_repair ? "PRUNED" : "NEED-PRUNE",
               dir_path, rec_idx);
        printf("      '%s' (len=%zu): %s; title '%s'\n\n", orig_escaped, len,
               (cond1_bad_path || cond4_unterminated) ?
               "not a file name" : "no such file or directory", title);
        fflush(stdout);
        return;
    }

    const char *new_name = NULL;
    /* A bad name with a file: the post may still be there, so fix by hand. */
    int bad_name_has_file = need_rename && !cond1_bad_path &&
                            !cond4_unterminated &&
                            cur_dir_has_file(FH_NAME(fh));
    if (need_rename && bad_name_has_file) {
        stats->bad_name_file_exists++;
        printf("[MANUAL] %s #%d\n", dir_path, rec_idx);
        printf("      current: '%s' (len=%zu): a bad name, but %s/%s exists;"
               " left alone\n\n", orig_escaped, len, cur_dirname, orig_escaped);
        fflush(stdout);
        need_rename = 0;
        need_repair = cond5_dirty_trailing;
    }
    if (need_rename) {
        new_name = FN_SAFEDEL;
        fileheader_t orig = *fh;

        make_tombstone(fh);
        if (opt_repair) {
            if (pwrite(fd, fh, sizeof(*fh), record_offset) != (ssize_t)sizeof(*fh)) {
                fprintf(stderr, "[ERROR] Write repair failed at %s #%d: %s\n",
                        dir_path, rec_idx, strerror(errno));
                *fh = orig;
            } else {
                stats->repaired_renamed++;
            }
        } else {
            stats->need_repair_renamed++;
        }

        /* Build reason description */
        char reasons[128] = "";
        if (cond1_bad_path)
            strlcat(reasons, "bad path (., .., ./, ../, /...)", sizeof(reasons));
        if (cond2_non_ascii) {
            if (reasons[0]) strlcat(reasons, ", ", sizeof(reasons));
            strlcat(reasons, "non-ASCII bytes", sizeof(reasons));
        }
        if (cond3_has_space) {
            if (reasons[0]) strlcat(reasons, ", ", sizeof(reasons));
            strlcat(reasons, "contains space", sizeof(reasons));
        }
        if (cond4_unterminated) {
            if (reasons[0]) strlcat(reasons, ", ", sizeof(reasons));
            strlcat(reasons, "not NUL-terminated", sizeof(reasons));
        }
        if (cond6_non_exist) {
            if (reasons[0]) strlcat(reasons, ", ", sizeof(reasons));
            strlcat(reasons, "'non-exist' placeholder", sizeof(reasons));
        }
        if (cond7_bad_chars) {
            if (reasons[0]) strlcat(reasons, ", ", sizeof(reasons));
            strlcat(reasons, "not a BBS file name", sizeof(reasons));
        }

        char owner_escaped[sizeof(orig.owner) * 4 + 1];
        escape_str(owner_escaped, sizeof(owner_escaped), orig.owner, sizeof(orig.owner));
        if (opt_repair) {
            printf("[REPAIRED] %s #%d\n", dir_path, rec_idx);
            printf("      old: '%s' by '%s' (len=%zu, reason: %s)\n",
                   orig_escaped, owner_escaped, len, reasons);
            printf("      new: tombstone '%s' (owner '-', title of a removed post)\n\n",
                   new_name);
        } else {
            printf("[NEED-REPAIR] %s #%d\n", dir_path, rec_idx);
            printf("      current: '%s' by '%s' (len=%zu, reason: %s)\n",
                   orig_escaped, owner_escaped, len, reasons);
            printf("      action:  tombstone '%s' (owner '-', title of a removed post)\n\n",
                   new_name);
        }
        fflush(stdout);

        /* After repair/rename, new len is length of new_name */
        len = strlen(new_name);
    } else if (opt_rename && (int)len > opt_threshold && !cond4_unterminated &&
               !bad_name_has_file) {
        char short_fn[FNSIZE];
        int aid_changed = 0;

        if (!short_name(FH_NAME(fh), short_fn, sizeof(short_fn), &aid_changed)) {
            stats->shorten_skipped++;
            printf("[NO-RENAME] %s #%d\n", dir_path, rec_idx);
            printf("      current: '%s' (len=%zu): not [MG].<time>.A..., or no free"
                   " name; fix by hand\n\n", orig_escaped, len);
        } else {
            char old_path[MAX_SCAN_PATH], new_path[MAX_SCAN_PATH];
            int has_file = cur_dir_has_file(FH_NAME(fh));
            int ok = 1;

            snprintf(old_path, sizeof(old_path), "%s/%s", cur_dirname, FH_NAME(fh));
            snprintf(new_path, sizeof(new_path), "%s/%s", cur_dirname, short_fn);
            if (opt_repair) {
                fileheader_t updated = *fh;
                memset(FH_NAME(&updated), 0, NAME_SPAN);
                strlcpy(updated.filename, short_fn, sizeof(updated.filename));

                /* The file first; it goes back if the record write fails. */
                if (has_file && rename(old_path, new_path) != 0) {
                    fprintf(stderr, "[ERROR] Cannot rename %s to %s: %s\n",
                            old_path, new_path, strerror(errno));
                    ok = 0;
                } else if (pwrite(fd, &updated, sizeof(updated), record_offset) !=
                           (ssize_t)sizeof(updated)) {
                    fprintf(stderr, "[ERROR] Write rename failed at %s #%d: %s\n",
                            dir_path, rec_idx, strerror(errno));
                    if (has_file && rename(new_path, old_path) != 0)
                        fprintf(stderr, "[ERROR] Cannot rename %s back to %s: %s\n",
                                new_path, old_path, strerror(errno));
                    ok = 0;
                }
                if (ok) {
                    *fh = updated;
                    stats->shortened++;
                }
            } else {
                stats->need_shorten++;
            }
            if (ok) {
                cur_names_add(short_fn);
                if (aid_changed)
                    stats->shorten_aid_changed++;
                printf("[%s] %s #%d\n", opt_repair ? "RENAMED" : "NEED-RENAME",
                       dir_path, rec_idx);
                printf("      old: '%s' (len=%zu)%s\n", orig_escaped, len,
                       has_file ? "" : " (no such file: record only)");
                printf("      new: '%s'%s\n\n", short_fn,
                       aid_changed ? " (name taken: the AID changes)" : "");
                if (opt_repair) {
                    len = strlen(short_fn);
                    cond5_dirty_trailing = 0;   /* written whole */
                }
            }
        }
        need_repair = 1;    /* printed here */
        fflush(stdout);
    }
    if (!need_rename && cond5_dirty_trailing) {
        if (opt_repair) {
            if (len > 0)
                memset(FH_NAME(fh) + len + 1, 0, NAME_SPAN - len - 1);
            else
                memset(FH_NAME(fh) + 1, 0, NAME_SPAN - 1);

            if (pwrite(fd, fh, sizeof(*fh), record_offset) != (ssize_t)sizeof(*fh)) {
                fprintf(stderr, "[ERROR] Write repair trailing bytes failed at %s #%d: %s\n",
                        dir_path, rec_idx, strerror(errno));
            } else {
                stats->repaired_trailing++;
            }
        } else {
            stats->need_repair_trailing++;
        }

        if (opt_verbose) {
            printf("[%s] %s #%d: '%s' (zeroed dirty trailing bytes)\n",
                   opt_repair ? "REPAIRED-PAD" : "NEED-PAD", dir_path, rec_idx, orig_escaped);
            fflush(stdout);
        }
    }

record_stats: ;
    /* Record statistics for filename length */
    int final_null_term = (len < NAME_SPAN);
    if (len <= NAME_SPAN)
        stats->len_hist[len]++;
    else
        stats->len_hist[NAME_SPAN]++;

    if ((int)len > stats->max_len) {
        stats->max_len = (int)len;
        snprintf(stats->max_len_fn, sizeof(stats->max_len_fn), "%.*s", (int)NAME_SPAN, FH_NAME(fh));
        snprintf(stats->max_len_path, sizeof(stats->max_len_path), "%s", dir_path);
        stats->max_len_idx = rec_idx;
    }

    int is_gt = ((int)len > opt_threshold);
    int is_eq = ((int)len == opt_threshold);

    if (is_gt)
        stats->count_gt_threshold++;
    if (is_eq)
        stats->count_eq_threshold++;

    /* If not already printed by repair and matches threshold/all */
    if (!need_repair && (is_gt || is_eq || opt_all)) {
        char fn_escaped[NAME_SPAN * 4 + 1];
        escape_str(fn_escaped, sizeof(fn_escaped), FH_NAME(fh), NAME_SPAN);

        char owner[sizeof(fh->owner) + 1];
        snprintf(owner, sizeof(owner), "%.*s", (int)sizeof(fh->owner), fh->owner);

        char date[sizeof(fh->date) + 1];
        snprintf(date, sizeof(date), "%.*s", (int)sizeof(fh->date), fh->date);

        char title[sizeof(fh->title) + 1];
        snprintf(title, sizeof(title), "%.*s", (int)sizeof(fh->title), fh->title);

        const char *tag = is_gt ? "[>THRESH]" : (is_eq ? "[==THRESH]" : "[INFO]");
        if (opt_threshold == 20) {
            tag = is_gt ? "[>20]" : (is_eq ? "[==20]" : "[INFO]");
        }

        printf("%s %s #%d\n", tag, dir_path, rec_idx);
        printf("      filename: '%s' (len=%zu, null_term=%s)\n",
               fn_escaped, len, final_null_term ? "yes" : "NO");
        printf("      owner:    '%-14s'  date: '%-6s'\n", owner, date);
        printf("      title:    '%s'\n\n", title);
        fflush(stdout);
    }
}

static void scan_dir_file(const char *dir_path, section_stats_t *stats)
{
    struct stat st;
    if (stat(dir_path, &st) != 0) {
        if (opt_verbose)
            fprintf(stderr, "[WARN] Cannot stat %s: %s\n", dir_path, strerror(errno));
        return;
    }

    stats->total_dirs++;

    if (opt_verbose) {
        printf("  Scanning %s (%" PRIu64 " bytes)...\n", dir_path, (uint64_t)st.st_size);
        fflush(stdout);
    }

    if (st.st_size == 0) {
        stats->empty_dirs++;
        return;
    }

    if (st.st_size % (off_t)sizeof(fileheader_t) != 0) {
        stats->invalid_size_dirs++;
        if (!opt_quiet) {
            fprintf(stderr, "[WARN] %s: file size %" PRIu64 " is not a multiple of %zu (partial record?)\n",
                    dir_path, (uint64_t)st.st_size, sizeof(fileheader_t));
        }
    }

    /* With nest.svc (BOF_FILES_ARE_CACHE) the lists of the boards are only
     * its cache, keyed by the file names: renaming them here would part
     * them from nest.svc. Inspected only; fix them in nest.svc instead. */
    int saved_repair = opt_repair;
    char board[BRDNAME_SIZE];
    if (BOF_FILES_ARE_CACHE && opt_repair && bfile_to_brdname(dir_path, board, sizeof(board))) {
        if (!opt_quiet)
            fprintf(stderr, "[WARN] %s: not repaired: with nest.svc the "
                    "lists of boards change only through BoF\n", dir_path);
        opt_repair = 0;
    }

    int flags = opt_repair ? O_RDWR : O_RDONLY;
    int fd = open(dir_path, flags);
    if (fd < 0 && opt_repair && (errno == EACCES || errno == EROFS)) {
        fprintf(stderr, "[WARN] Cannot open %s for write (%s), falling back to read-only\n",
                dir_path, strerror(errno));
        fd = open(dir_path, O_RDONLY);
    }

    if (fd < 0) {
        if (!opt_quiet)
            fprintf(stderr, "[WARN] Cannot open %s: %s\n", dir_path, strerror(errno));
        opt_repair = saved_repair;
        return;
    }

    snprintf(cur_dirname, sizeof(cur_dirname), "%s", dir_path);
    char *slash = strrchr(cur_dirname, '/');
    if (slash)
        *slash = '\0';
    else
        snprintf(cur_dirname, sizeof(cur_dirname), ".");
    /* Writers of .DIR (append_record) take flock; so does a repair. */
    if (opt_repair && flock(fd, LOCK_EX) != 0)
        fprintf(stderr, "[WARN] Cannot lock %s: %s\n", dir_path, strerror(errno));
    if (opt_rename)
        cur_names_load(fd);

    /* Pruning moves kept records down and truncates: whole records only. */
    cur_prunable = is_announce_path(dir_path) || is_mail_path(dir_path);
    if (opt_prune_missing && cur_prunable &&
        st.st_size % (off_t)sizeof(fileheader_t) != 0) {
        fprintf(stderr, "[WARN] %s: not pruned: not a whole number of records\n",
                dir_path);
        cur_prunable = 0;
    }

    fileheader_t buf[CHUNK_RECORDS];
    ssize_t bytes_read;
    int rec_idx = 0;
    off_t kept = 0;     /* records kept so far, for --prune-missing */
    int dropped = 0;

    while ((bytes_read = read(fd, buf, sizeof(buf))) > 0) {
        size_t count = (size_t)bytes_read / sizeof(fileheader_t);
        for (size_t i = 0; i < count; i++) {
            int drop = 0;
            rec_idx++;
            /* At or before where it was read: never over unread records. */
            off_t offset = kept * (off_t)sizeof(fileheader_t);
            process_record(&buf[i], dir_path, rec_idx, fd, offset, stats, &drop);
            if (drop) {
                dropped++;
                continue;
            }
            if (opt_repair && dropped &&
                pwrite(fd, &buf[i], sizeof(buf[i]), offset) != (ssize_t)sizeof(buf[i])) {
                fprintf(stderr, "[ERROR] Cannot move %s #%d: %s\n",
                        dir_path, rec_idx, strerror(errno));
            }
            kept++;
        }
    }

    if (opt_repair && dropped &&
        ftruncate(fd, kept * (off_t)sizeof(fileheader_t)) != 0) {
        fprintf(stderr, "[ERROR] Cannot cut %s after %" PRIu64 " records: %s\n",
                dir_path, (uint64_t)kept, strerror(errno));
    }

    close(fd);
    opt_repair = saved_repair;
}

static void crawl_path_recursive(const char *path, section_stats_t *stats, int depth)
{
    if (depth > MAX_RECURSE_DEPTH) {
        if (opt_verbose)
            fprintf(stderr, "[WARN] Max recursion depth exceeded at: %s\n", path);
        return;
    }

    struct stat st;
    /* Use stat() to follow symlinks to directories/mounts */
    if (stat(path, &st) != 0)
        return;

    if (S_ISREG(st.st_mode)) {
        if (is_dir_file_name(path) || strstr(path, FN_DIR) != NULL) {
            scan_dir_file(path, stats);
        }
        return;
    }

    if (!S_ISDIR(st.st_mode))
        return;

    /* Prevent circular symlink loops or duplicate directory traversal */
    if (has_visited_dir(st.st_dev, st.st_ino))
        return;
    mark_visited_dir(st.st_dev, st.st_ino);

    DIR *dir = opendir(path);
    if (!dir) {
        if (opt_verbose)
            fprintf(stderr, "[WARN] Cannot open directory %s: %s\n", path, strerror(errno));
        return;
    }

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        char subpath[MAX_SCAN_PATH];
        int n = snprintf(subpath, sizeof(subpath), "%s/%s", path, de->d_name);
        if (n < 0 || n >= (int)sizeof(subpath)) {
            fprintf(stderr, "[WARN] Path too long: %s/%s\n", path, de->d_name);
            continue;
        }

#ifdef _DIRENT_HAVE_D_TYPE
        if (de->d_type == DT_REG) {
            if (is_dir_file_name(de->d_name)) {
                scan_dir_file(subpath, stats);
            }
            continue;
        } else if (de->d_type == DT_DIR) {
            crawl_path_recursive(subpath, stats, depth + 1);
            continue;
        }
#endif

        struct stat cst;
        /* stat() resolves symlinks to directories and files */
        if (stat(subpath, &cst) != 0)
            continue;

        if (S_ISDIR(cst.st_mode)) {
            crawl_path_recursive(subpath, stats, depth + 1);
        } else if (S_ISREG(cst.st_mode)) {
            if (is_dir_file_name(de->d_name)) {
                scan_dir_file(subpath, stats);
            }
        }
    }

    closedir(dir);
}

static void print_summary_table(const section_stats_t *boards,
                                const section_stats_t *mail,
                                const section_stats_t *man,
                                const section_stats_t *custom,
                                const section_stats_t *overall)
{
    char b1[32], b2[32], b3[32], b4[32];

    printf("================================================================================\n");
    printf("                               SCAN SUMMARY\n");
    printf("================================================================================\n");
    printf("Threshold: length > %d (and length == %d)\n", opt_threshold, opt_threshold);
    printf("Mode:      %s\n\n", opt_repair ? "REPAIR (in-place modification)" : "DRY-RUN (inspection only)");

    printf("%-20s %15s %18s %11s %11s\n",
           "Section", "Dirs Scanned", "Records Scanned", "len > TH", "len == TH");
    printf("--------------------------------------------------------------------------------\n");

    if (boards->total_dirs > 0) {
        printf("%-20s %15s %18s %11s %11s\n",
               "Boards",
               fmt_num(boards->total_dirs, b1),
               fmt_num(boards->total_records, b2),
               fmt_num(boards->count_gt_threshold, b3),
               fmt_num(boards->count_eq_threshold, b4));
    }
    if (mail->total_dirs > 0) {
        printf("%-20s %15s %18s %11s %11s\n",
               "Mail (Home)",
               fmt_num(mail->total_dirs, b1),
               fmt_num(mail->total_records, b2),
               fmt_num(mail->count_gt_threshold, b3),
               fmt_num(mail->count_eq_threshold, b4));
    }
    if (man->total_dirs > 0) {
        printf("%-20s %15s %18s %11s %11s\n",
               "Announce (Man)",
               fmt_num(man->total_dirs, b1),
               fmt_num(man->total_records, b2),
               fmt_num(man->count_gt_threshold, b3),
               fmt_num(man->count_eq_threshold, b4));
    }
    if (custom->total_dirs > 0) {
        printf("%-20s %15s %18s %11s %11s\n",
               "Custom Paths",
               fmt_num(custom->total_dirs, b1),
               fmt_num(custom->total_records, b2),
               fmt_num(custom->count_gt_threshold, b3),
               fmt_num(custom->count_eq_threshold, b4));
    }

    printf("--------------------------------------------------------------------------------\n");
    printf("%-20s %15s %18s %11s %11s\n\n",
           "Total",
           fmt_num(overall->total_dirs, b1),
           fmt_num(overall->total_records, b2),
           fmt_num(overall->count_gt_threshold, b3),
           fmt_num(overall->count_eq_threshold, b4));

    if (overall->max_len >= 0) {
        printf("Max filename length observed: %d\n", overall->max_len);
        if (overall->max_len_fn[0]) {
            printf("  Example: '%s' in %s #%d\n",
                   overall->max_len_fn, overall->max_len_path, overall->max_len_idx);
        }
    } else {
        printf("No records found.\n");
    }

    printf("\nFilename Length Distribution:\n");
    printf("  Length          Count     Percentage\n");
    printf("  ------------------------------------\n");
    for (int i = 0; i <= NAME_SPAN; i++) {
        if (overall->len_hist[i] > 0) {
            double pct = overall->total_records ?
                ((double)overall->len_hist[i] * 100.0 / (double)overall->total_records) : 0.0;
            printf("  %6d %14s %13.4f %%\n",
                   i, fmt_num(overall->len_hist[i], b1), pct);
        }
    }
    printf("  ------------------------------------\n");
    printf("  Total:  %14s      100.0000 %%\n\n", fmt_num(overall->total_records, b1));

    uint64_t total_issues = overall->need_repair_renamed + overall->need_repair_trailing +
                            overall->repaired_renamed + overall->repaired_trailing;

    if (total_issues > 0) {
        printf("Repair Statistics:\n");
        if (opt_repair) {
            printf("  Turned into tombstones:       %14s\n", fmt_num(overall->repaired_renamed, b1));
            printf("  Zeroed dirty trailing bytes:  %14s\n", fmt_num(overall->repaired_trailing, b2));
            printf("  Total records repaired:       %14s\n\n",
                   fmt_num(overall->repaired_renamed + overall->repaired_trailing, b3));
        } else {
            printf("  Records needing tombstones:   %14s\n", fmt_num(overall->need_repair_renamed, b1));
            printf("  Records with dirty padding:   %14s\n", fmt_num(overall->need_repair_trailing, b2));
            printf("  Total records needing repair: %14s\n",
                   fmt_num(overall->need_repair_renamed + overall->need_repair_trailing, b3));
            printf("  NOTE: Run with -r or --repair to apply fixes to .DIR files.\n\n");
        }
    }

    if (opt_clear_reserved) {
        printf("Reserved (--clear-reserved, the %d bytes after filename):\n",
               LEGACY_FNSIZE - FNSIZE);
        printf("  %-28s %14s\n", opt_repair ? "Cleared:" : "To clear:",
               fmt_num(opt_repair ? overall->cleared_reserved :
                       overall->need_clear_reserved, b1));
        printf("  %-28s %14s\n\n", "Names running into it:",
               fmt_num(overall->reserved_long, b2));
        if (!opt_repair && overall->need_clear_reserved)
            printf("  NOTE: Run with -r to clear them.\n\n");
        if (overall->reserved_long)
            printf("  NOTE: Rename those first (--rename -r), then clear.\n\n");
    }

    if (overall->bad_name_file_exists > 0)
        printf("Bad names left alone as a file has them ([MANUAL]): %s\n\n",
               fmt_num(overall->bad_name_file_exists, b1));

    if (opt_prune_missing) {
        printf("Prune (--prune-missing, announce and mail records without their file):\n");
        printf("  %-28s %14s\n\n", opt_repair ? "Dropped:" : "To drop:",
               fmt_num(opt_repair ? overall->pruned : overall->need_prune, b1));
        if (opt_repair && overall->pruned)
            printf("  NOTE: run mandex if announce lists were pruned.\n\n");
    }

    if (opt_rename) {
        printf("Rename (--rename, names longer than %d):\n", opt_threshold);
        printf("  %-28s %14s\n", opt_repair ? "Renamed:" : "To rename:",
               fmt_num(opt_repair ? overall->shortened : overall->need_shorten, b1));
        printf("  %-28s %14s\n", "  of which the AID changes:",
               fmt_num(overall->shorten_aid_changed, b2));
        printf("  %-28s %14s\n\n", "Cannot rename ([NO-RENAME]):",
               fmt_num(overall->shorten_skipped, b3));
        if (opt_repair && overall->shortened)
            printf("  NOTE: backups (.DIR.old, .DIR.bak, ...) still have the old names.\n\n");
    }

    if (overall->count_gt_threshold == 0 && overall->count_eq_threshold == 0) {
        printf("RESULT: [PASS] All filenames are within %d bytes (0 > %d, 0 == %d).\n",
               opt_threshold - 1, opt_threshold, opt_threshold);
    } else {
        printf("RESULT: [WARN] Potential issues found: %" PRIu64 " records > %d, %" PRIu64 " records == %d.\n",
               overall->count_gt_threshold, opt_threshold,
               overall->count_eq_threshold, opt_threshold);
    }
    printf("================================================================================\n");
}

/* Spare space report: string bytes past the longest value and its NUL, if
 * never set, could hold something else. */
typedef struct {
    const char *name;
    size_t off, size;
    int kind;           /* FIELD_* */
} field_t;

enum { FIELD_STRING, FIELD_NUMBER, FIELD_PAD };

#define FIELD(m, k) { #m, offsetof(fileheader_t, m), \
                      sizeof(((fileheader_t *)0)->m), k }

static const field_t fhdr_fields[] = {
    FIELD(filename, FIELD_STRING),
    FIELD(reserved, FIELD_PAD),
    FIELD(modified, FIELD_NUMBER),
    FIELD(score,    FIELD_NUMBER),
    FIELD(owner,    FIELD_STRING),
    FIELD(date,     FIELD_STRING),
    FIELD(title,    FIELD_STRING),
    FIELD(pad2,     FIELD_PAD),
    FIELD(multi,    FIELD_NUMBER),
    FIELD(filemode, FIELD_NUMBER),
    FIELD(comments, FIELD_NUMBER),
};

static int field_max_len(const field_t *f, const section_stats_t *s)
{
    if (f->off == offsetof(fileheader_t, filename))
        return s->max_len;
    if (f->off == offsetof(fileheader_t, owner))
        return s->max_owner_len;
    if (f->off == offsetof(fileheader_t, date))
        return s->max_date_len;
    if (f->off == offsetof(fileheader_t, title))
        return s->max_title_len;
    return -1;
}

/* Prints never-set bytes in [from, to) as ranges; returns how many. */
static size_t print_zero_ranges(const section_stats_t *s, size_t from, size_t to)
{
    size_t n = 0;
    for (size_t i = from; i < to; i++) {
        if (s->byte_nz[i])
            continue;
        size_t j = i;
        while (j + 1 < to && !s->byte_nz[j + 1])
            j++;
        if (j == i)
            printf(" [%zu]", i - from);
        else
            printf(" [%zu..%zu]", i - from, j - from);
        n += j - i + 1;
        i = j;
    }
    return n;
}

static void print_space_report(const section_stats_t *s)
{
    char b1[32], b2[32];
    size_t spare_total = 0;

    if (!s->total_records)
        return;

    printf("\n================================================================================\n");
    printf("                     SPARE SPACE IN fileheader_t (%zu bytes)\n",
           sizeof(fileheader_t));
    printf("================================================================================\n");
    printf("Bytes counted as read, before any repair (-r zeroes dirty filename tails).\n\n");
    printf("%-9s %4s %4s %7s %6s  %s\n",
           "Field", "Off", "Size", "MaxLen", "Spare", "Bytes never set (index in field)");
    printf("--------------------------------------------------------------------------------\n");

    for (size_t k = 0; k < sizeof(fhdr_fields) / sizeof(fhdr_fields[0]); k++) {
        const field_t *f = &fhdr_fields[k];
        int maxlen = field_max_len(f, s);
        char maxlen_s[16] = "-";
        size_t spare = 0;

        if (f->kind == FIELD_STRING) {
            snprintf(maxlen_s, sizeof(maxlen_s), "%d", maxlen);
            /* The tail never set, after the longest value and its NUL. */
            size_t keep = (size_t)maxlen + 1;
            for (size_t i = f->size; i > keep && !s->byte_nz[f->off + i - 1]; i--)
                spare++;
        } else if (f->kind == FIELD_PAD) {
            spare = 1;
            for (size_t i = 0; i < f->size; i++) {
                if (s->byte_nz[f->off + i]) {
                    spare = 0;
                    break;
                }
            }
            if (spare)
                spare = f->size;
        }
        spare_total += spare;

        printf("%-9s %4zu %4zu %7s %6zu ", f->name, f->off, f->size, maxlen_s, spare);
        if (!print_zero_ranges(s, f->off, f->off + f->size))
            printf(" (none)");
        if (f->kind == FIELD_NUMBER)
            printf("  (a number: zero bytes only mean small values)");
        printf("\n");
    }
    printf("--------------------------------------------------------------------------------\n");
    printf("Spare bytes (string tails past the longest value + NUL, pads): %zu\n\n",
           spare_total);

    /* Bytes that are set, but rarely: maybe junk worth cleaning up. */
    int rare = 0;
    for (size_t k = 0; k < sizeof(fhdr_fields) / sizeof(fhdr_fields[0]); k++) {
        const field_t *f = &fhdr_fields[k];
        if (f->kind == FIELD_NUMBER)
            continue;
        int maxlen = field_max_len(f, s);
        size_t from = f->kind == FIELD_STRING ? (size_t)maxlen + 1 : 0;
        for (size_t i = from; i < f->size; i++) {
            uint64_t nz = s->byte_nz[f->off + i];
            if (!nz || nz * 1000 > s->total_records)
                continue;
            if (!rare++)
                printf("Rarely set bytes past the values (< 0.1%% of records; junk?):\n");
            printf("  %s[%zu]: %s records, e.g. %s\n", f->name, i, fmt_num(nz, b1),
                   nz_example[f->off + i]);
        }
    }
    if (rare)
        printf("\n");

    /* Names in N bytes (their length + 1), of the LEGACY_FNSIZE they took. */
    printf("Names in N of the %d bytes filename[] was (names need len + 1;"
           " FNSIZE is %d):\n", NAME_SPAN, FNSIZE);
    printf("  %4s %14s %12s\n", "N", "Not fitting", "Freed bytes");
    for (int n = NAME_SPAN - 2; n >= 16; n -= 2) {
        uint64_t over = 0;
        for (int len = n; len <= NAME_SPAN; len++)
            over += s->len_hist[len];
        printf("  %4d %14s %12d%s%s\n", n, fmt_num(over, b2), NAME_SPAN - n,
               over ? "" : "  OK", n == FNSIZE ? "  <- FNSIZE" : "");
    }
    if (s->max_len >= 0) {
        printf("  The longest name (%d) needs filename[%d]: up to %d bytes could be freed.\n",
               s->max_len, s->max_len + 1, NAME_SPAN - (s->max_len + 1));
    }
    printf("================================================================================\n");
}

/* A leftover pre-mail.svc queue (out/.DIR of MailQueue) holds filepath
 * "out/M.<time>.<pid>.A" (up to 26 chars), which a shorter FNSIZE broke. */
#define LEGACY_MAILQUEUE_SIZE 216   /* sizeof(MailQueue) on 64-bit */

static int check_legacy_mailqueue(const char *bbshome)
{
    char path[MAX_SCAN_PATH];
    struct stat st;

    snprintf(path, sizeof(path), "%s/out/" FN_DIR, bbshome);
    if (stat(path, &st) != 0 || st.st_size == 0)
        return 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    int n = 0, maxlen = 0;
    char rec[LEGACY_MAILQUEUE_SIZE], longest[LEGACY_FNSIZE + 1] = "";
    while (read(fd, rec, sizeof(rec)) == (ssize_t)sizeof(rec)) {
        int len = (int)strnlen(rec, LEGACY_FNSIZE);
        n++;
        if (len > maxlen) {
            maxlen = len;
            snprintf(longest, sizeof(longest), "%.*s", len, rec);
        }
    }
    close(fd);

    printf("\n[WARN] %s: %" PRIu64 " bytes, %d legacy outgoing mail queue records\n",
           path, (uint64_t)st.st_size, n);
    printf("       (MailQueue, not fileheader_t; size %d each%s).\n",
           LEGACY_MAILQUEUE_SIZE,
           st.st_size % LEGACY_MAILQUEUE_SIZE ? ", but the file is not a multiple" : "");
    printf("       Longest filepath: %d bytes ('%s'). outmail is gone (mail.svc sends\n",
           maxlen, longest);
    printf("       mail now): send or remove them; FNSIZE (%d) cannot hold them.\n", FNSIZE);
    return n;
}

int main(int argc, char **argv)
{
    static struct option long_options[] = {
        {"bbshome",        required_argument, 0, 'b'},
        {"threshold",      required_argument, 0, 't'},
        {"repair",         no_argument,       0, 'r'},
        {"boards",         no_argument,       0, 1001},
        {"mail",           no_argument,       0, 1002},
        {"home",           no_argument,       0, 1002},
        {"man",            no_argument,       0, 1003},
        {"announce",       no_argument,       0, 1003},
        {"include-backup", no_argument,       0, 1004},
        {"include-digest", no_argument,       0, 1005},
        {"rename",         no_argument,       0, 1006},
        {"prune-missing",  no_argument,       0, 1007},
        {"clear-reserved", no_argument,       0, 1008},
        {"verbose",        no_argument,       0, 'v'},
        {"quiet",          no_argument,       0, 'q'},
        {"all",            no_argument,       0, 'a'},
        {"help",           no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "b:t:rvqah", long_options, NULL)) != -1) {
        switch (c) {
        case 'b':
            snprintf(opt_bbshome, sizeof(opt_bbshome), "%s", optarg);
            break;
        case 't':
            opt_threshold = atoi(optarg);
            if (opt_threshold < 1 || opt_threshold > NAME_SPAN) {
                fprintf(stderr, "Error: threshold must be between 1 and %d\n", NAME_SPAN);
                return 2;
            }
            break;
        case 'r':
            opt_repair = 1;
            break;
        case 1001:
            opt_scan_boards = 1;
            break;
        case 1002:
            opt_scan_mail = 1;
            break;
        case 1003:
            opt_scan_man = 1;
            break;
        case 1004:
            opt_include_backup = 1;
            break;
        case 1005:
            opt_include_digest = 1;
            break;
        case 1006:
            opt_rename = 1;
            break;
        case 1007:
            opt_prune_missing = 1;
            break;
        case 1008:
            opt_clear_reserved = 1;
            break;
        case 'v':
            opt_verbose = 1;
            opt_quiet = 0;
            break;
        case 'q':
            opt_quiet = 1;
            opt_verbose = 0;
            break;
        case 'a':
            opt_all = 1;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 2;
        }
    }

    section_stats_t boards_stats, mail_stats, man_stats, custom_stats, overall_stats;
    init_stats(&boards_stats);
    init_stats(&mail_stats);
    init_stats(&man_stats);
    init_stats(&custom_stats);
    init_stats(&overall_stats);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    if (optind < argc) {
        /* Scan specific paths passed by the user */
        for (int i = optind; i < argc; i++) {
            if (!opt_quiet) {
                printf("==> Scanning path: %s ...\n", argv[i]);
                fflush(stdout);
            }
            crawl_path_recursive(argv[i], &custom_stats, 0);
        }
        merge_stats(&overall_stats, &custom_stats);
    } else {
        /* Determine BBSHOME */
        if (opt_bbshome[0] == '\0') {
            if (access("boards", F_OK) == 0) {
                snprintf(opt_bbshome, sizeof(opt_bbshome), ".");
            } else if (access(BBSHOME "/boards", F_OK) == 0) {
                snprintf(opt_bbshome, sizeof(opt_bbshome), "%s", BBSHOME);
            } else if (access("/v/bbshome/boards", F_OK) == 0) {
                snprintf(opt_bbshome, sizeof(opt_bbshome), "/v/bbshome");
            } else if (access("/home/bbs/boards", F_OK) == 0) {
                snprintf(opt_bbshome, sizeof(opt_bbshome), "/home/bbs");
            } else {
                snprintf(opt_bbshome, sizeof(opt_bbshome), "%s", BBSHOME);
            }
        }

        /* Default to scanning boards, mail, and man if none selected */
        if (!opt_scan_boards && !opt_scan_mail && !opt_scan_man) {
            opt_scan_boards = 1;
            opt_scan_mail = 1;
            opt_scan_man = 1;
        }

        if (!opt_quiet) {
            printf("BBS Home: %s\n", opt_bbshome);
            printf("Threshold: > %d (flagging == %d as well)\n", opt_threshold, opt_threshold);
            printf("Mode: %s\n\n", opt_repair ? "REPAIR (in-place modification)" : "DRY-RUN (inspection only)");
        }

        char path_buf[MAX_SCAN_PATH];

        if (opt_scan_boards) {
            snprintf(path_buf, sizeof(path_buf), "%s/boards", opt_bbshome);
            if (!opt_quiet) {
                printf("==> Scanning Boards (%s) ...\n", path_buf);
                fflush(stdout);
            }
            crawl_path_recursive(path_buf, &boards_stats, 0);
            merge_stats(&overall_stats, &boards_stats);
            if (!opt_quiet) {
                char s_dir[32], s_rec[32];
                printf("    Scanned %s .DIR files, %s records.\n",
                       fmt_num(boards_stats.total_dirs, s_dir),
                       fmt_num(boards_stats.total_records, s_rec));
            }
        }

        if (opt_scan_mail) {
            snprintf(path_buf, sizeof(path_buf), "%s/home", opt_bbshome);
            if (!opt_quiet) {
                printf("==> Scanning Mail (%s) ...\n", path_buf);
                fflush(stdout);
            }
            crawl_path_recursive(path_buf, &mail_stats, 0);
            merge_stats(&overall_stats, &mail_stats);
            if (!opt_quiet) {
                char s_dir[32], s_rec[32];
                printf("    Scanned %s .DIR files, %s records.\n",
                       fmt_num(mail_stats.total_dirs, s_dir),
                       fmt_num(mail_stats.total_records, s_rec));
            }
        }

        if (opt_scan_man) {
            snprintf(path_buf, sizeof(path_buf), "%s/man", opt_bbshome);
            if (!opt_quiet) {
                printf("==> Scanning Announce / Man (%s) ...\n", path_buf);
                fflush(stdout);
            }
            crawl_path_recursive(path_buf, &man_stats, 0);

            /* Also check man/boards if not already under man or as a separate mount */
            snprintf(path_buf, sizeof(path_buf), "%s/man/boards", opt_bbshome);
            if (access(path_buf, F_OK) == 0) {
                crawl_path_recursive(path_buf, &man_stats, 0);
            }

            merge_stats(&overall_stats, &man_stats);
            if (!opt_quiet) {
                char s_dir[32], s_rec[32];
                printf("    Scanned %s .DIR files, %s records.\n",
                       fmt_num(man_stats.total_dirs, s_dir),
                       fmt_num(man_stats.total_records, s_rec));
            }
        }
        if (!opt_quiet)
            printf("\n");
    }

    print_summary_table(&boards_stats, &mail_stats, &man_stats, &custom_stats, &overall_stats);
    print_space_report(&overall_stats);
    if (optind >= argc)
        check_legacy_mailqueue(opt_bbshome);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("Scan took %.1f seconds.\n",
           (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9);

    if (overall_stats.count_gt_threshold > 0 ||
        overall_stats.need_repair_renamed > 0 ||
        overall_stats.need_repair_trailing > 0)
        return 1;
    return 0;
}
