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
static int opt_backup = 1;

static void
usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [options] [path_to_.BRD]\n"
            "Migrate legacy boardheader_t title format into bclass and title in .BRD.\n\n"
            "Options:\n"
            "  -n, --dry-run   Show what would be migrated without modifying files\n"
            "  -v, --verbose   Show detailed migration info for each board\n"
            "  --no-backup     Do not create .bak backup before modifying .BRD\n"
            "  -h, --help      Display this help message\n\n"
            "If path_to_.BRD is omitted, " BBSHOME "/" FN_BOARD " or ./" FN_BOARD " is used.\n",
            prog);
}



int
main(int argc, char **argv)
{
    const char *brd_path = NULL;
    int argi = 1;

    while (argi < argc) {
        if (strcmp(argv[argi], "-n") == 0 || strcmp(argv[argi], "--dry-run") == 0) {
            opt_dry_run = 1;
        } else if (strcmp(argv[argi], "-v") == 0 || strcmp(argv[argi], "--verbose") == 0) {
            opt_verbose = 1;
        } else if (strcmp(argv[argi], "--no-backup") == 0) {
            opt_backup = 0;
        } else if (strcmp(argv[argi], "-h") == 0 || strcmp(argv[argi], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[argi][0] != '-') {
            brd_path = argv[argi];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[argi]);
            usage(argv[0]);
            return 1;
        }
        argi++;
    }

    char default_path[PATHLEN];
    if (!brd_path) {
        if (dashf(BBSHOME "/" FN_BOARD)) {
            brd_path = BBSHOME "/" FN_BOARD;
        } else if (dashf(FN_BOARD)) {
            brd_path = FN_BOARD;
        } else {
            snprintf(default_path, sizeof(default_path), "%s/%s", BBSHOME, FN_BOARD);
            brd_path = default_path;
        }
    }

    int fd = open(brd_path, opt_dry_run ? O_RDONLY : O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Error opening %s: %s\n", brd_path, strerror(errno));
        return 1;
    }

    if (flock(fd, opt_dry_run ? LOCK_SH : LOCK_EX) < 0) {
        perror("flock .BRD");
        close(fd);
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat .BRD");
        close(fd);
        return 1;
    }

    int total_records = (int)(st.st_size / sizeof(boardheader_t));
    printf("Migrating %s (%d boards)%s...\n",
           brd_path, total_records, opt_dry_run ? " [DRY-RUN]" : "");

    if (!opt_dry_run && opt_backup) {
        char bak_path[PATHLEN + 8];
        snprintf(bak_path, sizeof(bak_path), "%s.bak", brd_path);
        if (copy_file(brd_path, bak_path) == 0) {
            printf("Backup created: %s\n", bak_path);
        } else {
            fprintf(stderr, "Warning: failed to create backup %s (%s)\n",
                    bak_path, strerror(errno));
        }
    }

    int count_migrated = 0;
    int count_already_ok = 0;
    int count_empty = 0;

    for (int i = 0; i < total_records; i++) {
        boardheader_t bh;
        off_t offset = (off_t)i * sizeof(boardheader_t);

        if (pread(fd, &bh, sizeof(bh), offset) != (ssize_t)sizeof(bh)) {
            fprintf(stderr, "Error reading record %d at offset %ld\n", i + 1, (long)offset);
            break;
        }

        if (bh.brdname[0] == '\0') {
            count_empty++;
            continue;
        }

        /*
         * In legacy format:
         *   bh.bclass[0..3]: 4-byte class name
         *   bh.bclass[4]:    ' ' (space separator)
         *   bh.pad_symbol:   2-byte symbol (e.g. symbol)
         *   bh.title:        title text (starts at old offset 7)
         *
         * In new format:
         *   bh.bclass[4]:    '\0'
         *   bh.pad_symbol:   '\0\0' (deprecated)
         *   bh.title:        title text
         */
        int needs_migration = 0;
        if (bh.bclass[4] == ' ') {
            needs_migration = 1;
        } else if (bh.pad_symbol[0] != '\0' || bh.pad_symbol[1] != '\0') {
            needs_migration = 1;
        }

        if (needs_migration) {
            char old_bclass[6], old_sym[3];
            snprintf(old_bclass, sizeof(old_bclass), "%.4s", bh.bclass);
            snprintf(old_sym, sizeof(old_sym), "%.2s", bh.pad_symbol);

            bh.bclass[4] = '\0';
            bh.pad_symbol[0] = '\0';
            bh.pad_symbol[1] = '\0';
            bh.desc[BTLEN] = '\0';

            if (!opt_dry_run) {
                if (pwrite(fd, &bh, sizeof(bh), offset) != (ssize_t)sizeof(bh)) {
                    fprintf(stderr, "Error writing record %d (%s)\n", i + 1, bh.brdname);
                    break;
                }
            }

            count_migrated++;
            if (opt_verbose) {
                printf("[#%d %-13s] migrated: class=\"%s\" (was \"%s\" sym=\"%s\") desc=\"%s\"\n",
                       i + 1, bh.brdname, bh.bclass, old_bclass, old_sym, bh.desc);
            }
        } else {
            count_already_ok++;
            if (opt_verbose) {
                printf("[#%d %-13s] already ok: class=\"%s\" desc=\"%s\"\n",
                       i + 1, bh.brdname, bh.bclass, bh.desc);
            }
        }
    }

    close(fd);

    printf("\nFinished migration of %s%s:\n"
           "  Total records:     %d\n"
           "  Migrated boards:   %d\n"
           "  Already migrated:  %d\n"
           "  Empty records:     %d\n",
           brd_path, opt_dry_run ? " (DRY-RUN)" : "",
           total_records, count_migrated, count_already_ok, count_empty);

    return 0;
}
