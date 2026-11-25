/*
 * clean_log_posts - move the logs posted to Security into adm/, and remove
 * the copies of posts from Security and Record.
 *
 * For each log post (by its owner and title, and its first line looking
 * like the log), oldest first:
 *  - compresses it (zstd -9 as bin/rotatelog, or gzip without zstd) into
 *    adm/YYYY/<name>/<name>-YYYYMMDD.zst (.gz), named after the day it
 *    covers as bin/rotatelog names it (the time of the post, at 0:01,
 *    less 6 minutes; bets: the day of the draw), checks the archive against
 *    the post, and gives it
 *    the time of the post as mtime. A second post for the same day goes to
 *    <name>-YYYYMMDD-2.zst, and so on; an archive already there with the
 *    same content (from an earlier run that stopped) counts as done.
 *  - then takes the posts archived (and those to remove) out of the .DIR
 *    at once, under its locks (rewritten in place, so a post appended
 *    meanwhile is kept), removes their files and updates the count of posts
 *    in the board cache.
 *
 * It also moves the archives the old bin/account left in adm/ (mailog every
 * day, usies on the days it failed to post), adm/usies-YYMMDD.gz and
 * adm/mailog-YYMMDD.gz, into adm/YYYY/<name>/<name>-YYYYMMDD.zst,
 * recompressed and checked (or as they are, .gz, without zstd).
 *
 * It writes the files of the board directly, not through BoF: run it before
 * the boards are imported into nest.svc (see README). The BBS may keep
 * running, though it is best run at a quiet time: zstd -9 takes a few
 * seconds for a 350 MB day (-l 19 makes it 20% smaller, in ~1.5 minutes).
 *
 * Usage: make clean_log_posts
 *        ./clean_log_posts [-w] [-v] [-n max] [-l level] [-b BBSHOME]
 *   Dry run unless -w; -n handles at most max posts of each board (to try
 *   it on a few).
 */
#define _UTIL_C_
#include "bbs.h"

#include <dirent.h>
#include <getopt.h>
#include <sys/file.h>
#include <sys/time.h>

#define LOG_OWNER "[歷史老師]"
// bin/account posted them at 0:01: the day is their time less 6 minutes.
#define ADJUST_SECONDS (6 * 60)

// The posts to clean, by their board, owner and a word of their titles.
typedef struct {
    const char *board;
    const char *owner;  // of the post
    const char *title;  // in the title of the post (NULL: any)
    const char *name;   // adm/YYYY/<name>/<name>-YYYYMMDD.zst
    int (*looks_like)(const char *line);
    int adjust;         // seconds before the post the day is taken
    int remove;         // removed, not archived (name: what it is)
} log_kind_t;

// usies: "MM/DD/YYYY HH:MM:SS Www ..."
static int
usies_line(const char *line)
{
    int mo, da, yr, hh, mm, ss;
    return sscanf(line, "%d/%d/%d %d:%d:%d", &mo, &da, &yr, &hh, &mm,
                  &ss) == 6 && mo >= 1 && mo <= 12 && da >= 1 && da <= 31;
}

// The song log (etc/osong.log): "id: <userid> ..."
static int
osong_line(const char *line)
{
    return strncmp(line, "id: ", 4) == 0;
}

// The money log (etc/money): "<userid> 給 <userid> <money> ..."
static int
money_line(const char *line)
{
    return strstr(line, " 給 ") != NULL;
}

// The bets, as openticket posted them: the header of the post first.
static int
bets_line(const char *line)
{
    return strncmp(line, "作者: [投注紀錄]", strlen("作者: [投注紀錄]")) == 0;
}

// mailog: "YY/MM/DD HH:MM:SS ..."
static int
mailog_line(const char *line)
{
    int yy, mo, da, hh, mm, ss;
    return sscanf(line, "%d/%d/%d %d:%d:%d", &yy, &mo, &da, &hh, &mm,
                  &ss) == 6;
}

static const log_kind_t kinds[] = {
    // Logs, archived.
    { BN_SECURITY, LOG_OWNER, "使用者上線監控", "usies", usies_line,
      ADJUST_SECONDS, 0 },
    { BN_SECURITY, LOG_OWNER, "本日點歌記錄", "osong.log", osong_line,
      ADJUST_SECONDS, 0 },
    { BN_SECURITY, LOG_OWNER, "本日金錢往來記錄", "money", money_line,
      ADJUST_SECONDS, 0 },
    { BN_SECURITY, "[投注紀錄]", NULL, "lottery_bets.log", bets_line, 0, 0 },
    { BN_SECURITY, "[系統安全局]", "站外來信紀錄mailog", "mailog",
      mailog_line, 0, 0 },
    // Copies and leftovers, removed.
    { BN_SECURITY, "[馬路探子]", "樂透開獎", "lottery result", NULL, 0, 1 },
    { BN_SECURITY, "[馬路探子]", "樂透取消", "lottery result", NULL, 0, 1 },
    { BN_RECORD, "[馬路探子]", "樂透開獎", "lottery result", NULL, 0, 1 },
    { BN_RECORD, "[馬路探子]", "樂透取消", "lottery result", NULL, 0, 1 },
    { BN_RECORD, "[馬路探子]", "開始舉辦樂透", "lottery start", NULL, 0, 1 },
    { BN_RECORD, "[馬路探子]", "看板 選情報導", "poll result", NULL, 0, 1 },
    { BN_RECORD, LOG_OWNER, "雞場報告", "chicken report", NULL, 0, 1 },
    { BN_RECORD, "[Ptt警察局]", "今日違法廣告信名單", "spam list", NULL, 0,
      1 },
};
static int removed_of[sizeof(kinds) / sizeof(kinds[0])];
#define NKINDS ((int)(sizeof(kinds) / sizeof(kinds[0])))

static int write_mode, verbose, level = 9, zstd;

static void
usage(const char *prog)
{
    printf("Usage: %s [-w] [-v] [-n max] [-l level] [-b BBSHOME]\n\n"
           "Moves the logs posted to %s (usies, song, money, bets,\n"
           "mailog) into adm/YYYY/<name>/<name>-YYYYMMDD.zst (.gz without\n"
           "zstd), removes the copies of posts in %s and %s (results of\n"
           "lotteries and polls, ...), and moves adm/usies-YYMMDD.gz and\n"
           "adm/mailog-YYMMDD.gz into adm/YYYY/. Run it before the boards\n"
           "are imported into nest.svc.\n"
           "Dry run unless -w (--write); -n handles at most max posts of\n"
           "each board; -l is the zstd level (default %d; gzip takes up to\n"
           "9).\n", prog, BN_SECURITY, BN_SECURITY, BN_RECORD, level);
}

// A post picked to move.
typedef struct {
    char filename[FNLEN];
    const log_kind_t *kind;
    int archived;
} item_t;

static const log_kind_t *
kind_of(const char *board, const fileheader_t *fh)
{
    if (fh->filename[0] != 'M' || fh->filename[1] != '.')
        return NULL;
    for (int i = 0; i < NKINDS; i++)
        if (strcmp(board, kinds[i].board) == 0 &&
            strcmp(fh->owner, kinds[i].owner) == 0 &&
            (!kinds[i].title || strstr(fh->title, kinds[i].title)))
            return &kinds[i];
    return NULL;
}

// Whether the file starts like the log, or is empty (a day with nothing).
static int
looks_like(const char *path, const log_kind_t *kind)
{
    char line[256];
    int ok;
    FILE *fp = fopen(path, "r");

    if (!fp)
        return 0;
    if (!fgets(line, sizeof(line), fp))
        ok = feof(fp);
    else
        ok = kind->looks_like(line);
    fclose(fp);
    return ok;
}

// Runs a shell command; its status is 0 on success.
static int
run(const char *cmd)
{
    if (verbose > 1)
        printf("  %s\n", cmd);
    return system(cmd);
}

static int
same_content(const char *archive, const char *plain)
{
    AUTO_STR(cmd, "%s -dc '%s' | cmp -s - '%s'", zstd ? "zstd -q" : "gzip",
             archive, plain);
    return run(cmd) == 0;
}

// The output of a command (its first line), for checksums.
static void
output_of(const char *cmd, char *out, size_t sz)
{
    FILE *fp = popen(cmd, "r");

    out[0] = 0;
    if (!fp)
        return;
    if (!fgets(out, sz, fp))
        out[0] = 0;
    pclose(fp);
}

static void
set_mtime(const char *path, time_t t)
{
    struct timeval tv[2] = { { .tv_sec = t }, { .tv_sec = t } };
    utimes(path, tv);
}

// Archives a post. Returns 1 if archived (now or by an earlier run), 0 if
// skipped, -1 on errors.
static int
archive_post(item_t *it)
{
    char src[PATHLEN], target[PATHLEN];
    time_t posted = time4_to_time(get_fhdr_stamp_ts(it->filename));
    time_t day_t = posted - it->kind->adjust;
    const char *name = it->kind->name;
    struct tm day;
    struct stat st;

    setbfile(src, it->kind->board, it->filename);
    if (it->kind->remove) {
        if (verbose)
            printf("%s %s (%s)\n", write_mode ? "remove" : "would remove",
                   src, name);
        removed_of[it->kind - kinds]++;
        return 1;
    }
    if (stat(src, &st) != 0) {
        fprintf(stderr, "%s: %s\n", src, strerror(errno));
        return -1;
    }
    if (!looks_like(src, it->kind)) {
        printf("skip %s: not %s\n", src, name);
        return 0;
    }

    localtime_r(&day_t, &day);
    AUTO_STR(year_dir, "adm/%04d", day.tm_year + 1900);
    AUTO_STR(dir, "%s/%s", year_dir, name);
    for (int n = 1; ; n++) {
        if (n == 1)
            snprintf(target, sizeof(target), "%s/%s-%04d%02d%02d.%s", dir,
                     name, day.tm_year + 1900, day.tm_mon + 1, day.tm_mday,
                     zstd ? "zst" : "gz");
        else
            snprintf(target, sizeof(target), "%s/%s-%04d%02d%02d-%d.%s",
                     dir, name, day.tm_year + 1900, day.tm_mon + 1,
                     day.tm_mday, n, zstd ? "zst" : "gz");
        if (!dashf(target))
            break;
        if (same_content(target, src)) {
            if (verbose)
                printf("%s -> %s: already there\n", src, target);
            return 1;
        }
    }

    if (verbose || !write_mode)
        printf("%s %s (%lld bytes) -> %s\n", write_mode ? "move" : "would move",
               src, (long long)st.st_size, target);
    if (!write_mode)
        return 1;
    Mkdir(year_dir);
    Mkdir(dir);
    AUTO_STR(tmp, "%s.tmp", target);
    char cmd[PATHLEN * 3];
    if (zstd)
        snprintf(cmd, sizeof(cmd), "zstd -q -f -%d -o '%s' '%s'", level, tmp,
                 src);
    else
        snprintf(cmd, sizeof(cmd), "gzip -nc -%d '%s' >'%s'",
                 level < 9 ? level : 9, src, tmp);
    if (run(cmd) != 0 || !same_content(tmp, src) || Rename(tmp, target) != 0) {
        fprintf(stderr, "%s: cannot compress to %s\n", src, target);
        unlink(tmp);
        return -1;
    }
    set_mtime(target, posted);
    return 1;
}

static int
cmp_name(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

// Takes the posts archived (or to remove) out of .DIR (in place, under the locks of
// append_record and of the old expire tools) and removes their files.
// Returns how many, or -1.
static int
prune_dir(const char *board, const char *dir_path, const item_t *items,
          int nitems)
{
    char (*names)[FNLEN] = calloc(nitems ? nitems : 1, FNLEN);
    char lock_path[PATHLEN], path[PATHLEN], name[FNLEN];
    fileheader_t *recs = NULL;
    int n = 0, nrecs, kept = 0, removed = 0, fd = -1, lock_fd = -1;
    struct stat st;

    if (!names)
        return -1;
    for (int i = 0; i < nitems; i++)
        if (items[i].archived)
            strlcpy(names[n++], items[i].filename, FNLEN);
    if (!n) {
        free(names);
        return 0;
    }
    qsort(names, n, FNLEN, cmp_name);

    snprintf(lock_path, sizeof(lock_path), "%s.lock", dir_path);
    if ((lock_fd = OpenCreate(lock_path, O_RDWR | O_APPEND)) < 0 ||
        flock(lock_fd, LOCK_EX) != 0 ||
        (fd = open(dir_path, O_RDWR)) < 0 || flock(fd, LOCK_EX) != 0 ||
        fstat(fd, &st) != 0) {
        fprintf(stderr, "Error: cannot lock %s: %s\n", dir_path,
                strerror(errno));
        goto out;
    }
    nrecs = st.st_size / sizeof(fileheader_t);
    if (!(recs = malloc((nrecs ? nrecs : 1) * sizeof(fileheader_t))) ||
        pread(fd, recs, nrecs * sizeof(fileheader_t), 0) !=
            (ssize_t)(nrecs * sizeof(fileheader_t))) {
        fprintf(stderr, "Error: cannot read %s\n", dir_path);
        goto out;
    }
    for (int i = 0; i < nrecs; i++) {
        strlcpy(name, recs[i].filename, sizeof(name));
        if (bsearch(name, names, n, FNLEN, cmp_name)) {
            setbfile(path, board, name);
            unlink(path);
            removed++;
        } else {
            recs[kept++] = recs[i];
        }
    }
    if (pwrite(fd, recs, kept * sizeof(fileheader_t), 0) !=
            (ssize_t)(kept * sizeof(fileheader_t)) ||
        ftruncate(fd, kept * sizeof(fileheader_t)) != 0) {
        fprintf(stderr, "Error: cannot write %s: %s (a copy is in %s.old)\n",
                dir_path, strerror(errno), dir_path);
        removed = -1;
    }
out:
    if (fd >= 0)
        close(fd);
    if (lock_fd >= 0)
        close(lock_fd);
    free(recs);
    free(names);
    return removed;
}

// Moves adm/<name>-YYMMDD.gz into adm/YYYY/<name>/<name>-YYYYMMDD.zst (or
// .gz as it is, without zstd). Returns how many were (or would be) moved.
static int
move_old_archives(void)
{
    DIR *d = opendir("adm");
    struct dirent *de;
    int moved = 0;

    if (!d)
        return 0;
    while ((de = readdir(d))) {
        char name[16], a[64] = "", b[64] = "";
        int yy, mm, dd, end = 0;

        if (sscanf(de->d_name, "%15[a-z]-%2d%2d%2d.gz%n", name, &yy, &mm, &dd,
                   &end) != 4 || de->d_name[end] ||
            strlen(de->d_name) != strlen(name) + 1 + 6 + 3 ||
            (strcmp(name, "usies") != 0 && strcmp(name, "mailog") != 0) ||
            mm < 1 || mm > 12 || dd < 1 || dd > 31)
            continue;
        int year = yy >= 90 ? 1900 + yy : 2000 + yy;
        AUTO_STR(src, "adm/%s", de->d_name);
        AUTO_STR(year_dir, "adm/%04d", year);
        AUTO_STR(dir, "%s/%s", year_dir, name);
        AUTO_STR(target, "%s/%s-%04d%02d%02d.%s", dir, name, year, mm, dd,
                 zstd ? "zst" : "gz");
        AUTO_STR(tmp, "%s.tmp", target);
        if (dashf(target)) {
            printf("skip %s: %s is there\n", src, target);
            continue;
        }
        if (verbose)
            printf("%s %s -> %s\n", write_mode ? "move" : "would move", src,
                   target);
        if (write_mode && !zstd) {
            Mkdir(year_dir);
            Mkdir(dir);
            if (Rename(src, target) != 0) {
                fprintf(stderr, "%s: cannot move to %s\n", src, target);
                continue;
            }
        } else if (write_mode) {
            struct stat st;
            Mkdir(year_dir);
            Mkdir(dir);
            AUTO_STR(cmd, "gzip -dc '%s' | zstd -q -f -%d -o '%s'", src,
                     level, tmp);
            AUTO_STR(sum_a, "gzip -dc '%s' | md5sum", src);
            AUTO_STR(sum_b, "zstd -dcq '%s' | md5sum", tmp);
            if (run(cmd) == 0) {
                output_of(sum_a, a, sizeof(a));
                output_of(sum_b, b, sizeof(b));
            }
            if (!a[0] || strcmp(a, b) != 0 || stat(src, &st) != 0 ||
                Rename(tmp, target) != 0) {
                fprintf(stderr, "%s: cannot recompress to %s\n", src, target);
                unlink(tmp);
                continue;
            }
            set_mtime(target, st.st_mtime);
            unlink(src);
        }
        moved++;
    }
    closedir(d);
    return moved;
}

int
main(int argc, char **argv)
{
    static struct option opts[] = {
        { "write",   no_argument,       0, 'w' },
        { "verbose", no_argument,       0, 'v' },
        { "max",     required_argument, 0, 'n' },
        { "level",   required_argument, 0, 'l' },
        { "bbshome", required_argument, 0, 'b' },
        { "help",    no_argument,       0, 'h' },
        { 0, 0, 0, 0 }
    };
    const char *bbshome = BBSHOME;
    char dir_path[PATHLEN];
    int c, max = -1, archived = 0, skipped = 0, failed = 0, removed = 0;
    fileheader_t fh;

    while ((c = getopt_long(argc, argv, "wvn:l:b:h", opts, NULL)) != -1) {
        switch (c) {
        case 'w': write_mode = 1; break;
        case 'v': verbose++; break;
        case 'n': max = atoi(optarg); break;
        case 'l': level = atoi(optarg); break;
        case 'b': bbshome = optarg; break;
        case 'h': usage(argv[0]); return 0;
        default: usage(argv[0]); return 1;
        }
    }
    if (level < 1 || level > 19) {
        usage(argv[0]);
        return 1;
    }
    zstd = system("command -v zstd >/dev/null 2>&1") == 0;
    if (!zstd)
        printf("No zstd: compressing with gzip into .gz.\n");

    now = time(NULL);
    drop_privileges();
    if (chdir(bbshome) != 0) {
        fprintf(stderr, "Error: cannot chdir to %s: %s\n", bbshome,
                strerror(errno));
        return 1;
    }

    const char *boards[] = { BN_SECURITY, BN_RECORD };
    for (size_t b = 0; b < sizeof(boards) / sizeof(boards[0]); b++) {
        const char *board = boards[b];
        int nitems = 0, done = 0;
        item_t *items = NULL;

        setbfile(dir_path, board, FN_DIR);
        int fd = open(dir_path, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "%s: %s\n", dir_path, strerror(errno));
            failed++;
            continue;
        }
        while (read_fileheader(fd, &fh) == 1) {
            const log_kind_t *kind = kind_of(board, &fh);
            if (!kind)
                continue;
            if (max >= 0 && nitems >= max)
                break;
            items = realloc(items, sizeof(*items) * (nitems + 1));
            if (!items) {
                fprintf(stderr, "Error: out of memory\n");
                return 1;
            }
            items[nitems] = (item_t){ .kind = kind };
            strlcpy(items[nitems].filename, fh.filename, FNLEN);
            nitems++;
        }
        close(fd);
        printf("%s: %d posts to clean%s.\n", dir_path, nitems,
               max >= 0 ? " (at most -n)" : "");

        for (int i = 0; i < nitems; i++) {
            int r = archive_post(&items[i]);
            if (r > 0) {
                items[i].archived = 1;
                done++;
                if (!items[i].kind->remove)
                    archived++;
            } else if (r == 0) {
                skipped++;
            } else {
                failed++;
            }
            if (write_mode && !items[i].kind->remove && (i + 1) % 10 == 0)
                printf("%d/%d...\n", i + 1, nitems);
        }

        if (write_mode && done) {
            // A copy of the list as it was, in case.
            AUTO_STR(old, "%s.old", dir_path);
            Copy(dir_path, old);
            int n = prune_dir(board, dir_path, items, nitems);
            if (n < 0) {
                failed++;
            } else {
                removed += n;
                if (attach_check_SHM()) {
                    int bid = getbnum(board);
                    if (bid > 0)
                        touchbtotal(bid);
                }
            }
        }
        free(items);
    }
    for (int i = 0; i < NKINDS; i++)
        if (kinds[i].remove && removed_of[i])
            printf("%s: %d %s posts (%s) %s\n", kinds[i].board, removed_of[i],
                   kinds[i].name, kinds[i].title,
                   write_mode ? "removed" : "to remove");

    int old = move_old_archives();

    printf("%s: %d log posts %s, %d posts taken out of the boards, "
           "%d skipped, %d failed; %d old archives in adm/ %s.\n",
           write_mode ? "Done" : "Dry run", archived,
           write_mode ? "archived" : "to archive", removed, skipped, failed,
           old, write_mode ? "moved" : "to move");
    if (write_mode && (removed || old))
        log_filef("log/clean_log_posts.log",
                  "archived %d log posts in adm/, took %d posts out of %s "
                  "and %s, moved %d old archives into adm/YYYY/\n",
                  archived, removed, BN_SECURITY, BN_RECORD, old);
    return failed ? 1 : 0;
}
