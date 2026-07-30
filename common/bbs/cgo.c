#include "cmbbs.h"
#include "var.h"
#include <stddef.h>
#include <string.h>

const char *
get_bbshome(void)
{
    return BBSHOME;
}

const char *
get_userid_by_uid(int uid)
{
    if (!SHM || uid <= 0 || uid > MAX_USERS) {
        return NULL;
    }
    return SHM->userid[uid - 1];
}

int
get_ushm_size(void)
{
    return USHM_SIZE;
}

int
get_online_session(int uip, int *out_pid, int *out_uid, char *out_userid)
{
    if (!SHM || !VALID_USHM_ENTRY(uip)) {
        return 0;
    }
    userinfo_t *u = &SHM->uinfo[uip];
    if (u->pid <= 0 || u->userid[0] == '\0') {
        return 0;
    }
    if (out_pid) {
        *out_pid = (int)u->pid;
    }
    if (out_uid) {
        *out_uid = u->uid;
    }
    if (out_userid) {
        strlcpy(out_userid, u->userid, IDLEN + 1);
    }
    return 1;
}
