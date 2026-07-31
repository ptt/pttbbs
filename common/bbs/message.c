#include <assert.h>
#include "cmbbs.h"
#include "bbs.h"

/* Like UNIX 'write' command, directly write on the target user's screen. */
int write_message(int uip, pid_t to_pid, pid_t from_pid, const char *from_id,
                  const char *text, int msgmode) {
    assert(SHM);
    if (!VALID_USHM_ENTRY(uip)) {
        return -1;
    }

    userinfo_t *uentp = &(SHM->uinfo[uip]);
    if (!uentp->pid) {
        return -2;
    }

    if (to_pid > 0 && uentp->pid != to_pid) {
        return -4;
    }

    int pos = (int)uentp->msgcount;
    if (pos >= MAX_MSGS) {
        return -3;
    }

    msgque_t *msg = &(uentp->msgs[pos]);
    if (msg->pid) {
        for (pos = 0; pos < MAX_MSGS; pos++) {
            msg = &(uentp->msgs[pos]);
            if (!msg->pid) {
                break;
            }
        }
    }

    msg->pid = from_pid > 0 ? from_pid : uentp->pid;
    msg->msgmode = msgmode;
    if (from_id) {
        STRLCPY(msg->userid, from_id);
    }
    if (text) {
        STRLCPY(msg->last_call_in,text);
    }

    uentp->msgcount = pos + 1;

    int r = kill(uentp->pid, SIGUSR2);
    if (r != 0) {
        return errno ? -errno : -10;
    }
    return 0;
}
