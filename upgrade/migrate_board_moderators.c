#define _UTIL_C_
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bbs.h"

static int opt_dry_run = 0;
static int opt_verbose = 0;
static int opt_quiet = 0;
static int opt_force = 0;
static int opt_backup = 1;

static void
usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [options] [path_to_.BRD]\n"
            "Migrate boardheader_t BM to numeric user_t index and relocate limit fields in .BRD.\n\n"
            "Options:\n"
            "  -n, --dry-run          Show what would be migrated without modifying files or SHM\n"
            "  -v, --verbose          Show detailed migration info for each board\n"
            "  -q, --quiet            Quiet mode, print summary only\n"
            "  -f, --force            Force updating records even if already migrated\n"
            "  --no-backup            Do not create .bak backup before modifying .BRD\n"
            "  -b, --bbshome <path>   BBS home directory (default: " BBSHOME ")\n"
            "  -h, --help             Display this help message\n\n"
            "Note: The BBS server must be stopped before running this program.\n"
            "Ensure upgrade/board_posttype_to_file has been executed before this upgrade,\n"
            "as legacy posttype fields in boardheader_t will be overwritten.\n"
            "Requires active BBS shared memory (SHM). If not active, run 'shmctl init' first.\n\n"
            "If path_to_.BRD is omitted, " BBSHOME "/" FN_BOARD " or ./" FN_BOARD " is used.\n",
            prog);
}

static int
parse_moderators(const char *input, user_t moderators[MAX_BMs])
{
    int uids[MAX_BMs];
    int n = parseBMlist(input, uids);
    int count = 0;
    memset(moderators, 0, sizeof(user_t) * MAX_BMs);
    for (int i = 0; i < n && i < MAX_BMs; i++) {
        if (uids[i] > 0 && uids[i] <= MAX_USERS) {
            userec_t u;
            if (passwd_query(uids[i], &u) == 0) {
                moderators[count].unum = uids[i];
                moderators[count].firstlogin = u.firstlogin;
                count++;
            }
        }
    }
    return count;
}

static void
clean_moderator_notes(char *moderator_notes, const user_t moderators[MAX_BMs])
{
    if (!moderator_notes || moderator_notes[0] == '\0')
        return;

    // 1. Replace Big-5 full-width spaces (0xa1 0x40) with ASCII spaces
    for (int i = 0; moderator_notes[i] != '\0'; ) {
        unsigned char c1 = (unsigned char)moderator_notes[i];
        if (big5_is_valid_lead(c1) && moderator_notes[i + 1] != '\0') {
            unsigned char c2 = (unsigned char)moderator_notes[i + 1];
            if (c1 == 0xa1 && c2 == 0x40) {
                moderator_notes[i] = ' ';
                moderator_notes[i + 1] = ' ';
            }
            i += 2;
        } else {
            i++;
        }
    }

    // 2. Strip out user IDs present in moderators (case-insensitive)
    for (int i = 0; moderator_notes[i] != '\0'; ) {
        unsigned char c = (unsigned char)moderator_notes[i];
        if (big5_is_valid_lead(c) && moderator_notes[i + 1] != '\0') {
            i += 2;
        } else if (isalnum(c)) {
            int start = i;
            while (moderator_notes[i] != '\0' && isalnum((unsigned char)moderator_notes[i])) {
                i++;
            }
            int tok_len = i - start;
            for (int m = 0; m < MAX_BMs; m++) {
                int uid = moderators[m].unum;
                if (uid > 0 && uid <= MAX_USERS) {
                    char m_id_buf[IDLEN + 1];
                    const char *m_id = getuserid(uid);
                    if (!m_id || !m_id[0]) {
                        userec_t mu;
                        if (passwd_query(uid, &mu) == 0 && mu.userid[0]) {
                            strlcpy(m_id_buf, mu.userid, sizeof(m_id_buf));
                            m_id = m_id_buf;
                        }
                    }
                    if (m_id && (int)strlen(m_id) == tok_len &&
                        strncasecmp(&moderator_notes[start], m_id, tok_len) == 0) {
                        for (int k = start; k < i; k++) {
                            moderator_notes[k] = ' ';
                        }
                        break;
                    }
                }
            }
        } else {
            i++;
        }
    }

    // 3. Remove delimiters: '/', '\', '.', ',' -> replace with space
    for (int i = 0; moderator_notes[i] != '\0'; ) {
        unsigned char c = (unsigned char)moderator_notes[i];
        if (big5_is_valid_lead(c) && moderator_notes[i + 1] != '\0') {
            i += 2;
        } else {
            if (c == '/' || c == '\\' || c == '.' || c == ',') {
                moderator_notes[i] = ' ';
            }
            i++;
        }
    }

    // 4. Remove empty brackets: [] and ()
    for (int i = 0; moderator_notes[i] != '\0'; ) {
        unsigned char c = (unsigned char)moderator_notes[i];
        if (big5_is_valid_lead(c) && moderator_notes[i + 1] != '\0') {
            i += 2;
        } else if (c == '[' || c == '(') {
            char close_c = (c == '[') ? ']' : ')';
            int j = i + 1;
            while (moderator_notes[j] == ' ')
                j++;
            if (moderator_notes[j] == close_c) {
                for (int k = i; k <= j; k++)
                    moderator_notes[k] = ' ';
                i = j + 1;
            } else {
                i++;
            }
        } else {
            i++;
        }
    }

    // 5. Collapse multiple spaces and trim leading/trailing whitespace
    char tmp[sizeof(((boardheader_t *)0)->moderator_notes) + 1];
    int out_len = 0;
    int in_space = 1;

    for (int i = 0; moderator_notes[i] != '\0'; ) {
        unsigned char c = (unsigned char)moderator_notes[i];
        if (big5_is_valid_lead(c) && moderator_notes[i + 1] != '\0') {
            if (out_len + 2 < (int)sizeof(tmp)) {
                tmp[out_len++] = moderator_notes[i];
                tmp[out_len++] = moderator_notes[i + 1];
            }
            in_space = 0;
            i += 2;
        } else if (c <= ' ' || isspace(c)) {
            if (!in_space) {
                if (out_len + 1 < (int)sizeof(tmp)) {
                    tmp[out_len++] = ' ';
                }
                in_space = 1;
            }
            i++;
        } else {
            if (out_len + 1 < (int)sizeof(tmp)) {
                tmp[out_len++] = moderator_notes[i];
            }
            in_space = 0;
            i++;
        }
    }

    if (out_len > 0 && tmp[out_len - 1] == ' ') {
        out_len--;
    }
    tmp[out_len] = '\0';
    if ((int)strlen(tmp) > IDLEN) {
        tmp[IDLEN] = '\0';
        mbs_safe_trim(tmp);
        int l = strlen(tmp);
        while (l > 0 && tmp[l - 1] == ' ')
            tmp[--l] = '\0';
    }
    strlcpy(moderator_notes, tmp, sizeof(((boardheader_t *)0)->moderator_notes));
}

static int
fill_board_moderators(boardheader_t *bh)
{
    int n = parse_moderators(bh->moderator_notes, bh->moderators);
    clean_moderator_notes(bh->moderator_notes, bh->moderators);
    return n;
}

int
main(int argc, char **argv)
{
    const char *brd_path = NULL;
    char opt_bbshome[PATHLEN] = "";

    static struct option long_options[] = {
        {"dry-run",   no_argument,       0, 'n'},
        {"verbose",   no_argument,       0, 'v'},
        {"quiet",     no_argument,       0, 'q'},
        {"force",     no_argument,       0, 'f'},
        {"no-backup", no_argument,       0, 'B'},
        {"bbshome",   required_argument, 0, 'b'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "nvqfb:h", long_options, NULL)) != -1) {
        switch (c) {
        case 'n':
            opt_dry_run = 1;
            break;
        case 'v':
            opt_verbose = 1;
            break;
        case 'q':
            opt_quiet = 1;
            break;
        case 'f':
            opt_force = 1;
            break;
        case 'B':
            opt_backup = 0;
            break;
        case 'b':
            strlcpy(opt_bbshome, optarg, sizeof(opt_bbshome));
            break;
        case 'h':
            usage(argv[0]);
            return 0;
        default:
            usage(argv[0]);
            return 1;
        }
    }

    if (optind < argc) {
        brd_path = argv[optind];
    }

    now = time(NULL);

    const char *bbshome = opt_bbshome[0] ? opt_bbshome : BBSHOME;
    if (chdir(bbshome) != 0) {
        if (opt_verbose) {
            fprintf(stderr, "Notice: cannot chdir to %s: %s\n", bbshome, strerror(errno));
        }
    }

    char default_brd[PATHLEN];
    if (!brd_path) {
        if (dashf(FN_BOARD)) {
            brd_path = FN_BOARD;
        } else {
            snprintf(default_brd, sizeof(default_brd), "%s/%s", bbshome, FN_BOARD);
            brd_path = default_brd;
        }
    }

    attach_SHM();
    if (!SHM) {
        fprintf(stderr,
                "Error: Failed to attach to BBS shared memory (SHM).\n"
                "BM resolution requires SHM. Please ensure SHM is initialized (run 'shmctl init').\n");
        return 1;
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
    if (!opt_quiet) {
        printf("Migrating board moderators and limits in %s (%d boards)%s...\n",
               brd_path, total_records, opt_dry_run ? " [DRY-RUN]" : "");
    }

    if (!opt_dry_run && opt_backup) {
        char bak_path[PATHLEN + 8];
        snprintf(bak_path, sizeof(bak_path), "%s.bak", brd_path);
        if (copy_file(brd_path, bak_path) == 0) {
            if (!opt_quiet) {
                printf("Backup created: %s\n", bak_path);
            }
        } else {
            fprintf(stderr, "Warning: failed to create backup %s (%s)\n",
                    bak_path, strerror(errno));
        }
    }

    int count_migrated = 0;
    int count_already_ok = 0;
    int count_empty = 0;
    int total_mods_populated = 0;
    int total_unresolved_tokens = 0;

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

        boardheader_t new_bh = bh;
        int changed = 0;

        int is_legacy = (bh._vote_limit_badpost != 0 ||
                         bh._fastrecommend_pause != 0 ||
                         bh._post_limit_badpost != 0);

        if (is_legacy) {
            new_bh.vote_limit_badpost = bh._vote_limit_badpost;
            new_bh.fastrecommend_pause = bh._fastrecommend_pause;
            new_bh.post_limit_badpost = bh._post_limit_badpost;
            changed = 1;
        }

        /* Clear legacy limit and pad fields */
        new_bh.pad[0] = '\0';
        new_bh.pad[1] = '\0';
        new_bh._fastrecommend_pause = 0;
        new_bh._vote_limit_badpost = 0;
        new_bh._post_limit_badpost = 0;
        new_bh.pad3[0] = '\0';
        new_bh.pad3[1] = '\0';
        new_bh.pad3[2] = '\0';

        /* Check if board was already migrated with valid moderators */
        int already_migrated = 0;
        if (!is_legacy && !opt_force) {
            int valid_mods = 0;
            int invalid_mods = 0;
            for (int m = 0; m < MAX_BMs; m++) {
                int uid = bh.moderators[m].unum;
                if (uid == 0)
                    continue;
                if (uid > 0 && uid <= MAX_USERS) {
                    userec_t u;
                    if (passwd_query(uid, &u) == 0 && u.firstlogin != 0 &&
                        u.firstlogin == bh.moderators[m].firstlogin) {
                        valid_mods++;
                        continue;
                    }
                }
                invalid_mods++;
            }
            if (valid_mods > 0 && invalid_mods == 0) {
                already_migrated = 1;
            }
        }

        if (already_migrated) {
            clean_moderator_notes(new_bh.moderator_notes, new_bh.moderators);
        } else {
            fill_board_moderators(&new_bh);
        }

        if (memcmp(new_bh.moderators, bh.moderators, sizeof(new_bh.moderators)) != 0) {
            changed = 1;
        }
        if (strcmp(new_bh.moderator_notes, bh.moderator_notes) != 0) {
            changed = 1;
        }

        if (memcmp(&new_bh, &bh, sizeof(boardheader_t)) != 0) {
            changed = 1;
        }

        /* Count populated moderators */
        int mods_in_board = 0;
        for (int m = 0; m < MAX_BMs; m++) {
            if (new_bh.moderators[m].unum > 0)
                mods_in_board++;
        }
        total_mods_populated += mods_in_board;

        /* Check for any BM tokens that could not be resolved */
        char unres_toks[MAX_BMs][IDLEN + 1];
        int unres_cnt = 0;
        if (bh.moderator_notes[0] != '\0') {
            char bm_copy[sizeof(bh.moderator_notes) + 32];
            strlcpy(bm_copy, bh.moderator_notes, sizeof(bm_copy));
            for (int k = 0; bm_copy[k] != '\0'; ) {
                unsigned char c = (unsigned char)bm_copy[k];
                if (big5_is_valid_lead(c) && bm_copy[k + 1] != '\0') {
                    bm_copy[k] = ' ';
                    bm_copy[k + 1] = ' ';
                    k += 2;
                } else if (!isalnum(c)) {
                    bm_copy[k] = ' ';
                    k++;
                } else {
                    k++;
                }
            }
            char *saveptr = NULL;
            for (char *tok = strtok_r(bm_copy, " ", &saveptr);
                 tok != NULL;
                 tok = strtok_r(NULL, " ", &saveptr)) {
                if (searchuser(tok, NULL) == 0 && unres_cnt < MAX_BMs) {
                    strlcpy(unres_toks[unres_cnt], tok, sizeof(unres_toks[0]));
                    unres_cnt++;
                }
            }
        }
        total_unresolved_tokens += unres_cnt;

        if (changed) {
            if (!opt_dry_run) {
                if (pwrite(fd, &new_bh, sizeof(new_bh), offset) != (ssize_t)sizeof(new_bh)) {
                    fprintf(stderr, "Error writing record %d (%s): %s\n",
                            i + 1, bh.brdname, strerror(errno));
                    break;
                }
            }
            if (SHM && (i + 1) >= 1 && (i + 1) <= MAX_BOARD) {
                SHM->bcache[i] = new_bh;
                buildBMcache(i + 1);
            }
            count_migrated++;
            if (!opt_quiet && (opt_verbose || is_legacy || mods_in_board > 0 || unres_cnt > 0 || changed)) {
                if (strcmp(bh.moderator_notes, new_bh.moderator_notes) != 0) {
                    printf("[#%d %-13s] migrated: BM=\"%s\" -> \"%s\" (%d moderators)",
                           i + 1, bh.brdname, bh.moderator_notes, new_bh.moderator_notes, mods_in_board);
                } else {
                    printf("[#%d %-13s] migrated: BM=\"%s\" (%d moderators)",
                           i + 1, bh.brdname, bh.moderator_notes, mods_in_board);
                }
                if (mods_in_board > 0) {
                    printf(" [mods:");
                    for (int m = 0; m < MAX_BMs; m++) {
                        if (new_bh.moderators[m].unum > 0) {
                            const char *uid_name = getuserid(new_bh.moderators[m].unum);
                            printf(" %s(uid=%d,firstlogin=%u)",
                                   uid_name ? uid_name : "?",
                                   new_bh.moderators[m].unum,
                                   (unsigned)new_bh.moderators[m].firstlogin);
                        }
                    }
                    printf("]");
                }
                if (is_legacy) {
                    printf(" [limits: badpost=%d, pause=%d, post_badpost=%d]",
                           new_bh.vote_limit_badpost, new_bh.fastrecommend_pause,
                           new_bh.post_limit_badpost);
                }
                if (unres_cnt > 0) {
                    printf(" (unresolved:");
                    for (int u = 0; u < unres_cnt; u++) {
                        printf(" %s", unres_toks[u]);
                    }
                    printf(")");
                }
                printf("\n");
            }
        } else {
            count_already_ok++;
            if (opt_verbose) {
                printf("[#%d %-13s] already ok: BM=\"%s\" (%d moderators)\n",
                       i + 1, bh.brdname, bh.moderator_notes, mods_in_board);
            }
        }
    }

    close(fd);

    if (!opt_quiet) {
        printf("\nFinished moderator migration for %s%s:\n"
               "  Total records:            %d\n"
               "  Migrated boards:          %d\n"
               "  Already up to date:       %d\n"
               "  Empty records:            %d\n"
               "  Moderators populated:     %d\n"
               "  Unresolved BM tokens:     %d\n",
               brd_path, opt_dry_run ? " (DRY-RUN)" : "",
               total_records, count_migrated, count_already_ok, count_empty,
               total_mods_populated, total_unresolved_tokens);
    }

    return 0;
}
