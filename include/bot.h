#ifndef INCLUDE_BOT_H
#define INCLUDE_BOT_H

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include "pttstruct.h"

enum {
    BOT_NAV_BOARD_SWITCH = 1,
    BOT_NAV_SEARCH_AID   = 2,
    BOT_NAV_SEARCH_SR    = 3,
};

enum {
    BOT_SORT_AUTO     = 0,
    BOT_SORT_READS    = 1,
    BOT_SORT_BURSTS   = 2,
    BOT_SORT_SWITCHES = 3,
    BOT_SORT_SEARCHES = 4,
};

typedef struct bot_dump_filter {
    uint32_t min_read_count;
    uint32_t min_board_switches;
    uint32_t min_searches;
    uint32_t min_bursts;
    uint32_t client_code;
    int      has_client_filter;
    int      banned_only;
    int      sort_by;
} bot_dump_filter_t;

int bot_on_login_attempt(const char *userid, const char *hostip,
                         int is_free_userid, int *out_delay_sec,
                         char *msg_buf, size_t msg_size);
int bot_on_auth_result(const char *userid, const char *hostip,
                       int is_free_userid, int is_exempt,
                       int *out_delay_sec,
                       char *msg_buf, size_t msg_size);

int bot_user_set_penalty(const char *userid, const char *hostip,
                         int penalty_sec, const char *reason);
uint32_t bot_user_track_read(const char *userid, time4_t firstlogin,
                             uint32_t delta);
int bot_on_mbbsd_nav(const char *userid, time4_t firstlogin,
                     const char *hostip, uint32_t client_code,
                     int is_exempt, int nav_type, int target_bid);
int bot_dump_shm(FILE *fp, uint32_t min_read_count, int banned_only);
int bot_dump_shm_ex(FILE *fp, const bot_dump_filter_t *filter);
int bot_user_reset(const char *userid, int clear_all);
int bot_user_reset_all(int clear_all);
int bot_clear_all_shm(int clear_ips);

#endif // INCLUDE_BOT_H
