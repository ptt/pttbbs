#ifndef INCLUDE_BOT_H
#define INCLUDE_BOT_H

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include "pttstruct.h"

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
int bot_dump_shm(FILE *fp, uint32_t min_read_count, int banned_only);
int bot_user_reset(const char *userid, int clear_all);
int bot_user_reset_all(int clear_all);
int bot_clear_all_shm(int clear_ips);

#endif // INCLUDE_BOT_H
