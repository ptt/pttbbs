/*
 * checkvotes: lists the boards whose votes need care, and archives the
 * votes older code left stuck.
 *
 * mbbsd is to set bvote, the vote count kept in .BRD, from the votes'
 * control files (vote_count() in mbbsd/vote.c) rather than count it up and
 * down. Older code let bvote drift, and a board whose bvote fell to 0
 * keeps its votes unlisted and unclosed. This lists:
 *
 *   - boards whose bvote differs from the number of control files;
 *   - votes past their close time that are not closed;
 *   - votes without items ("000,000"), as an opening cut off at the items
 *     leaves;
 *   - control files without a close time, or that cannot be opened;
 *   - votes whose lock file is left, which b_result_one() never closes;
 *
 * with what mbbsd with vote_count() does with each.
 *
 * With -a, the votes on boards whose bvote is 0 that closed more than
 * ARCHIVE_AGE ago are to be archived: their files are moved to
 * DIR/<board>/, keeping their names, so that mbbsd does not close them
 * years late and post their results. A control file without a close time
 * goes by its modification time instead. Nothing is moved without -y.
 *
 * Run it before mbbsd with vote_count() is deployed: that mbbsd repairs a
 * bvote of 0 when a moderator or a user looks at the votes, and then
 * reads them. Older mbbsd does not read the votes of a board whose bvote
 * is 0, and an opening only takes a slot without a control file, so this
 * can run while the site is up. It reads the board's bvote again just
 * before moving each vote, from .BRD and from the shared memory mbbsd goes
 * by when it is there, never writes .BRD, and never replaces a
 * file: each file is linked to its new name and then unlinked, so DIR must
 * be on the file system of BBSHOME. The control file goes first; when a
 * file cannot be moved, those moved go back, the control file last and
 * only if all the others did.
 *
 *   checkvotes [-v] [-a DIR [-y]] [BBSHOME]
 *
 * -v lists every board with a vote or a bvote, not only those that need
 * care.
 */
#define _UTIL_C_
#include "bbs.h"

#include <getopt.h>

// as in mbbsd/vote.c: slots 0 to MAX_VOTE_NR - 1. Older user_vote() also
// lists slot MAX_VOTE_NR, which nothing else counts or closes.
#define MAX_VOTE_NR (20)
#define NUM_SLOTS (MAX_VOTE_NR + 1)

#define ARCHIVE_AGE (7 * DAY_SECONDS)

// The files of a vote, as in mbbsd/vote.c. The control file goes first:
// once it is moved, the vote is gone.
static const char *const vote_files[] = {
    "control", "ballots", "flags", "desc", "comments", "limited",
    "limits", "vtitle", "vlogs", "logconf", "vlock",
};
#define NUM_FILES ((int)ARRAY_SIZE(vote_files))

enum vote_state {
    VS_OPEN,
    VS_PAST,
    VS_NO_ITEMS,
    VS_NO_TIME,
    VS_CANNOT_OPEN,
    VS_COUNT,
};

static const char *const vs_name[VS_COUNT] = {
    "open",
    "past its close time",
    "no items",
    "no close time",
    "control file cannot be opened",
};

// What mbbsd with vote_count() does with it.
static const char *const vs_fate[VS_COUNT] = {
    "",
    "closed with a report at the next daily close",
    "dropped without a report at its close time",
    ("closed a day after it was last written, if counted or on a board"
     " holding another vote (an empty one is not counted)"),
    "counted but never closed",
};

typedef struct {
    int slot, items, locked, archive;
    enum vote_state state;
    time4_t closetime, mtime;
} vote_t;

typedef struct {
    int boards;         // boards listed
    int votes;
    int too_low, too_high;
    int stuck;          // votes on a board with bvote 0
    int locked;
    int state[VS_COUNT];
    int archive, archived, failed;
} totals_t;

static time4_t now_time;
static const char *archive_dir;     // absolute
static int apply;
static int brd_fd = -1;

// Reads a control file as b_result() and b_result_one() do: the first
// line is "items,tickets", the second the close time.
static enum vote_state
read_control(const char *path, vote_t *v)
{
    FILE *fp;
    char line[256];
    short n = 0, tickets = 0;
    int t = 0, ok = 0;

    v->mtime = dasht(path);
    if (!(fp = fopen(path, "r")))
        return VS_CANNOT_OPEN;
    // b_result_one() takes the items from the first field alone
    if (!fgets(line, sizeof(line), fp) ||
        sscanf(line, "%hd,%hd", &n, &tickets) < 1)
        n = 0;
    if (fgets(line, sizeof(line), fp) && sscanf(line, "%d", &t) == 1)
        ok = 1;
    fclose(fp);

    v->items = n;
    v->closetime = t;
    if (!ok)
        return VS_NO_TIME;
    if (v->items <= 0)
        return VS_NO_ITEMS;
    if (v->closetime < now_time)
        return VS_PAST;
    return VS_OPEN;
}

static void
vote_path(char *buf, const char *bname, const char *file, int slot)
{
    char name[32];

    snprintf(name, sizeof(name), "%s%d", file, slot);
    setbfile(buf, bname, name);
}

static int
archive_path(char *buf, const char *bname, const char *file, int slot)
{
    int len;

    if (file)
        len = snprintf(buf, PATHLEN, "%s/%s/%s%d", archive_dir, bname, file,
                       slot);
    else
        len = snprintf(buf, PATHLEN, "%s/%s", archive_dir, bname);
    if (len < 0 || len >= PATHLEN) {
        fprintf(stderr, "%s/%s: path too long\n", archive_dir, bname);
        return -1;
    }
    return 0;
}

// Moves src to dst without replacing a file at dst.
static int
move_file(const char *src, const char *dst)
{
    if (link(src, dst) != 0)
        return -1;
    if (unlink(src) != 0) {
        int err = errno;
        unlink(dst);
        errno = err;
        return -1;
    }
    return 0;
}

// Moves the files of a vote to archive_dir/<board>/, the control file
// first. Gives up before moving any when one of them is there already,
// and moves those moved back when one cannot be moved. Returns 0 when all
// are moved, -1 when none is, -2 when some could not be put back.
static int
archive_vote(const char *bname, int slot, int *nfiles)
{
    char src[PATHLEN], dst[PATHLEN];
    int moved[NUM_FILES], i, n = 0;

    *nfiles = 0;
    if (archive_path(dst, bname, NULL, slot) != 0)
        return -1;
    if (!dashd(dst) && Mkdir(dst) != 0) {
        fprintf(stderr, "%s: %s\n", dst, strerror(errno));
        return -1;
    }
    for (i = 0; i < NUM_FILES; i++) {
        vote_path(src, bname, vote_files[i], slot);
        if (archive_path(dst, bname, vote_files[i], slot) != 0)
            return -1;
        if (access(src, F_OK) == 0 && access(dst, F_OK) == 0) {
            fprintf(stderr, "%s: there already, vote %d of %s not moved\n",
                    dst, slot, bname);
            return -1;
        }
    }
    for (i = 0; i < NUM_FILES; i++) {
        vote_path(src, bname, vote_files[i], slot);
        archive_path(dst, bname, vote_files[i], slot);
        if (access(src, F_OK) != 0)
            continue;
        if (move_file(src, dst) == 0) {
            moved[n++] = i;
            continue;
        }
        fprintf(stderr, "%s -> %s: %s\n", src, dst, strerror(errno));
        // Put back what was moved, the control file last, and only when
        // the others are back: a vote missing some of its files must not
        // come back.
        {
            int put_back = 1;

            while (n-- > 0) {
                if (moved[n] == 0 && !put_back) {
                    fprintf(stderr, "LEFT IN %s/%s: control%d, so that vote %d"
                            " stays closed; its other files are on the board"
                            " or there\n", archive_dir, bname, slot, slot);
                    return -2;
                }
                vote_path(src, bname, vote_files[moved[n]], slot);
                archive_path(dst, bname, vote_files[moved[n]], slot);
                if (move_file(dst, src) != 0) {
                    put_back = 0;
                    fprintf(stderr, "PUT BACK FAILED: %s -> %s: %s\n", dst,
                            src, strerror(errno));
                }
            }
            return put_back ? -1 : -2;
        }
    }
    *nfiles = n;
    return 0;
}

// The board's bvote now, in .BRD and in the shared memory mbbsd goes by
// when attached (it changes that copy first): 0 only when both are, -1
// when the record is not that board.
static int
current_bvote(int bid, const char *bname)
{
    boardheader_t bh;

    if (pread(brd_fd, &bh, sizeof(bh), (off_t)(bid - 1) * sizeof(bh)) !=
        (ssize_t)sizeof(bh))
        return -1;
    bh.brdname[sizeof(bh.brdname) - 1] = '\0';
    if (strcmp(bh.brdname, bname) != 0)
        return -1;
    if (bh.bvote == 0 && SHM && bid <= MAX_BOARD) {
        const boardheader_t *sh = &bcache[bid - 1];

        if (strncmp(sh->brdname, bname, sizeof(sh->brdname)) != 0)
            return -1;
        return sh->bvote;
    }
    return bh.bvote;
}

static void
check_board(int bid, const boardheader_t *bh, int verbose, totals_t *tot)
{
    vote_t votes[NUM_SLOTS];
    char path[PATHLEN];
    int i, n = 0, counted = 0, care = 0;

    for (i = 0; i < NUM_SLOTS; i++) {
        vote_t *v = &votes[n];
        time4_t age;

        vote_path(path, bh->brdname, "control", i);
        if (!dashf(path))
            continue;
        memset(v, 0, sizeof(*v));
        v->slot = i;
        v->state = read_control(path, v);
        vote_path(path, bh->brdname, "vlock", i);
        v->locked = dashf(path);
        age = v->state >= VS_NO_TIME ? v->mtime : v->closetime;
        v->archive = archive_dir && bh->bvote == 0 && v->state != VS_OPEN &&
                     age < now_time - ARCHIVE_AGE;
        if (v->state != VS_OPEN || v->locked || i >= MAX_VOTE_NR)
            care = 1;
        if (i < MAX_VOTE_NR)
            counted++;
        n++;
    }
    if (n == 0 && bh->bvote == 0)
        return;
    if (counted != bh->bvote)
        care = 1;
    if (!care && !verbose)
        return;

    tot->boards++;
    tot->votes += n;
    printf("%5d %-13s bvote %d, %d vote%s", bid, bh->brdname, bh->bvote,
           n, n == 1 ? "" : "s");
    if (bh->bvote < counted) {
        tot->too_low++;
        printf("  [bvote too low%s]", bh->bvote == 0 ?
               ": its votes are neither listed nor closed" : "");
    } else if (bh->bvote > counted) {
        tot->too_high++;
        printf("  [bvote too high]");
    }
    printf("\n");

    for (i = 0; i < n; i++) {
        vote_t *v = &votes[i];

        tot->state[v->state]++;
        if (bh->bvote == 0)
            tot->stuck++;
        if (v->locked)
            tot->locked++;
        printf("        vote %2d: %s", v->slot, vs_name[v->state]);
        if (v->state < VS_NO_TIME)
            printf(", %d item%s, closes %s", v->items,
                   v->items == 1 ? "" : "s", Cdate(&v->closetime));
        else
            printf(", modified %s", Cdate(&v->mtime));
        if (v->archive) {
            int nfiles = 0, rc;

            tot->archive++;
            if (!apply) {
                printf(": to archive\n");
                continue;
            }
            // as late as can be: a moderator may have counted it meanwhile
            if (current_bvote(bid, bh->brdname) != 0) {
                tot->failed++;
                printf(": NOT archived, as bvote is no longer 0\n");
            } else if ((rc = archive_vote(bh->brdname, v->slot,
                                          &nfiles)) == 0) {
                tot->archived++;
                printf(": archived, %d file%s\n", nfiles,
                       nfiles == 1 ? "" : "s");
            } else {
                tot->failed++;
                printf(rc == -1 ?
                       ": NOT archived, left as it was (see above)\n" :
                       ": PARTLY archived, closed, files on the board and in"
                       " the archive (see above)\n");
            }
            continue;
        }
        if (v->slot >= MAX_VOTE_NR)
            printf(": a slot only older user_vote() lists, never counted or"
                   " closed");
        else if (v->locked)
            printf(", lock file left: never closed until vlock%d is removed",
                   v->slot);
        else if (vs_fate[v->state][0])
            printf(": %s%s", vs_fate[v->state],
                   bh->bvote == 0 && v->state != VS_CANNOT_OPEN ?
                   ", once bvote is repaired (a moderator entering the vote"
                   " menu, or a user the vote list)" : "");
        printf("\n");
    }
}

static void
usage(const char *prog)
{
    fprintf(stderr, "usage: %s [-v] [-a DIR [-y]] [BBSHOME]\n", prog);
    exit(2);
}

int
main(int argc, char **argv)
{
    const char *home = BBSHOME;
    char cwd[PATHLEN], dir[PATHLEN];
    boardheader_t bh;
    totals_t tot = {0};
    int bid = 0, verbose = 0, c;

    // so that errors on stderr and the lines they go with stay in order
    setvbuf(stdout, NULL, _IOLBF, 0);
    while ((c = getopt(argc, argv, "va:y")) != -1) {
        switch (c) {
        case 'v':
            verbose = 1;
            break;
        case 'a':
            archive_dir = optarg;
            break;
        case 'y':
            apply = 1;
            break;
        default:
            usage(argv[0]);
        }
    }
    if (optind < argc)
        home = argv[optind++];
    if (optind < argc || (apply && !archive_dir))
        usage(argv[0]);

    // archive_dir is used after chdir(home)
    if (archive_dir && archive_dir[0] != '/') {
        int len;

        if (!getcwd(cwd, sizeof(cwd))) {
            perror("getcwd");
            return 1;
        }
        len = snprintf(dir, sizeof(dir), "%s/%s", cwd, archive_dir);
        if (len < 0 || len >= (int)sizeof(dir)) {
            fprintf(stderr, "%s: path too long\n", archive_dir);
            return 1;
        }
        archive_dir = dir;
    }
    if (apply && !dashd(archive_dir) && Mkdir(archive_dir) != 0) {
        perror(archive_dir);
        return 1;
    }
    if (chdir(home) != 0) {
        perror(home);
        return 1;
    }
    if (apply) {
        struct stat st_home, st_dir;

        if (stat("boards", &st_home) != 0 || stat(archive_dir, &st_dir) != 0) {
            perror("stat");
            return 1;
        }
        if (st_home.st_dev != st_dir.st_dev) {
            fprintf(stderr, "%s: not on the file system of %s/boards\n",
                    archive_dir, home);
            return 1;
        }
    }
    if (apply && !attach_check_SHM())
        fprintf(stderr, "shared memory not found: going by .BRD alone\n");
    if ((brd_fd = open(FN_BOARD, O_RDONLY)) < 0) {
        perror(FN_BOARD);
        return 1;
    }
    now_time = (time4_t)time(NULL);

    while (read(brd_fd, &bh, sizeof(bh)) == sizeof(bh)) {
        bid++;
        // links have no vote files of their own
        if (!bh.brdname[0] || (bh.brdattr & BRD_SYMBOLIC))
            continue;
        bh.brdname[sizeof(bh.brdname) - 1] = '\0';
        check_board(bid, &bh, verbose, &tot);
    }
    close(brd_fd);

    printf("\n%d board%s listed, %d vote%s on them\n",
           tot.boards, tot.boards == 1 ? "" : "s",
           tot.votes, tot.votes == 1 ? "" : "s");
    printf("bvote too low: %d board%s (votes on boards with bvote 0: %d)\n",
           tot.too_low, tot.too_low == 1 ? "" : "s", tot.stuck);
    printf("bvote too high: %d board%s\n",
           tot.too_high, tot.too_high == 1 ? "" : "s");
    for (c = VS_PAST; c < VS_COUNT; c++)
        printf("%s: %d\n", vs_name[c], tot.state[c]);
    printf("lock file left: %d\n", tot.locked);
    if (archive_dir) {
        if (apply)
            printf("archived to %s: %d vote%s, %d not\n", archive_dir,
                   tot.archived, tot.archived == 1 ? "" : "s", tot.failed);
        else
            printf("to archive to %s: %d vote%s (run again with -y)\n",
                   archive_dir, tot.archive, tot.archive == 1 ? "" : "s");
    }
    return tot.failed ? 1 : 0;
}
