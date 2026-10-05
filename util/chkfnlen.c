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

typedef struct {
    uint64_t total_dirs;
    uint64_t empty_dirs;
    uint64_t total_records;
    uint64_t invalid_size_dirs;
    uint64_t count_gt_threshold;
    uint64_t count_eq_threshold;
    uint64_t len_hist[FNLEN + 1];
    int max_len;
    char max_len_fn[FNLEN + 1];
    char max_len_path[MAX_SCAN_PATH];
    int max_len_idx;

    /* Repair statistics */
    uint64_t need_repair_renamed;
    uint64_t need_repair_trailing;
    uint64_t repaired_renamed;
    uint64_t repaired_trailing;
} section_stats_t;

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

static int opt_threshold = 20;
static int opt_verbose = 0;
static int opt_quiet = 0;
static int opt_all = 0;
static int opt_repair = 0;
static int opt_scan_boards = 0;
static int opt_scan_mail = 0;
static int opt_scan_man = 0;
static int opt_include_backup = 0;
static int opt_include_digest = 0;
static char opt_bbshome[MAX_SCAN_PATH] = "";

static void print_usage(const char *progname)
{
    printf("Usage: %s [options] [path1 [path2 ...]]\n\n", progname);
    printf("Scan BBS .DIR files to detect filename lengths and optionally repair invalid filenames.\n\n");
    printf("Options:\n");
    printf("  -b, --bbshome <dir>     Set BBS home directory (default: BBSHOME or auto-detect)\n");
    printf("  -t, --threshold <len>   Filename length threshold to check (default: 20)\n");
    printf("  -r, --repair            Apply repairs to .DIR files (default is dry-run inspection):\n");
    printf("                            1. Bad path (., .., ./, ../, /...) -> rename to '.deleted'\n");
    printf("                            2. Contains non-ASCII              -> rename to '.deleted'\n");
    printf("                            3. Contains space                  -> rename to '.deleted'\n");
    printf("                            4. Not NUL-terminated              -> rename to '.deleted'\n");
    printf("                            5. 'non-exist' or 'non-xist'       -> rename to '.deleted'\n");
    printf("                            *  All trailing bytes in filename are zeroed out\n");
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

    for (int i = 0; i <= FNLEN; i++) {
        dst->len_hist[i] += src->len_hist[i];
    }
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

static void process_record(fileheader_t *fh, const char *dir_path, int rec_idx,
                           int fd, off_t record_offset, section_stats_t *stats)
{
    stats->total_records++;

    /* Save original filename for logging/diff */
    char orig_fn[FNLEN + 1];
    memcpy(orig_fn, fh->filename, FNLEN);
    orig_fn[FNLEN] = '\0';

    char orig_escaped[FNLEN * 4 + 1];
    escape_str(orig_escaped, sizeof(orig_escaped), fh->filename, FNLEN);

    /* Check validity according to repair rules */
    int cond1_bad_path = 0;
    int cond2_non_ascii = 0;
    int cond3_has_space = 0;
    int cond4_unterminated = 0;
    int cond5_dirty_trailing = 0;
    int cond6_non_exist = 0;

    const char *nul_ptr = memchr(fh->filename, '\0', FNLEN);
    size_t len = 0;

    if (!nul_ptr) {
        cond4_unterminated = 1;
        len = FNLEN;
    } else {
        len = (size_t)(nul_ptr - fh->filename);
    }

    if (len > 0) {
        cond1_bad_path = is_bad_leading_path(fh->filename, len);
        if (strcmp(fh->filename, "non-exist") == 0 || strcmp(fh->filename, "non-xist") == 0)
            cond6_non_exist = 1;

        for (size_t i = 0; i < len; i++) {
            unsigned char c = (unsigned char)fh->filename[i];
            if (c < 0x20 || c >= 0x7F)
                cond2_non_ascii = 1;
            if (c == ' ')
                cond3_has_space = 1;
        }

        /* Check if bytes after '\0' are dirty */
        if (!cond4_unterminated) {
            for (size_t i = len + 1; i < FNLEN; i++) {
                if (fh->filename[i] != '\0') {
                    cond5_dirty_trailing = 1;
                    break;
                }
            }
        }
    } else {
        /* Empty filename: check if bytes 1..FNLEN-1 have non-zero garbage */
        for (size_t i = 1; i < FNLEN; i++) {
            if (fh->filename[i] != '\0') {
                cond5_dirty_trailing = 1;
                break;
            }
        }
    }

    int need_rename = (cond1_bad_path || cond2_non_ascii || cond3_has_space || cond4_unterminated || cond6_non_exist);
    int need_repair = (need_rename || cond5_dirty_trailing);

    const char *new_name = NULL;
    if (need_rename) {
        new_name = ".deleted";

        if (opt_repair) {
            memset(fh->filename, 0, FNLEN);
            memcpy(fh->filename, new_name, strlen(new_name));
            if (pwrite(fd, fh, sizeof(*fh), record_offset) != (ssize_t)sizeof(*fh)) {
                fprintf(stderr, "[ERROR] Write repair failed at %s #%d: %s\n",
                        dir_path, rec_idx, strerror(errno));
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

        if (opt_repair) {
            printf("[REPAIRED] %s #%d\n", dir_path, rec_idx);
            printf("      old: '%s' (len=%zu, reason: %s)\n", orig_escaped, len, reasons);
            printf("      new: '%s' (trailing bytes zeroed)\n\n", new_name);
        } else {
            printf("[NEED-REPAIR] %s #%d\n", dir_path, rec_idx);
            printf("      current: '%s' (len=%zu, reason: %s)\n", orig_escaped, len, reasons);
            printf("      action:  rename to '%s' (trailing bytes zeroed)\n\n", new_name);
        }
        fflush(stdout);

        /* After repair/rename, new len is length of new_name */
        len = strlen(new_name);
    } else if (cond5_dirty_trailing) {
        if (opt_repair) {
            if (len > 0)
                memset(fh->filename + len + 1, 0, FNLEN - len - 1);
            else
                memset(fh->filename + 1, 0, FNLEN - 1);

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

    /* Record statistics for filename length */
    int final_null_term = (len < FNLEN);
    if (len <= FNLEN)
        stats->len_hist[len]++;
    else
        stats->len_hist[FNLEN]++;

    if ((int)len > stats->max_len) {
        stats->max_len = (int)len;
        snprintf(stats->max_len_fn, sizeof(stats->max_len_fn), "%.*s", (int)FNLEN, fh->filename);
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
        char fn_escaped[sizeof(fh->filename) * 4 + 1];
        escape_str(fn_escaped, sizeof(fn_escaped), fh->filename, sizeof(fh->filename));

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
        return;
    }

    fileheader_t buf[CHUNK_RECORDS];
    ssize_t bytes_read;
    int rec_idx = 0;

    while ((bytes_read = read(fd, buf, sizeof(buf))) > 0) {
        size_t count = (size_t)bytes_read / sizeof(fileheader_t);
        for (size_t i = 0; i < count; i++) {
            rec_idx++;
            off_t offset = (off_t)(rec_idx - 1) * (off_t)sizeof(fileheader_t);
            process_record(&buf[i], dir_path, rec_idx, fd, offset, stats);
        }
    }

    close(fd);
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
    for (int i = 0; i <= FNLEN; i++) {
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
            printf("  Renamed to '.deleted':        %14s\n", fmt_num(overall->repaired_renamed, b1));
            printf("  Zeroed dirty trailing bytes:  %14s\n", fmt_num(overall->repaired_trailing, b2));
            printf("  Total records repaired:       %14s\n\n",
                   fmt_num(overall->repaired_renamed + overall->repaired_trailing, b3));
        } else {
            printf("  Records needing rename:       %14s\n", fmt_num(overall->need_repair_renamed, b1));
            printf("  Records with dirty padding:   %14s\n", fmt_num(overall->need_repair_trailing, b2));
            printf("  Total records needing repair: %14s\n",
                   fmt_num(overall->need_repair_renamed + overall->need_repair_trailing, b3));
            printf("  NOTE: Run with -r or --repair to apply fixes to .DIR files.\n\n");
        }
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
            if (opt_threshold < 1 || opt_threshold > FNLEN) {
                fprintf(stderr, "Error: threshold must be between 1 and %d\n", FNLEN);
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

    if (overall_stats.count_gt_threshold > 0 ||
        overall_stats.need_repair_renamed > 0 ||
        overall_stats.need_repair_trailing > 0)
        return 1;
    return 0;
}
