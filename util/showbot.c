#define _UTIL_C_
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "bbs.h"
#include "bot.h"

static void
usage(const char *progname)
{
    fprintf(stderr,
            "Usage: %s [command] [options]\n\n"
            "Commands:\n"
            "  dump [options]          Dump bot SHM table status (default)\n"
            "  reset <userid> [all]    Reset tracking for a specific user (reads only, or all)\n"
            "  reset all [all]         Reset tracking for all users in SHM\n"
            "  clear [users|ips|all]   Clear records from SHM (default: users; or 'all' for users+ips)\n"
            "  clearall                Clear all records in SHM (both users and IPs)\n\n"
            "Options (for dump):\n"
            "  -r, --reads <min>       Filter: show only users with read_count >= min (default: 0)\n"
            "  -b, --banned            Filter: show only currently banned users/IPs\n"
            "  -h, --help              Show this help message\n",
            progname);
}

int
main(int argc, char *argv[])
{
    chdir(BBSHOME);

    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 ||
            strcmp(argv[1], "help") == 0) {
            usage(argv[0]);
            return 0;
        }

        if (strcmp(argv[1], "clear") == 0) {
            int clear_ips = 0;
            const char *target = (argc > 2) ? argv[2] : "users";

            if (strcasecmp(target, "all") == 0) {
                clear_ips = 1;
            } else if (strcasecmp(target, "ips") == 0 || strcasecmp(target, "ip") == 0) {
                clear_ips = 2;
            } else if (strcasecmp(target, "users") != 0 && strcasecmp(target, "user") != 0) {
                fprintf(stderr, "Unknown target for clear: %s (expected 'users', 'ips', or 'all')\n", target);
                return 1;
            }

            int n = bot_clear_all_shm(clear_ips);
            if (n < 0) {
                fprintf(stderr, "Failed to clear bot SHM (SHM not found or inaccessible).\n");
                return 1;
            }
            if (clear_ips == 1) {
                printf("Cleared entire bot SHM records for all users (%d user entries) and all IPs.\n", n);
            } else if (clear_ips == 2) {
                printf("Cleared bot SHM IP ban records.\n");
            } else {
                printf("Cleared entire bot SHM records for all users (%d user entries cleared).\n", n);
            }
            return 0;
        }

        if (strcmp(argv[1], "clearall") == 0 || strcmp(argv[1], "clear-all") == 0 ||
            strcmp(argv[1], "flush") == 0) {
            int n = bot_clear_all_shm(1);
            if (n < 0) {
                fprintf(stderr, "Failed to clear bot SHM (SHM not found or inaccessible).\n");
                return 1;
            }
            printf("Cleared entire bot SHM records for all users (%d user entries) and all IPs.\n", n);
            return 0;
        }

        if (strcmp(argv[1], "reset") == 0) {
            if (argc < 3) {
                fprintf(stderr, "Usage: %s reset <userid|all> [all]\n", argv[0]);
                return 1;
            }
            const char *userid = argv[2];
            int clear_all = (argc > 3 && strcasecmp(argv[3], "all") == 0) ? 1 : 0;

            if (strcasecmp(userid, "all") == 0 || strcmp(userid, "-a") == 0 ||
                strcmp(userid, "--all") == 0 || strcmp(userid, "*") == 0) {
                int n = bot_user_reset_all(clear_all);
                if (n < 0) {
                    fprintf(stderr, "Failed to reset bot SHM (SHM not found or inaccessible).\n");
                    return 1;
                }
                printf("Reset all users in bot SHM (%d users %s).\n",
                       n, clear_all ? "fully cleared" : "read counts/bans reset");
                return 0;
            }

            int ret = bot_user_reset(userid, clear_all);
            if (ret < 0) {
                fprintf(stderr, "Failed to access bot SHM.\n");
                return 1;
            } else if (ret == 0) {
                printf("User '%s' not found in bot SHM.\n", userid);
                return 1;
            } else {
                printf("Reset user '%s' (%s) successfully.\n",
                       userid, clear_all ? "all records" : "reads only");
                return 0;
            }
        }
    }

    int opt_start = 1;
    if (argc > 1 && (strcmp(argv[1], "dump") == 0 || strcmp(argv[1], "show") == 0 ||
                     strcmp(argv[1], "status") == 0)) {
        opt_start = 2;
    }

    uint32_t min_reads = 0;
    int banned_only = 0;

    for (int i = opt_start; i < argc; i++) {
        if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--banned") == 0) {
            banned_only = 1;
        } else if ((strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--reads") == 0) && i + 1 < argc) {
            min_reads = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (argv[i][0] == '-') {
            usage(argv[0]);
            return 1;
        }
    }

    int rc = bot_dump_shm(stdout, min_reads, banned_only);
    if (rc < 0) {
        return 1;
    }
    return 0;
}
