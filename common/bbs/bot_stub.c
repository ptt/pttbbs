#include "bbs.h"
#include "bot.h"

int
bot_on_login_attempt(const char *userid GCC_UNUSED, const char *hostip GCC_UNUSED,
                     int is_free_userid GCC_UNUSED, int *out_delay_sec GCC_UNUSED,
                     char *msg_buf GCC_UNUSED, size_t msg_size GCC_UNUSED)
{
    return 0;
}

int
bot_on_auth_result(const char *userid GCC_UNUSED, const char *hostip GCC_UNUSED,
                   int is_free_userid GCC_UNUSED, int is_exempt GCC_UNUSED,
                   int *out_delay_sec GCC_UNUSED,
                   char *msg_buf GCC_UNUSED, size_t msg_size GCC_UNUSED)
{
    return 0;
}

int
bot_user_set_penalty(const char *userid GCC_UNUSED, const char *hostip GCC_UNUSED,
                     int penalty_sec GCC_UNUSED, const char *reason GCC_UNUSED)
{
    return 0;
}

uint32_t
bot_user_track_read(const char *userid GCC_UNUSED, time4_t firstlogin GCC_UNUSED,
                    uint32_t delta)
{
    return delta;
}

int
bot_dump_shm(FILE *fp, uint32_t min_read_count GCC_UNUSED, int banned_only GCC_UNUSED)
{
    if (fp)
        fprintf(fp, "bot_dump_shm: stub implementation (no SHM active)\n");
    return 0;
}

int
bot_user_reset(const char *userid GCC_UNUSED, int clear_all GCC_UNUSED)
{
    return 0;
}

int
bot_user_reset_all(int clear_all GCC_UNUSED)
{
    return 0;
}

int
bot_clear_all_shm(int clear_ips GCC_UNUSED)
{
    return 0;
}
