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

    pid_t target_pid = from_pid > 0 ? from_pid : uentp->pid;
    int pos = -1;
    msgque_t *msg = NULL;

    for (int i = 0; i < MAX_MSGS; i++) {
        msgque_t *cand = &(uentp->msgs[i]);
        if (__sync_bool_compare_and_swap(&cand->pid, 0, target_pid)) {
            pos = i;
            msg = cand;
            break;
        }
    }

    if (pos < 0 || pos >= MAX_MSGS) {
        return -3;
    }

    msg->msgmode = msgmode;
    if (from_id) {
        STRLCPY(msg->userid, from_id);
    }
    if (text) {
        STRLCPY(msg->last_call_in, text);
    }

    while (1) {
        int cur_count = uentp->msgcount;
        if (cur_count >= pos + 1)
            break;
        if (__sync_bool_compare_and_swap(&uentp->msgcount, cur_count, pos + 1))
            break;
    }

    int r = kill(uentp->pid, SIGUSR2);
    if (r != 0) {
        return errno ? -errno : -10;
    }
    return 0;
}
