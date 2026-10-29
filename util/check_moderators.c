#define _UTIL_C_
#include "bbs.h"

#include <getopt.h>

enum {
    ROLE_HAS_BM         = (1 << 0),
    ROLE_HAS_GROUP_OP   = (1 << 1),
    ROLE_HAS_GROUP_LEAD = (1 << 2),
};

typedef struct {
    int boards_scanned;
    int regular_boards;
    int sub_group_boards;
    int top_group_boards;
    int boards_cleaned;
    int invalid_mods_removed;
    int firstlogin_backfilled;
    int unique_managers;
    int distinct_bms;
    int distinct_group_ops;
    int distinct_group_leads;
} board_stats_t;

typedef struct {
    const uint8_t *user_roles;
    int write_mode;
    int verbose;
    int quiet;
    int skip_sysop;
    int no_role_only;

    int total_accounts;
    int sysop_skipped;
    int users_modified;
    int users_added_perm;
    int users_cleared_perm;
    int bm_added;
    int bm_cleared;
    int group_op_added;
    int group_op_cleared;
    int group_lead_added;
    int group_lead_cleared;
} scan_ctx_t;

static void print_usage(const char *progname)
{
    printf("Usage: %s [options]\n\n", progname);
    printf("Maintain board moderators and user permissions in PTT BBS:\n");
    printf("  1. Scans all boards in .BRD and removes invalid moderators:\n");
    printf("     - Missing or deleted accounts in .PASSWDS\n");
    printf("     - Re-registered accounts (firstlogin mismatch)\n");
    printf("     - Out-of-range uids\n");
    printf("     - Backfills missing firstlogin for valid moderators\n");
    printf("  2. Scans all accounts in .PASSWDS and synchronizes permissions:\n");
    printf("     - Adds PERM_BM, PERM_GROUP_OP, PERM_GROUP_LEAD for active roles\n");
    printf("     - Removes obsolete permissions from users who no longer hold positions\n\n");
    printf("By default, %s runs in DRY-RUN mode and does not modify .BRD or .PASSWDS.\n", progname);
    printf("Use -w or --write to apply changes.\n\n");
    printf("Options:\n");
    printf("  -w, --write            Apply changes to .BRD and .PASSWDS (default: dry-run)\n");
    printf("  -f, --force            Alias for -w / --write\n");
    printf("  -n, --dry-run          Explicitly run in dry-run mode (inspection only)\n");
    printf("  -v, --verbose          Verbose output (show boards, moderators, and details)\n");
    printf("  -q, --quiet            Quiet mode (suppress per-item output, print summary only)\n");
    printf("  -s, --include-sysop    Check and modify SYSOP / PERM_BOARDS accounts (default: skip)\n");
    printf("  -0, --no-role-only     Only clear permissions for users holding NO roles at all\n");
    printf("      --prune-only       Only prune board moderators (do not modify .PASSWDS)\n");
    printf("      --perm-only        Only update .PASSWDS permissions (do not modify .BRD)\n");
    printf("  -b, --bbshome <dir>    Set BBS home directory (default: BBSHOME)\n");
    printf("  -h, --help             Display this help message and exit\n");
}

static char *format_moderators_array(const user_t *mods, char *buf, size_t buflen)
{
    if (!buf || buflen == 0)
        return buf;

    buf[0] = '\0';
    for (int m = 0; m < MAX_BMs; m++) {
        int uid = mods[m].unum;
        if (uid <= 0 || uid > MAX_USERS)
            continue;

        const char *name = getuserid(uid);
        if (!name || !name[0]) {
            userec_t u;
            if (passwd_query(uid, &u) == 0 && u.userid[0])
                name = u.userid;
        }

        if (name && name[0]) {
            if (buf[0] != '\0')
                strlcat(buf, "/", buflen);
            strlcat(buf, name, buflen);
        }
    }
    return buf;
}

static void scan_and_clean_boards(uint8_t *user_roles, board_stats_t *stats,
                                  int write_mode, int verbose, int quiet)
{
    int nboards = num_boards();
    if (nboards <= 0 || nboards > MAX_BOARD)
        nboards = MAX_BOARD;

    for (int bid = 1; bid <= nboards; bid++) {
        boardheader_t bh;
        if (get_boardheader(&bh, bid) <= 0)
            continue;
        if (!bh.brdname[0] || (bh.brdattr & BRD_SYMBOLIC))
            continue;

        stats->boards_scanned++;

        uint8_t role_bit = 0;
        if (bh.brdattr & BRD_GROUPBOARD) {
            if (bh.gid == 1) {
                role_bit = ROLE_HAS_GROUP_LEAD;
                stats->top_group_boards++;
            } else {
                role_bit = ROLE_HAS_GROUP_OP;
                stats->sub_group_boards++;
            }
        } else {
            role_bit = ROLE_HAS_BM;
            stats->regular_boards++;
        }

        user_t new_mods[MAX_BMs];
        memset(new_mods, 0, sizeof(new_mods));
        int new_m_count = 0;
        int board_changed = 0;
        int mods_removed_in_board = 0;

        char old_bm[BOARD_MODERATORS_FORMAT_MAX_LEN] = "";
        format_moderators_array(bh.moderators, old_bm, sizeof(old_bm));

        for (int m = 0; m < MAX_BMs; m++) {
            int uid = bh.moderators[m].unum;
            if (uid <= 0)
                continue;

            const char *remove_reason = NULL;
            userec_t u;
            memset(&u, 0, sizeof(u));

            if (uid > MAX_USERS) {
                remove_reason = "uid out of range";
            } else if (passwd_query(uid, &u) != 0 || !u.userid[0]) {
                remove_reason = "account not found in passwd";
            } else if (bh.moderators[m].firstlogin != 0 &&
                       u.firstlogin != bh.moderators[m].firstlogin) {
                remove_reason = "firstlogin mismatch (re-registered account)";
            }

            if (remove_reason) {
                board_changed = 1;
                mods_removed_in_board++;
                stats->invalid_mods_removed++;
                if (!quiet) {
                    const char *uname = u.userid[0] ? u.userid : getuserid(uid);
                    printf("[%s-BOARD] %-15s slot %d: removed invalid moderator '%s' (uid=%d): %s\n",
                           write_mode ? "CLEAN" : "DRYRUN",
                           bh.brdname, m + 1, uname ? uname : "(unknown)", uid, remove_reason);
                }
                continue;
            }

            // Valid moderator
            new_mods[new_m_count].unum = uid;
            if (bh.moderators[m].firstlogin == 0 && u.firstlogin != 0) {
                new_mods[new_m_count].firstlogin = u.firstlogin;
                stats->firstlogin_backfilled++;
                board_changed = 1;
                if (verbose > 1) {
                    printf("  [BACKFILL] %-15s slot %d: backfilled firstlogin for '%s' (uid=%d)\n",
                           bh.brdname, m + 1, u.userid, uid);
                }
            } else {
                new_mods[new_m_count].firstlogin = bh.moderators[m].firstlogin;
            }
            new_m_count++;

            if (!user_roles[uid])
                stats->unique_managers++;
            user_roles[uid] |= role_bit;

            if (verbose > 1) {
                printf("  Board %-15s Role: %-10s User: %-12s (uid=%d)\n",
                       bh.brdname,
                       (role_bit == ROLE_HAS_GROUP_LEAD) ? "GROUP_LEAD" :
                       (role_bit == ROLE_HAS_GROUP_OP) ? "GROUP_OP" : "BM",
                       u.userid, uid);
            }
        }

        if (board_changed) {
            stats->boards_cleaned++;

            char new_bm[BOARD_MODERATORS_FORMAT_MAX_LEN] = "";
            format_moderators_array(new_mods, new_bm, sizeof(new_bm));

            if (!quiet && mods_removed_in_board > 0) {
                printf("[%s-BOARD] %-15s moderators updated: '%s' -> '%s'\n",
                       write_mode ? "CLEAN" : "DRYRUN",
                       bh.brdname,
                       old_bm[0] ? old_bm : "(none)",
                       new_bm[0] ? new_bm : "(none)");
            }

            if (write_mode) {
                memcpy(bh.moderators, new_mods, sizeof(bh.moderators));
                if (modify_boardheader(&bh, bid) == 0) {
                    reset_board(bid);
                    log_filef("log/check_moderators.log",
                              "[Board %s] Cleaned moderators: '%s' -> '%s'\n",
                              bh.brdname,
                              old_bm[0] ? old_bm : "(none)",
                              new_bm[0] ? new_bm : "(none)");
                } else {
                    fprintf(stderr, "Error: Failed to write boardheader for %s (bid=%d)\n",
                            bh.brdname, bid);
                }
            }
        }
    }

    for (int uid = 1; uid <= MAX_USERS; uid++) {
        if (user_roles[uid] & ROLE_HAS_BM)
            stats->distinct_bms++;
        if (user_roles[uid] & ROLE_HAS_GROUP_OP)
            stats->distinct_group_ops++;
        if (user_roles[uid] & ROLE_HAS_GROUP_LEAD)
            stats->distinct_group_leads++;
    }
}

static int check_user(void *ctx, int idx, userec_t *u)
{
    scan_ctx_t *s = (scan_ctx_t *)ctx;
    int unum = idx + 1;

    if (!u || !u->userid[0])
        return 0;

    s->total_accounts++;

    if (s->skip_sysop) {
        if ((u->userlevel & (PERM_SYSOP | PERM_BOARDS))
#if IS_ENABLED(CONFIG_SYSOP_ACCOUNT)
            || (strcasecmp(u->userid, str_sysop) == 0)
#endif
        ) {
            s->sysop_skipped++;
            if (s->verbose > 1)
                printf("  [SKIP-SYSOP] %-12s (perm: 0x%08x)\n", u->userid, u->userlevel);
            return 0;
        }
    }

    uint8_t roles = s->user_roles[unum];

    uint32_t expected_perms = 0;
    if (roles & ROLE_HAS_BM)
        expected_perms |= PERM_BM;
    if (roles & ROLE_HAS_GROUP_OP)
        expected_perms |= PERM_GROUP_OP;
    if (roles & ROLE_HAS_GROUP_LEAD)
        expected_perms |= PERM_GROUP_LEAD;

    uint32_t current_perms = u->userlevel & (PERM_BM | PERM_GROUP_OP | PERM_GROUP_LEAD);

    uint32_t add_bits = expected_perms & ~current_perms;
    uint32_t clear_bits = current_perms & ~expected_perms;

    if (s->no_role_only) {
        if (roles != 0)
            return 0;
        add_bits = 0;
    }

    if (!add_bits && !clear_bits)
        return 0;

    s->users_modified++;
    if (add_bits)
        s->users_added_perm++;
    if (clear_bits)
        s->users_cleared_perm++;

    if (add_bits & PERM_BM)
        s->bm_added++;
    if (add_bits & PERM_GROUP_OP)
        s->group_op_added++;
    if (add_bits & PERM_GROUP_LEAD)
        s->group_lead_added++;

    if (clear_bits & PERM_BM)
        s->bm_cleared++;
    if (clear_bits & PERM_GROUP_OP)
        s->group_op_cleared++;
    if (clear_bits & PERM_GROUP_LEAD)
        s->group_lead_cleared++;

    if (!s->quiet) {
        char add_str[64] = "";
        if (add_bits & PERM_BM) strlcat(add_str, "+PERM_BM ", sizeof(add_str));
        if (add_bits & PERM_GROUP_OP) strlcat(add_str, "+PERM_GROUP_OP ", sizeof(add_str));
        if (add_bits & PERM_GROUP_LEAD) strlcat(add_str, "+PERM_GROUP_LEAD ", sizeof(add_str));

        char clear_str[64] = "";
        if (clear_bits & PERM_BM) strlcat(clear_str, "-PERM_BM ", sizeof(clear_str));
        if (clear_bits & PERM_GROUP_OP) strlcat(clear_str, "-PERM_GROUP_OP ", sizeof(clear_str));
        if (clear_bits & PERM_GROUP_LEAD) strlcat(clear_str, "-PERM_GROUP_LEAD ", sizeof(clear_str));

        printf("[%s-USER] %-12s (uid: %-5d) %s%s\n",
               s->write_mode ? "APPLY" : "DRYRUN",
               u->userid, unum,
               add_str, clear_str);
    }

    if (s->write_mode) {
        passwd_lock();
        userec_t cur;
        if (passwd_query(unum, &cur) == 0) {
            cur.userlevel = (cur.userlevel | add_bits) & ~clear_bits;
            passwd_update(unum, &cur);
        }
        passwd_unlock();

        char log_str[128] = "";
        if (add_bits & PERM_BM) strlcat(log_str, "+PERM_BM ", sizeof(log_str));
        if (add_bits & PERM_GROUP_OP) strlcat(log_str, "+PERM_GROUP_OP ", sizeof(log_str));
        if (add_bits & PERM_GROUP_LEAD) strlcat(log_str, "+PERM_GROUP_LEAD ", sizeof(log_str));
        if (clear_bits & PERM_BM) strlcat(log_str, "-PERM_BM ", sizeof(log_str));
        if (clear_bits & PERM_GROUP_OP) strlcat(log_str, "-PERM_GROUP_OP ", sizeof(log_str));
        if (clear_bits & PERM_GROUP_LEAD) strlcat(log_str, "-PERM_GROUP_LEAD ", sizeof(log_str));

        log_user_security(u->userid, "[CheckModerators] Updated permissions: %s\n", log_str);
        log_filef("log/check_moderators.log",
                  "[%s] unum=%d: add=%#x clear=%#x, new level=%#x\n",
                  u->userid, unum, add_bits, clear_bits,
                  (u->userlevel | add_bits) & ~clear_bits);
    }

    return 0;
}

int main(int argc, char **argv)
{
    int write_mode = 0;
    int verbose = 0;
    int quiet = 0;
    int skip_sysop = 1;
    int no_role_only = 0;
    int prune_only = 0;
    int perm_only = 0;
    char opt_bbshome[PATH_MAX] = "";

    enum {
        OPT_PRUNE_ONLY = 1000,
        OPT_PERM_ONLY  = 1001,
    };

    static struct option long_options[] = {
        {"write",         no_argument,       0, 'w'},
        {"force",         no_argument,       0, 'f'},
        {"dry-run",       no_argument,       0, 'n'},
        {"verbose",       no_argument,       0, 'v'},
        {"quiet",         no_argument,       0, 'q'},
        {"include-sysop", no_argument,       0, 's'},
        {"no-role-only",  no_argument,       0, '0'},
        {"prune-only",    no_argument,       0, OPT_PRUNE_ONLY},
        {"perm-only",     no_argument,       0, OPT_PERM_ONLY},
        {"bbshome",       required_argument, 0, 'b'},
        {"help",          no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "wfnvqs0b:h", long_options, NULL)) != -1) {
        switch (c) {
        case 'w':
        case 'f':
            write_mode = 1;
            break;
        case 'n':
            write_mode = 0;
            break;
        case 'v':
            verbose++;
            break;
        case 'q':
            quiet = 1;
            break;
        case 's':
            skip_sysop = 0;
            break;
        case '0':
            no_role_only = 1;
            break;
        case OPT_PRUNE_ONLY:
            prune_only = 1;
            break;
        case OPT_PERM_ONLY:
            perm_only = 1;
            break;
        case 'b':
            strlcpy(opt_bbshome, optarg, sizeof(opt_bbshome));
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    now = time(NULL);

    if (geteuid() == 0) {
        setgid(BBSGID);
        setuid(BBSUID);
    }

    const char *bbshome = opt_bbshome[0] ? opt_bbshome : BBSHOME;
    if (chdir(bbshome) != 0) {
        fprintf(stderr, "Error: cannot chdir to %s: %s\n", bbshome, strerror(errno));
        return 1;
    }

    if (!attach_check_SHM()) {
        fprintf(stderr, "Error: Unable to attach to BBS shared memory (SHM).\n");
        fprintf(stderr, "Please verify that BBS daemon is running.\n");
        return 1;
    }

    resolve_boards();
    if (passwd_init() != 0) {
        fprintf(stderr, "Error: Failed to initialize passwd subsystem.\n");
        return 1;
    }

    if (access(FN_PASSWD, R_OK) != 0) {
        fprintf(stderr, "Error: Cannot open %s: %s\n", FN_PASSWD, strerror(errno));
        return 1;
    }

    uint8_t *user_roles = (uint8_t *)calloc(MAX_USERS + 1, sizeof(uint8_t));
    if (!user_roles) {
        fprintf(stderr, "Error: Failed to allocate memory for user roles.\n");
        return 1;
    }

    board_stats_t bstats;
    memset(&bstats, 0, sizeof(bstats));

    if (!quiet) {
        printf("check_moderators (%s mode)\n", write_mode ? "WRITE" : "DRY-RUN");
        printf("Scanning boards...\n");
    }

    int board_write = write_mode && !perm_only;
    scan_and_clean_boards(user_roles, &bstats, board_write, verbose, quiet);

    if (!quiet) {
        printf("Boards scanned: %d (regular: %d, sub-group: %d, top-group: %d)\n",
               bstats.boards_scanned, bstats.regular_boards,
               bstats.sub_group_boards, bstats.top_group_boards);
        if (bstats.invalid_mods_removed > 0 || bstats.firstlogin_backfilled > 0) {
            printf("Board modifications: %d boards (invalid mods removed: %d, backfilled firstlogin: %d)\n",
                   bstats.boards_cleaned, bstats.invalid_mods_removed, bstats.firstlogin_backfilled);
        }
        printf("Active managers: %d unique users (BM: %d, GroupOP: %d, GroupLead: %d)\n\n",
               bstats.unique_managers, bstats.distinct_bms,
               bstats.distinct_group_ops, bstats.distinct_group_leads);
    }

    scan_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.user_roles = user_roles;
    ctx.write_mode = write_mode && !prune_only;
    ctx.verbose = verbose;
    ctx.quiet = quiet;
    ctx.skip_sysop = skip_sysop;
    ctx.no_role_only = no_role_only;

    if (!prune_only) {
        if (!quiet) {
            printf("Scanning user accounts in %s...\n", FN_PASSWD);
        }

        if (passwd_apply(&ctx, check_user) != 0) {
            fprintf(stderr, "Error: Failed while scanning %s\n", FN_PASSWD);
            free(user_roles);
            return 1;
        }
    }

    if (!quiet) {
        printf("\n----------------------------------------------------------------------\n");
        printf("Check Moderators Summary (%s - %s):\n",
               write_mode ? "WRITE MODE" : "DRY RUN",
               write_mode ? "changes applied" : "no changes applied");
        printf("  Boards scanned:            %d (regular: %d, sub-group: %d, top-group: %d)\n",
               bstats.boards_scanned, bstats.regular_boards,
               bstats.sub_group_boards, bstats.top_group_boards);
        printf("  Board modifications:\n");
        printf("    - Boards cleaned:        %d\n", bstats.boards_cleaned);
        printf("    - Invalid mods removed:  %d\n", bstats.invalid_mods_removed);
        printf("    - Firstlogin backfilled: %d\n", bstats.firstlogin_backfilled);
        printf("  Active managers:           %d unique users (BM: %d, GroupOP: %d, GroupLead: %d)\n",
               bstats.unique_managers, bstats.distinct_bms,
               bstats.distinct_group_ops, bstats.distinct_group_leads);

        if (!prune_only) {
            printf("  User accounts checked:     %d\n", ctx.total_accounts);
            printf("  Accounts %s:           %d\n",
                   ctx.write_mode ? "modified" : "to modify", ctx.users_modified);
            printf("    - Permissions to add:    %d accounts\n", ctx.users_added_perm);
            printf("      * PERM_BM:             +%d\n", ctx.bm_added);
            printf("      * PERM_GROUP_OP:       +%d\n", ctx.group_op_added);
            printf("      * PERM_GROUP_LEAD:     +%d\n", ctx.group_lead_added);
            printf("    - Permissions to remove: %d accounts\n", ctx.users_cleared_perm);
            printf("      * PERM_BM:             -%d\n", ctx.bm_cleared);
            printf("      * PERM_GROUP_OP:       -%d\n", ctx.group_op_cleared);
            printf("      * PERM_GROUP_LEAD:     -%d\n", ctx.group_lead_cleared);
            if (ctx.sysop_skipped)
                printf("  SYSOP accounts skipped:    %d (use -s / --include-sysop to check)\n",
                       ctx.sysop_skipped);
        }

        if (!write_mode && (bstats.boards_cleaned > 0 || ctx.users_modified > 0)) {
            printf("\nNotice: Run with -w or --write to apply changes.\n");
        } else if (write_mode && (bstats.boards_cleaned > 0 || ctx.users_modified > 0)) {
            printf("\nChanges saved and logged to log/check_moderators.log.\n");
        }
    }

    free(user_roles);
    return 0;
}
