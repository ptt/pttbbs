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
    int users_with_bm_perm;
    int users_cleaned;
    int users_cleaned_no_role;
    int users_cleaned_partial_role;
    int bm_cleared;
    int group_op_cleared;
    int group_lead_cleared;
} scan_ctx_t;

static void print_usage(const char *progname)
{
    printf("Usage: %s [options]\n\n", progname);
    printf("Scan all boards and remove obsolete BM / GroupOP / GroupLead permissions\n");
    printf("from users in .PASSWDS who no longer hold corresponding positions.\n\n");
    printf("By default, %s runs in DRY-RUN mode and does not modify .PASSWDS.\n", progname);
    printf("Use -w or --write to apply changes.\n\n");
    printf("Options:\n");
    printf("  -w, --write            Apply permission changes to .PASSWDS (default: dry-run)\n");
    printf("  -f, --force            Alias for -w / --write\n");
    printf("  -n, --dry-run          Explicitly run in dry-run mode (inspection only)\n");
    printf("  -v, --verbose          Verbose output (show boards, BMs, and details)\n");
    printf("  -q, --quiet            Quiet mode (suppress per-user output, print summary only)\n");
    printf("  -s, --include-sysop    Check and modify SYSOP / PERM_BOARDS accounts (default: skip)\n");
    printf("  -0, --no-role-only     Only clean users who hold NO board roles at all\n");
    printf("  -b, --bbshome <dir>    Set BBS home directory (default: BBSHOME)\n");
    printf("  -h, --help             Display this help message and exit\n");
}

static void scan_boards(uint8_t *user_roles, board_stats_t *stats, int verbose)
{
    int nboards = num_boards();
    if (nboards <= 0 || nboards > MAX_BOARD)
        nboards = MAX_BOARD;

    for (int bid = 1; bid <= nboards; bid++) {
        const boardheader_t *bh = getbcache(bid);
        if (!bh || !bh->brdname[0] || (bh->brdattr & BRD_SYMBOLIC) || !bh->moderators[0].unum)
            continue;

        stats->boards_scanned++;

        uint8_t role_bit = 0;
        if (bh->brdattr & BRD_GROUPBOARD) {
            if (bh->gid == 1) {
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

        for (int m = 0; m < MAX_BMs; m++) {
            int uid = bh->moderators[m].unum;
            if (uid <= 0 || uid > MAX_USERS)
                continue;
            userec_t u;
            if (passwd_query(uid, &u) != 0)
                continue;
            if (bh->moderators[m].firstlogin != 0 &&
                u.firstlogin != bh->moderators[m].firstlogin)
                continue;

            if (!user_roles[uid])
                stats->unique_managers++;
            user_roles[uid] |= role_bit;
            if (verbose > 1) {
                printf("  Board %-15s Role: %-10s User: %-12s (uid=%d)\n",
                       bh->brdname,
                       (role_bit == ROLE_HAS_GROUP_LEAD) ? "GROUP_LEAD" :
                       (role_bit == ROLE_HAS_GROUP_OP) ? "GROUP_OP" : "BM",
                       u.userid, uid);
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

    uint32_t bm_perm_mask = (PERM_BM | PERM_GROUP_OP | PERM_GROUP_LEAD);
    if (!(u->userlevel & bm_perm_mask))
        return 0;

    s->users_with_bm_perm++;

    if (s->skip_sysop) {
        if ((u->userlevel & (PERM_SYSOP | PERM_BOARDS))
#if IS_ENABLED(CONFIG_SYSOP_ACCOUNT)
            || (strcasecmp(u->userid, str_sysop) == 0)
#endif
        ) {
            s->sysop_skipped++;
            if (s->verbose)
                printf("  [SKIP-SYSOP] %-12s (perm: 0x%08x)\n", u->userid, u->userlevel);
            return 0;
        }
    }

    uint8_t roles = s->user_roles[unum];

    if (s->no_role_only && roles != 0)
        return 0;

    uint32_t clear_bits = 0;
    if ((u->userlevel & PERM_BM) && !(roles & ROLE_HAS_BM))
        clear_bits |= PERM_BM;
    if ((u->userlevel & PERM_GROUP_OP) && !(roles & ROLE_HAS_GROUP_OP))
        clear_bits |= PERM_GROUP_OP;
    if ((u->userlevel & PERM_GROUP_LEAD) && !(roles & ROLE_HAS_GROUP_LEAD))
        clear_bits |= PERM_GROUP_LEAD;

    if (!clear_bits)
        return 0;

    s->users_cleaned++;
    if (roles == 0)
        s->users_cleaned_no_role++;
    else
        s->users_cleaned_partial_role++;

    if (clear_bits & PERM_BM)
        s->bm_cleared++;
    if (clear_bits & PERM_GROUP_OP)
        s->group_op_cleared++;
    if (clear_bits & PERM_GROUP_LEAD)
        s->group_lead_cleared++;

    if (!s->quiet) {
        char held_str[64] = "";
        if (roles & ROLE_HAS_BM) strlcat(held_str, "BM ", sizeof(held_str));
        if (roles & ROLE_HAS_GROUP_OP) strlcat(held_str, "GroupOP ", sizeof(held_str));
        if (roles & ROLE_HAS_GROUP_LEAD) strlcat(held_str, "GroupLead ", sizeof(held_str));
        if (!held_str[0]) strlcpy(held_str, "none", sizeof(held_str));

        char clear_str[64] = "";
        if (clear_bits & PERM_BM) strlcat(clear_str, "PERM_BM ", sizeof(clear_str));
        if (clear_bits & PERM_GROUP_OP) strlcat(clear_str, "PERM_GROUP_OP ", sizeof(clear_str));
        if (clear_bits & PERM_GROUP_LEAD) strlcat(clear_str, "PERM_GROUP_LEAD ", sizeof(clear_str));

        printf("[%s] %-12s (uid: %-5d) held: [%s] %s: [%s]\n",
               s->write_mode ? "CLEAN" : "DRYRUN",
               u->userid, unum, held_str,
               s->write_mode ? "removed" : "to remove",
               clear_str);
    }

    if (s->write_mode) {
        passwd_lock();
        userec_t cur;
        if (passwd_query(unum, &cur) == 0) {
            cur.userlevel &= ~clear_bits;
            passwd_update(unum, &cur);
        }
        passwd_unlock();

        char log_str[64] = "";
        if (clear_bits & PERM_BM) strlcat(log_str, "PERM_BM ", sizeof(log_str));
        if (clear_bits & PERM_GROUP_OP) strlcat(log_str, "PERM_GROUP_OP ", sizeof(log_str));
        if (clear_bits & PERM_GROUP_LEAD) strlcat(log_str, "PERM_GROUP_LEAD ", sizeof(log_str));

        log_user_security(u->userid, "[CleanBMPerm] Removed unheld permissions: %s\n", log_str);
        log_filef("log/cleanbmperm.log",
                  "[%s] unum=%d: removed %#x, remaining level=%#x\n",
                  u->userid, unum, clear_bits, u->userlevel & ~clear_bits);
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
    char opt_bbshome[PATH_MAX] = "";

    static struct option long_options[] = {
        {"write",         no_argument,       0, 'w'},
        {"force",         no_argument,       0, 'f'},
        {"dry-run",       no_argument,       0, 'n'},
        {"verbose",       no_argument,       0, 'v'},
        {"quiet",         no_argument,       0, 'q'},
        {"include-sysop", no_argument,       0, 's'},
        {"no-role-only",  no_argument,       0, '0'},
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
        printf("cleanbmperm (%s mode)\n", write_mode ? "WRITE" : "DRY-RUN");
        printf("Scanning boards...\n");
    }

    scan_boards(user_roles, &bstats, verbose);

    if (!quiet) {
        printf("Boards scanned: %d (regular: %d, sub-group: %d, top-group: %d)\n",
               bstats.boards_scanned, bstats.regular_boards,
               bstats.sub_group_boards, bstats.top_group_boards);
        printf("Active managers: %d unique users (BM: %d, GroupOP: %d, GroupLead: %d)\n\n",
               bstats.unique_managers, bstats.distinct_bms,
               bstats.distinct_group_ops, bstats.distinct_group_leads);
        printf("Scanning user accounts in %s...\n", FN_PASSWD);
    }

    scan_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.user_roles = user_roles;
    ctx.write_mode = write_mode;
    ctx.verbose = verbose;
    ctx.quiet = quiet;
    ctx.skip_sysop = skip_sysop;
    ctx.no_role_only = no_role_only;

    if (passwd_apply(&ctx, check_user) != 0) {
        fprintf(stderr, "Error: Failed while scanning %s\n", FN_PASSWD);
        free(user_roles);
        return 1;
    }

    if (!quiet) {
        printf("\n----------------------------------------------------------------------\n");
        printf("Scan Summary (%s - %s):\n",
               write_mode ? "WRITE MODE" : "DRY RUN",
               write_mode ? "changes applied" : "no changes applied");
        printf("  Boards scanned:        %d (regular: %d, sub-group: %d, top-group: %d)\n",
               bstats.boards_scanned, bstats.regular_boards,
               bstats.sub_group_boards, bstats.top_group_boards);
        printf("  Active managers:       %d unique users (BM: %d, GroupOP: %d, GroupLead: %d)\n",
               bstats.unique_managers, bstats.distinct_bms,
               bstats.distinct_group_ops, bstats.distinct_group_leads);
        printf("  User accounts checked: %d (users with BM permissions: %d)\n",
               ctx.total_accounts, ctx.users_with_bm_perm);
        printf("  Accounts %s:       %d\n",
               write_mode ? "cleaned" : "to clean", ctx.users_cleaned);
        printf("    - Holding no board roles: %d\n", ctx.users_cleaned_no_role);
        printf("    - Holding partial roles:  %d\n", ctx.users_cleaned_partial_role);
        printf("  Permissions %s:\n",
               write_mode ? "cleared" : "to clear");
        printf("    - PERM_BM:           %d\n", ctx.bm_cleared);
        printf("    - PERM_GROUP_OP:     %d\n", ctx.group_op_cleared);
        printf("    - PERM_GROUP_LEAD:   %d\n", ctx.group_lead_cleared);
        if (ctx.sysop_skipped)
            printf("  SYSOP accounts skipped: %d (use -s / --include-sysop to check)\n",
                   ctx.sysop_skipped);

        if (!write_mode && ctx.users_cleaned > 0) {
            printf("\nNotice: Run with -w or --write to apply changes.\n");
        } else if (write_mode && ctx.users_cleaned > 0) {
            printf("\nChanges saved to %s and logged to log/cleanbmperm.log.\n", FN_PASSWD);
        }
    }

    free(user_roles);
    return 0;
}
