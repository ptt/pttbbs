#define _UTIL_C_
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bbs.h"

static int opt_dry_run = 0;
static int opt_verbose = 0;
static int opt_keep_file = 0;

static int
migrate_board(int bid, boardheader_t *bh)
{
    char fn_bot[PATHLEN], fn_dir[PATHLEN], art_path[PATHLEN];

    if (!bh || !bh->brdname[0])
        return 0;

    setbfile(fn_bot, bh->brdname, FN_DIR ".bottom");
    if (!dashf(fn_bot)) {
        if (opt_verbose)
            printf("[#%d %s] no %s found, skipping\n", bid, bh->brdname, FN_DIR ".bottom");
        return 0;
    }

    int fd_bot = open(fn_bot, O_RDWR);
    if (fd_bot < 0) {
        if (errno == ENOENT)
            return 0;
        perror(fn_bot);
        return -1;
    }

    if (flock(fd_bot, LOCK_EX) < 0) {
        perror("flock bottom");
        close(fd_bot);
        return -1;
    }

    struct stat st_bot;
    if (fstat(fd_bot, &st_bot) < 0 || st_bot.st_nlink == 0) {
        close(fd_bot);
        return 0;
    }

    int bot_count = (int)(st_bot.st_size / sizeof(fileheader_t));
    if (bot_count <= 0) {
        printf("[#%d %s] %s is empty\n", bid, bh->brdname, FN_DIR ".bottom");
        if (!opt_dry_run && !opt_keep_file)
            unlink(fn_bot);
        close(fd_bot);
        return 0;
    }
    if (bot_count > MAX_BOTTOM_POSTS) {
        printf("[#%d %s] warning: %d bottom posts found, truncating to %d\n",
               bid, bh->brdname, bot_count, MAX_BOTTOM_POSTS);
        bot_count = MAX_BOTTOM_POSTS;
    }

    setbfile(fn_dir, bh->brdname, FN_DIR);
    int fd_dir = open(fn_dir, O_RDWR | O_CREAT, DEFAULT_FILE_CREATE_PERM);
    if (fd_dir < 0) {
        perror(fn_dir);
        close(fd_bot);
        return -1;
    }

    struct stat st_dir;
    int total = (fstat(fd_dir, &st_dir) == 0)
                    ? (int)(st_dir.st_size / sizeof(fileheader_t))
                    : 0;

    aidu_t new_slots[MAX_BOTTOM_POSTS];
    memset(new_slots, 0, sizeof(new_slots));
    int valid_cnt = 0;

    for (int i = 0; i < bot_count && valid_cnt < MAX_BOTTOM_POSTS; i++) {
        fileheader_t bfh;
        if (pread(fd_bot, &bfh, sizeof(bfh), (off_t)i * sizeof(bfh)) !=
            (ssize_t)sizeof(bfh))
            break;

        aidu_t a = fn2aidu(bfh.filename);
        if (aidu_raw(a) == 0)
            continue;

        /* Use legacy multi.refer as O(1) index hint if available */
        uint32_t raw_ref = (uint32_t)bfh.multi.money;
        if ((raw_ref & 0x80000000U) && (raw_ref & 0x7fffffffU) > 0)
            a = aidu_with_idx(a, (int)(raw_ref & 0x7fffffffU));

        fileheader_t dfh;
        int found_idx = search_dir_by_aidu_fd(fd_dir, total, a, 0, NULL);
        if (found_idx > 0) {
            off_t off = (off_t)(found_idx - 1) * sizeof(dfh);
            if (pread(fd_dir, &dfh, sizeof(dfh), off) == (ssize_t)sizeof(dfh) &&
                (dfh.filemode & (FILE_BOTTOM | FILE_MARKED)) !=
                (FILE_BOTTOM | FILE_MARKED)) {
                dfh.filemode |= (FILE_BOTTOM | FILE_MARKED);
                if (!opt_dry_run)
                    pwrite(fd_dir, &dfh, sizeof(dfh), off);
            }
            new_slots[valid_cnt++] = aidu_with_idx(a, found_idx);
            printf("[#%d %s] slot %d: %s (matched .DIR recno %d)\n",
                   bid, bh->brdname, valid_cnt - 1, bfh.filename, found_idx);
        } else {
            /* Orphan bottom post: restore into .DIR if article file exists */
            setbfile(art_path, bh->brdname, bfh.filename);
            if (dashf(art_path)) {
                dfh = bfh;
                dfh.multi.money = 0;
                dfh.filemode |= (FILE_BOTTOM | FILE_MARKED);
                int new_recno = total + 1;
                if (!opt_dry_run) {
                    if (append_record(fn_dir, &dfh, sizeof(dfh)) == 0)
                        total = get_num_records(fn_dir, sizeof(fileheader_t));
                    else
                        new_recno = 0;
                } else {
                    total++;
                }
                if (new_recno > 0) {
                    new_slots[valid_cnt++] = aidu_with_idx(a, new_recno);
                    printf("[#%d %s] slot %d: %s (restored orphan to .DIR recno %d)\n",
                           bid, bh->brdname, valid_cnt - 1, bfh.filename, new_recno);
                }
            } else {
                printf("[#%d %s] slot dropped: %s (missing article)\n",
                       bid, bh->brdname, bfh.filename);
            }
        }
    }
    close(fd_dir);

    if (!opt_dry_run) {
        memcpy(bh->bottom, new_slots, sizeof(bh->bottom));
        substitute_record(FN_BOARD, bh, sizeof(boardheader_t), bid);
        reset_board(bid);
        if (SHM && bid >= 1 && bid <= MAX_BOARD)
            SHM->n_bottom[bid - 1] = valid_cnt;

        if (!opt_keep_file) {
            if (unlink(fn_bot) < 0)
                perror("unlink bottom file");
        }
    }

    printf("[#%d %s] migrated %d bottom posts%s\n",
           bid, bh->brdname, valid_cnt, opt_dry_run ? " (dry-run)" : "");
    close(fd_bot);
    return valid_cnt;
}

static void
usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [options] [boardname ...]\n"
            "Migrate legacy .DIR.bottom files into boardheader_t.bottom[] in .BRD / SHM.\n\n"
            "Options:\n"
            "  -n, --dry-run   Show what would be migrated without modifying files or SHM\n"
            "  -k, --keep      Do not remove .DIR.bottom after migrating\n"
            "  -v, --verbose   Show verbose logs for boards without .DIR.bottom\n"
            "  -h, --help      Display this help message\n\n"
            "If no boardname is specified, all boards in the system are scanned.\n",
            prog);
}

int
main(int argc, char **argv)
{
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-') {
        if (strcmp(argv[argi], "-n") == 0 || strcmp(argv[argi], "--dry-run") == 0) {
            opt_dry_run = 1;
        } else if (strcmp(argv[argi], "-k") == 0 || strcmp(argv[argi], "--keep") == 0) {
            opt_keep_file = 1;
        } else if (strcmp(argv[argi], "-v") == 0 || strcmp(argv[argi], "--verbose") == 0) {
            opt_verbose = 1;
        } else if (strcmp(argv[argi], "-h") == 0 || strcmp(argv[argi], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[argi]);
            usage(argv[0]);
            return 1;
        }
        argi++;
    }

    now = time(NULL);
    if (chdir(BBSHOME) < 0) {
        perror("chdir " BBSHOME);
        return 1;
    }

    attach_SHM();
    if (!SHM) {
        fprintf(stderr, "Failed to attach to SHM.\n");
        return 1;
    }

    int n_boards = num_boards();
    printf("Scanning boards (total %d)...%s\n",
           n_boards, opt_dry_run ? " [DRY-RUN]" : "");

    int total_migrated_boards = 0;
    int total_migrated_posts = 0;

    if (argi < argc) {
        for (; argi < argc; argi++) {
            int bid = getbnum(argv[argi]);
            if (bid <= 0 || bid > n_boards) {
                fprintf(stderr, "Board '%s' not found.\n", argv[argi]);
                continue;
            }
            boardheader_t *bh = getbcache(bid);
            int cnt = migrate_board(bid, bh);
            if (cnt > 0) {
                total_migrated_boards++;
                total_migrated_posts += cnt;
            }
        }
    } else {
        for (int bid = 1; bid <= n_boards; bid++) {
            boardheader_t *bh = getbcache(bid);
            if (!bh || !bh->brdname[0])
                continue;
            int cnt = migrate_board(bid, bh);
            if (cnt > 0) {
                total_migrated_boards++;
                total_migrated_posts += cnt;
            }
        }
    }

    printf("Done. Migrated %d posts across %d board(s)%s.\n",
           total_migrated_posts, total_migrated_boards,
           opt_dry_run ? " (dry-run)" : "");
    return 0;
}
