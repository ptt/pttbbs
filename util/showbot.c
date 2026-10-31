#define _UTIL_C_
#include <ctype.h>
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
            "  dump [options]               Dump bot SHM table status (default)\n"
            "  ban <userid> <sec> [reason]  Set temporary login ban on a user\n"
            "  reset <userid> [all]         Reset tracking for a specific user (counters only, or all)\n"
            "  reset all [all]              Reset tracking for all users in SHM\n"
            "  clear [users|ips|all]        Clear records from SHM (default: users; or 'all' for users+ips)\n"
            "  clearall                     Clear all records in SHM (both users and IPs)\n\n"
            "Options (for dump):\n"
            "  -r, --reads <min>            Filter: read_count >= min\n"
            "  -s, --switches <min>         Filter: board_switch_count >= min\n"
            "  -q, --searches <min>         Filter: search_count >= min\n"
            "  -B, --burst [min]            Filter: burst_count >= min (default 1)\n"
            "  -c, --client <hex>           Filter: exact client_code (e.g. 0x2000023)\n"
            "  -b, --banned                 Filter: show only currently banned users/IPs\n"
            "  --sort <mode>                Sort by: auto, bursts, switches, searches, reads\n"
            "  -h, --help                   Show this help message\n",
            progname);
}

static int
parse_sort_mode(const char *s)
{
    if (!s || strcasecmp(s, "auto") == 0)
        return BOT_SORT_AUTO;
    if (strcasecmp(s, "reads") == 0 || strcasecmp(s, "read") == 0)
        return BOT_SORT_READS;
    if (strcasecmp(s, "bursts") == 0 || strcasecmp(s, "burst") == 0)
        return BOT_SORT_BURSTS;
    if (strcasecmp(s, "switches") == 0 || strcasecmp(s, "switch") == 0)
        return BOT_SORT_SWITCHES;
    if (strcasecmp(s, "searches") == 0 || strcasecmp(s, "search") == 0)
        return BOT_SORT_SEARCHES;
    return -1;
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

        if (strcmp(argv[1], "ban") == 0) {
            if (argc < 4) {
                fprintf(stderr, "Usage: %s ban <userid> <seconds> [reason]\n", argv[0]);
                return 1;
            }
            const char *userid = argv[2];
            int sec = atoi(argv[3]);
            const char *reason = (argc > 4) ? argv[4] : "manual_showbot";
            if (sec <= 0) {
                fprintf(stderr, "Invalid ban seconds: %s\n", argv[3]);
                return 1;
            }
            if (bot_user_set_penalty(userid, NULL, sec, reason) < 0) {
                fprintf(stderr, "Failed to set penalty for '%s' (SHM unavailable).\n", userid);
                return 1;
            }
            printf("Banned user '%s' for %d seconds (reason: %s).\n", userid, sec, reason);
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
                       n, clear_all ? "fully cleared" : "counters/bans reset");
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
                       userid, clear_all ? "all records" : "counters only");
                return 0;
            }
        }
    }

    int opt_start = 1;
    if (argc > 1 && (strcmp(argv[1], "dump") == 0 || strcmp(argv[1], "show") == 0 ||
                     strcmp(argv[1], "status") == 0)) {
        opt_start = 2;
    }

    bot_dump_filter_t filter;
    memset(&filter, 0, sizeof(filter));

    for (int i = opt_start; i < argc; i++) {
        if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--banned") == 0) {
            filter.banned_only = 1;
        } else if ((strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--reads") == 0) && i + 1 < argc) {
            filter.min_read_count = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--switches") == 0) && i + 1 < argc) {
            filter.min_board_switches = (uint32_t)strtoul(argv[++i], NULL, 10);
            if (filter.sort_by == BOT_SORT_AUTO)
                filter.sort_by = BOT_SORT_SWITCHES;
        } else if ((strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--searches") == 0) && i + 1 < argc) {
            filter.min_searches = (uint32_t)strtoul(argv[++i], NULL, 10);
            if (filter.sort_by == BOT_SORT_AUTO)
                filter.sort_by = BOT_SORT_SEARCHES;
        } else if (strcmp(argv[i], "-B") == 0 || strcmp(argv[i], "--burst") == 0 ||
                   strcmp(argv[i], "--bursts") == 0) {
            if (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0])) {
                filter.min_bursts = (uint32_t)strtoul(argv[++i], NULL, 10);
            } else {
                filter.min_bursts = 1;
            }
            if (filter.sort_by == BOT_SORT_AUTO)
                filter.sort_by = BOT_SORT_BURSTS;
        } else if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--client") == 0) && i + 1 < argc) {
            filter.client_code = (uint32_t)strtoul(argv[++i], NULL, 16);
            filter.has_client_filter = 1;
        } else if (strcmp(argv[i], "--sort") == 0 && i + 1 < argc) {
            int sm = parse_sort_mode(argv[++i]);
            if (sm < 0) {
                fprintf(stderr, "Unknown sort mode: %s\n", argv[i]);
                return 1;
            }
            filter.sort_by = sm;
        } else if (argv[i][0] == '-') {
            usage(argv[0]);
            return 1;
        }
    }

    int rc = bot_dump_shm_ex(stdout, &filter);
    if (rc < 0) {
        return 1;
    }
    return 0;
}
