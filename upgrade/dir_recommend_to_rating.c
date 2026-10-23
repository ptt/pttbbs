#define _UTIL_C_
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bbs.h"

#define RECORD_SIZE     128
#define OFF_PAD         32
#define OFF_REC         33
#define CHUNK_RECORDS   256

_Static_assert(sizeof(fileheader_t) == RECORD_SIZE, "fileheader_t size mismatch");
_Static_assert(sizeof(boardheader_t) == 256, "boardheader_t size mismatch");

static int opt_dry_run = 0;
static int opt_force = 0;
static int opt_backup = 0;
static int opt_verbose = 0;

static char *
expand_home(const char *path)
{
    if (path && path[0] == '~' && (path[1] == '/' || path[1] == '\0')) {
        const char *home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            if (pw)
                home = pw->pw_dir;
        }
        if (home) {
            char *buf = malloc(PATHLEN);
            snprintf(buf, PATHLEN, "%s%s", home, path + 1);
            return buf;
        }
    }
    return path ? strdup(path) : NULL;
}

static int
copy_file_simple(const char *src, const char *dst)
{
    int sfd = open(src, O_RDONLY);
    if (sfd < 0)
        return -1;
    int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, DEFAULT_FILE_CREATE_PERM);
    if (dfd < 0) {
        close(sfd);
        return -1;
    }
    char buf[8192];
    ssize_t n;
    while ((n = read(sfd, buf, sizeof(buf))) > 0) {
        if (write(dfd, buf, n) != n) {
            close(sfd);
            close(dfd);
            return -1;
        }
    }
    close(sfd);
    close(dfd);
    return 0;
}

static int
convert_one_file(const char *path, const char *boardname, const char *label,
                 int *out_total, int *out_converted)
{
    if (!dashf(path))
        return 0;

    int fd = open(path, opt_dry_run ? O_RDONLY : O_RDWR);
    if (fd < 0) {
        if (errno == ENOENT)
            return 0;
        fprintf(stderr, "[%s %s] error opening %s: %s\n",
                boardname, label, path, strerror(errno));
        return -1;
    }

    if (flock(fd, opt_dry_run ? LOCK_SH : LOCK_EX) < 0) {
        fprintf(stderr, "[%s %s] error locking %s: %s\n",
                boardname, label, path, strerror(errno));
        close(fd);
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        return -1;
    }

    if (st.st_size == 0) {
        if (opt_verbose)
            printf("[%s %s] empty file, skipping\n", boardname, label);
        close(fd);
        return 0;
    }

    if (st.st_size % RECORD_SIZE != 0) {
        fprintf(stderr, "[%s %s] warning: %s size (%ld bytes) is not a multiple of %d\n",
                boardname, label, path, (long)st.st_size, RECORD_SIZE);
    }

    int num_recs = (int)(st.st_size / RECORD_SIZE);
    int unconv_evidence = 0;
    int conv_evidence = 0;

    /* Pre-scan to detect whether this file has already been converted */
    for (int i = 0; i < num_recs; ) {
        int batch = (num_recs - i > CHUNK_RECORDS) ? CHUNK_RECORDS : (num_recs - i);
        off_t offset = (off_t)i * RECORD_SIZE;
        ssize_t bytes_to_read = (ssize_t)batch * RECORD_SIZE;
        uint8_t buf[CHUNK_RECORDS * RECORD_SIZE];

        if (pread(fd, buf, bytes_to_read, offset) != bytes_to_read) {
            fprintf(stderr, "[%s %s] error reading pre-scan chunk at offset %ld\n",
                    boardname, label, (long)offset);
            close(fd);
            return -1;
        }

        for (int j = 0; j < batch; j++) {
            uint8_t *rec = buf + j * RECORD_SIZE;
            uint8_t b32 = rec[OFF_PAD];
            uint8_t b33 = rec[OFF_REC];

            if (b32 == 0 && b33 == 0)
                continue;

            /* Legacy recommend:
             * Positive recommend (1..100) is at b33, b32 is pad (usually 0).
             * Negative recommend (-100..-1) is at b33, b32 is pad (usually 0, NOT 0xFF).
             */
            if (b33 > 0 && b33 <= 100 && b32 == 0) {
                unconv_evidence++;
            } else if ((int8_t)b33 < 0 && (int8_t)b33 >= -100 && b32 == 0) {
                unconv_evidence++;
            }

            /* Converted rating (little-endian int16_t):
             * Positive rating (1..100) has b32 in [1, 100] and b33 == 0.
             * Negative rating (-100..-1) has b32 in [0x9C, 0xFF] and b33 == 0xFF.
             */
            if (b33 == 0 && b32 > 0 && b32 <= 100) {
                conv_evidence++;
            } else if (b33 == 0xFF && b32 >= 0x9C) {
                conv_evidence++;
            }
        }
        i += batch;
    }

    if (conv_evidence > 0 && unconv_evidence == 0) {
        if (!opt_force) {
            if (opt_verbose) {
                printf("[%s %s] already converted (%d rating records, 0 legacy recommend); skipping (use -f to override)\n",
                       boardname, label, conv_evidence);
            }
            *out_total += num_recs;
            close(fd);
            return 0;
        }
    }

    if (opt_backup && !opt_dry_run) {
        char bak_path[PATHLEN];
        snprintf(bak_path, sizeof(bak_path), "%s.bak", path);
        if (copy_file_simple(path, bak_path) < 0) {
            fprintf(stderr, "[%s %s] warning: failed to create backup %s\n",
                    boardname, label, bak_path);
        } else if (opt_verbose) {
            printf("[%s %s] backup created: %s\n", boardname, label, bak_path);
        }
    }

    int modified = 0;
    for (int i = 0; i < num_recs; ) {
        int batch = (num_recs - i > CHUNK_RECORDS) ? CHUNK_RECORDS : (num_recs - i);
        off_t offset = (off_t)i * RECORD_SIZE;
        ssize_t bytes_to_read = (ssize_t)batch * RECORD_SIZE;
        uint8_t buf[CHUNK_RECORDS * RECORD_SIZE];

        if (pread(fd, buf, bytes_to_read, offset) != bytes_to_read) {
            fprintf(stderr, "[%s %s] error reading records at offset %ld\n",
                    boardname, label, (long)offset);
            break;
        }

        int batch_modified = 0;
        for (int j = 0; j < batch; j++) {
            uint8_t *rec = buf + j * RECORD_SIZE;
            int8_t old_rec = (int8_t)rec[OFF_REC];
            uint8_t old_pad = rec[OFF_PAD];

            /* Already 0, with clean pad: nothing to change */
            if (old_rec == 0 && old_pad == 0)
                continue;

            int16_t new_rating = (int16_t)old_rec;
            uint8_t new_bytes[2];
            memcpy(new_bytes, &new_rating, sizeof(int16_t));

            if (rec[OFF_PAD] != new_bytes[0] || rec[OFF_REC] != new_bytes[1]) {
                rec[OFF_PAD] = new_bytes[0];
                rec[OFF_REC] = new_bytes[1];
                batch_modified++;
                modified++;
            }
        }

        if (batch_modified > 0 && !opt_dry_run) {
            if (pwrite(fd, buf, bytes_to_read, offset) != bytes_to_read) {
                fprintf(stderr, "[%s %s] error writing records at offset %ld\n",
                        boardname, label, (long)offset);
                break;
            }
        }
        i += batch;
    }

    if (modified > 0 || opt_verbose) {
        printf("[%s %s] %s %d of %d records%s\n",
               boardname, label,
               opt_dry_run ? "would convert" : "converted",
               modified, num_recs,
               opt_dry_run ? " [DRY-RUN]" : "");
    }

    *out_total += num_recs;
    *out_converted += modified;
    close(fd);
    return modified > 0 ? 1 : 0;
}

static int
convert_board(const boardheader_t *bh, int bid, int *total_recs, int *converted_recs)
{
    char fn_dir[PATHLEN];
    char fn_names[PATHLEN];
    char fn_bottom[PATHLEN];

    if (!bh || !bh->brdname[0])
        return 0;

    int files_changed = 0;

    /* 1. Main article index (.DIR) */
    setbfile(fn_dir, bh->brdname, FN_DIR);
    if (dashf(fn_dir)) {
        if (convert_one_file(fn_dir, bh->brdname, FN_DIR, total_recs, converted_recs) > 0)
            files_changed++;
    } else if (opt_verbose) {
        printf("[#%d %s] %s not found, skipping\n", bid, bh->brdname, fn_dir);
    }

    /* 2. Board digest index (.Names) */
    setbfile(fn_names, bh->brdname, ".Names");
    if (dashf(fn_names)) {
        if (convert_one_file(fn_names, bh->brdname, ".Names", total_recs, converted_recs) > 0)
            files_changed++;
    }

    /* 3. Legacy bottom posts index (.DIR.bottom, if present) */
    setbfile(fn_bottom, bh->brdname, FN_DIR ".bottom");
    if (dashf(fn_bottom)) {
        if (convert_one_file(fn_bottom, bh->brdname, FN_DIR ".bottom", total_recs, converted_recs) > 0)
            files_changed++;
    }

    return files_changed;
}

static void
usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [options] [boardname ...]\n"
            "Convert board .DIR index files from legacy recommend (char pad + char recommend)\n"
            "to int16_t rating.\n\n"
            "Options:\n"
            "  -B, --brd <path>  Path to .BRD file (default: ~/.BRD, " BBSHOME "/" FN_BOARD ", or ./" FN_BOARD ")\n"
            "  -n, --dry-run     Inspect and count records without modifying files\n"
            "  -f, --force       Force conversion even if files appear already converted\n"
            "  -b, --backup      Create .DIR.bak before modifying files\n"
            "  -v, --verbose     Verbose output (show skipped boards and unchanged files)\n"
            "  -h, --help        Show this help message\n\n"
            "If specific board names are provided, only those boards are converted.\n"
            "Otherwise, all boards listed in .BRD are scanned.\n",
            prog);
}

int
main(int argc, char **argv)
{
    const char *brd_arg = NULL;
    int argi = 1;
    char **target_boards = NULL;
    int num_target_boards = 0;

    while (argi < argc) {
        if (strcmp(argv[argi], "-n") == 0 || strcmp(argv[argi], "--dry-run") == 0) {
            opt_dry_run = 1;
        } else if (strcmp(argv[argi], "-f") == 0 || strcmp(argv[argi], "--force") == 0) {
            opt_force = 1;
        } else if (strcmp(argv[argi], "-b") == 0 || strcmp(argv[argi], "--backup") == 0) {
            opt_backup = 1;
        } else if (strcmp(argv[argi], "-v") == 0 || strcmp(argv[argi], "--verbose") == 0) {
            opt_verbose = 1;
        } else if (strcmp(argv[argi], "-h") == 0 || strcmp(argv[argi], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if ((strcmp(argv[argi], "-B") == 0 || strcmp(argv[argi], "--brd") == 0) &&
                   argi + 1 < argc) {
            brd_arg = argv[++argi];
        } else if (argv[argi][0] != '-') {
            /* If this is the first non-option and ends with .BRD or is named .BRD, treat as brd */
            if (!brd_arg && (strstr(argv[argi], ".BRD") != NULL || strcmp(argv[argi], FN_BOARD) == 0)) {
                brd_arg = argv[argi];
            } else {
                /* Target board name */
                target_boards = &argv[argi];
                num_target_boards = argc - argi;
                break;
            }
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[argi]);
            usage(argv[0]);
            return 1;
        }
        argi++;
    }

    char *brd_path = NULL;
    if (brd_arg) {
        brd_path = expand_home(brd_arg);
    } else {
        char *home_brd = expand_home("~/" FN_BOARD);
        if (home_brd && dashf(home_brd)) {
            brd_path = home_brd;
        } else {
            free(home_brd);
            if (dashf(BBSHOME "/" FN_BOARD)) {
                brd_path = strdup(BBSHOME "/" FN_BOARD);
            } else if (dashf(FN_BOARD)) {
                brd_path = strdup(FN_BOARD);
            } else {
                brd_path = expand_home("~/" FN_BOARD);
            }
        }
    }

    if (!brd_path) {
        fprintf(stderr, "Cannot determine path to %s\n", FN_BOARD);
        return 1;
    }

    int fd_brd = open(brd_path, O_RDONLY);
    if (fd_brd < 0) {
        fprintf(stderr, "Error opening %s: %s\n", brd_path, strerror(errno));
        free(brd_path);
        return 1;
    }

    if (flock(fd_brd, LOCK_SH) < 0) {
        perror("flock .BRD");
        close(fd_brd);
        free(brd_path);
        return 1;
    }

    struct stat st;
    if (fstat(fd_brd, &st) < 0) {
        perror("fstat .BRD");
        close(fd_brd);
        free(brd_path);
        return 1;
    }

    /* Chdir to directory containing .BRD so relative paths (boards/<C>/<board>/...) resolve */
    const char *slash = strrchr(brd_path, '/');
    if (slash) {
        char dir[PATHLEN];
        size_t len = slash - brd_path;
        if (len == 0)
            len = 1;
        if (len < sizeof(dir)) {
            memcpy(dir, brd_path, len);
            dir[len] = '\0';
            if (chdir(dir) < 0) {
                perror("chdir");
                close(fd_brd);
                free(brd_path);
                return 1;
            }
        }
    }

    int total_boards = (int)(st.st_size / sizeof(boardheader_t));
    printf("Scanning boards from %s (%d boards)%s...\n",
           brd_path, total_boards, opt_dry_run ? " [DRY-RUN]" : "");

    int boards_processed = 0;
    int boards_with_changes = 0;
    int total_records = 0;
    int converted_records = 0;

    for (int bid = 1; bid <= total_boards; bid++) {
        boardheader_t bh;
        off_t offset = (off_t)(bid - 1) * sizeof(boardheader_t);

        if (pread(fd_brd, &bh, sizeof(bh), offset) != (ssize_t)sizeof(bh)) {
            fprintf(stderr, "Error reading board record %d\n", bid);
            break;
        }

        if (bh.brdname[0] == '\0')
            continue;

        if (num_target_boards > 0) {
            int matched = 0;
            for (int k = 0; k < num_target_boards; k++) {
                if (strcasecmp(bh.brdname, target_boards[k]) == 0) {
                    matched = 1;
                    break;
                }
            }
            if (!matched)
                continue;
        }

        boards_processed++;
        int changes = convert_board(&bh, bid, &total_records, &converted_records);
        if (changes > 0)
            boards_with_changes++;
    }

    close(fd_brd);
    free(brd_path);

    printf("\nFinished%s. Boards scanned: %d, Boards with changes: %d, Records scanned: %d, Records converted: %d.\n",
           opt_dry_run ? " [DRY-RUN]" : "",
           boards_processed, boards_with_changes, total_records, converted_records);

    return 0;
}
