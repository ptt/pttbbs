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

static void
usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [options] [path_to_.BRD]\n"
            "Extract legacy boardheader_t posttype categories into boards/<B>/<board>/posttype files,\n"
            "and rename disabled template files (postsample.* / sample.*) to *.bak.\n\n"
            "Options:\n"
            "  -n, --dry-run   Show what would be created without creating files\n"
            "  -v, --verbose   Show detailed migration info for each board\n"
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

    int fd = open(brd_path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error opening %s: %s\n", brd_path, strerror(errno));
        return 1;
    }

    if (flock(fd, LOCK_SH) < 0) {
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

    // If brd_path contains a directory component, chdir to that directory so relative
    // board paths like boards/<B>/<board>/... resolve correctly.
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
                close(fd);
                return 1;
            }
        }
    }

    int total_records = (int)(st.st_size / sizeof(boardheader_t));
    printf("Extracting posttype from %s (%d boards)%s...\n",
           brd_path, total_records, opt_dry_run ? " [DRY-RUN]" : "");

    int count_created = 0;
    int count_already_exists = 0;
    int count_no_posttype = 0;
    int count_empty = 0;
    int count_disabled_renamed = 0;

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

        char cats[8][32];
        int n_cats = 0;

        for (int j = 0; j < 8 && bh.deprecated_posttype[j * 4] != '\0'; j++) {
            char cat[5];
            snprintf(cat, sizeof(cat), "%.4s", bh.deprecated_posttype + j * 4);
            char *end = cat + strlen(cat) - 1;
            while (end >= cat && (*end == ' ' || *end == '\t')) {
                *end = '\0';
                end--;
            }
            if (cat[0] != '\0') {
                strlcpy(cats[n_cats++], cat, sizeof(cats[0]));
            }
        }

        char bdir[PATHLEN];
        setbpath(bdir, bh.brdname);
        int bdir_exists = dashd(bdir);

        char posttype_path[PATHLEN];
        setbfile(posttype_path, bh.brdname, FN_POSTTYPE);

        if (n_cats == 0) {
            count_no_posttype++;
        } else if (dashf(posttype_path)) {
            count_already_exists++;
            if (opt_verbose) {
                printf("[#%d %-13s] skipped: %s already exists\n",
                       i + 1, bh.brdname, posttype_path);
            }
        } else {
            if (!opt_dry_run) {
                if (!bdir_exists) {
                    Mkdir(bdir);
                    bdir_exists = 1;
                }

                FILE *fp = fopen(posttype_path, "w");
                if (!fp) {
                    fprintf(stderr, "Error writing %s (%s): %s\n",
                            posttype_path, bh.brdname, strerror(errno));
                } else {
                    for (int k = 0; k < n_cats; k++) {
                        fprintf(fp, "%s\n", cats[k]);
                    }
                    fclose(fp);
                    count_created++;
                }
            } else {
                count_created++;
            }

            if (opt_verbose && (opt_dry_run || dashf(posttype_path))) {
                printf("[#%d %-13s] created %s with %d categories:\n",
                       i + 1, bh.brdname, posttype_path, n_cats);
                for (int k = 0; k < n_cats; k++) {
                    printf("    %d. [%s]\n", k + 1, cats[k]);
                }
            }
        }

        if (bdir_exists) {
            unsigned char posttype_f = (unsigned char)bh.deprecated_posttype_f;
            const char * const tmpl_prefixes[] = {"postsample", "sample"};

            for (int j = 0; j < 8; j++) {
                int is_disabled = !(posttype_f & (1 << j)) || (n_cats > 0 && j >= n_cats);

                for (size_t pfx = 0; pfx < sizeof(tmpl_prefixes) / sizeof(tmpl_prefixes[0]); pfx++) {
                    char tmpl_path[PATHLEN];
                    setbnfile(tmpl_path, bh.brdname, tmpl_prefixes[pfx], j);

                    if (!dashf(tmpl_path))
                        continue;

                    if (is_disabled) {
                        char bak_path[PATHLEN];
                        snprintf(bak_path, sizeof(bak_path), "%s.bak", tmpl_path);

                        if (opt_dry_run) {
                            printf("[#%d %-13s] would rename disabled %s -> %s\n",
                                   i + 1, bh.brdname, tmpl_path, bak_path);
                        } else {
                            if (rename(tmpl_path, bak_path) < 0) {
                                fprintf(stderr, "[#%d %-13s] Error renaming %s to %s: %s\n",
                                        i + 1, bh.brdname, tmpl_path, bak_path, strerror(errno));
                            } else {
                                printf("[#%d %-13s] renamed disabled %s -> %s\n",
                                       i + 1, bh.brdname, tmpl_path, bak_path);
                            }
                        }
                        count_disabled_renamed++;
                    } else if (opt_verbose) {
                        printf("[#%d %-13s] keeping active template: %s\n",
                               i + 1, bh.brdname, tmpl_path);
                    }
                }
            }
        }
    }

    close(fd);

    printf("\nFinished extracting posttypes from %s%s:\n"
           "  Total records:              %d\n"
           "  Created posttype:           %d\n"
           "  Already existed:            %d\n"
           "  No posttype in .BRD:        %d\n"
           "  Empty records:              %d\n"
           "  Disabled templates %s: %d\n",
           brd_path, opt_dry_run ? " (DRY-RUN)" : "",
           total_records, count_created, count_already_exists,
           count_no_posttype, count_empty,
           opt_dry_run ? "to rename" : "renamed",
           count_disabled_renamed);

    return 0;
}
