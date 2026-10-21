#include "cmbbs.h"
#include "common.h"
#include "var.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define STAT_CACHE_TTL_SECS 60

typedef enum {
    STAT_KEY_POST = 0,
    STAT_KEY_MAIL,
    STAT_KEY_SONG,
    STAT_KEY_MAX,
} stat_key_t;

static int         stat_cache_val[STAT_KEY_MAX] = { -1, -1, -1 };
static time4_t     stat_cache_time[STAT_KEY_MAX] = { 0, 0, 0 };
static const char *stat_key_names[STAT_KEY_MAX] = { "post", "mail", "song" };

static int
parse_json_field(const char *json, const char *key, int default_val)
{
    const char *p = strstr(json, key);
    if (!p)
        return default_val;
    p += strlen(key);
    while (*p == ' ' || *p == ':' || *p == '\t')
        p++;
    return atoi(p);
}

static int
send_statd_req(const char *json_payload)
{
    char resp[256];
    if (svc_request(GET_SVC_SOCKET(stat), json_payload, resp, sizeof(resp)) < 0)
        return -1;

    if (strstr(resp, "\"success\":true") == NULL)
        return -1;

    return parse_json_field(resp, "\"value\"", 0);
}

static int
stat_op(const char *action, stat_key_t key, int count, const char *userid)
{
    if (!userid || !*userid)
        return -1;
    if (key < 0 || key >= STAT_KEY_MAX)
        return -1;

    time4_t now = time4(NULL);
    int is_curr = (cuser.userid[0] && strcasecmp(userid, cuser.userid) == 0);

    if (is_curr && stat_cache_val[key] >= 0 && (now - stat_cache_time[key]) < STAT_CACHE_TTL_SECS &&
        strcmp(action, "get") == 0)
        return stat_cache_val[key];

    char payload[256];
    if (count > 0)
        SNPRINTF(payload, "{\"action\":\"%s\",\"key\":\"%s\",\"count\":%d,\"userid\":\"%s\"}\n",
                 action, stat_key_names[key], count, userid);
    else
        SNPRINTF(payload, "{\"action\":\"%s\",\"key\":\"%s\",\"userid\":\"%s\"}\n",
                 action, stat_key_names[key], userid);

    int cnt = send_statd_req(payload);
    if (is_curr && cnt >= 0) {
        stat_cache_val[key] = cnt;
        stat_cache_time[key] = now;
    }
    return cnt;
}

int
StatGetUserDailyPost(const char *userid)
{
    return stat_op("get", STAT_KEY_POST, 0, userid);
}

int
StatIncUserDailyPost(const char *userid)
{
    return stat_op("inc", STAT_KEY_POST, 0, userid);
}

int
StatGetUserDailyMail(const char *userid)
{
    return stat_op("get", STAT_KEY_MAIL, 0, userid);
}

int
StatAddUserDailyMail(const char *userid, int count)
{
    return stat_op("add", STAT_KEY_MAIL, count, userid);
}

int
StatGetUserDailySong(const char *userid)
{
    return stat_op("get", STAT_KEY_SONG, 0, userid);
}

int
StatIncUserDailySong(const char *userid)
{
    return stat_op("inc", STAT_KEY_SONG, 0, userid);
}

int
StatGetDailyPost(void)
{
    if (!cuser.userid[0])
        return 0;
    int cnt = StatGetUserDailyPost(cuser.userid);
    return cnt >= 0 ? cnt : 0;
}

int
StatIncDailyPost(void)
{
    if (!cuser.userid[0])
        return 0;
    int cnt = StatIncUserDailyPost(cuser.userid);
    return cnt >= 0 ? cnt : 0;
}

int
StatGetDailyMail(void)
{
    if (!cuser.userid[0])
        return 0;
    int cnt = StatGetUserDailyMail(cuser.userid);
    return cnt >= 0 ? cnt : 0;
}

int
StatAddDailyMail(int count)
{
    if (count <= 0 || !cuser.userid[0])
        return 0;
    int cnt = StatAddUserDailyMail(cuser.userid, count);
    return cnt >= 0 ? cnt : 0;
}

int
StatGetDailySong(void)
{
    if (!cuser.userid[0])
        return 0;
    int cnt = StatGetUserDailySong(cuser.userid);
    return cnt >= 0 ? cnt : 0;
}

int
StatIncDailySong(void)
{
    if (!cuser.userid[0])
        return 0;
    int cnt = StatIncUserDailySong(cuser.userid);
    return cnt >= 0 ? cnt : 0;
}
