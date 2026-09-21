#ifndef INCLUDE_BOT_H
#define INCLUDE_BOT_H

#include <stddef.h>
#include <stdint.h>
#include "pttstruct.h"

int bot_on_login_attempt(const char *userid, const char *hostip,
                         int is_free_userid, int *out_delay_sec,
                         char *msg_buf, size_t msg_size);
int bot_on_auth_result(const char *userid, const char *hostip,
                       int is_free_userid, int *out_delay_sec,
                       char *msg_buf, size_t msg_size);

int bot_user_set_penalty(const char *userid, const char *hostip,
                         int penalty_sec, const char *reason);
uint32_t bot_user_track_read(const char *userid, time4_t firstlogin,
                             uint32_t delta);

#endif // INCLUDE_BOT_H
