#include "cmbbs.h"
#include "common.h"
#include "var.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

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
    pid_t pid = u->pid;
    if (pid <= 0 || u->userid[0] == '\0') {
        return 0;
    }
    if (kill(pid, 0) == -1 && errno == ESRCH) {
        if (u->pid == pid) {
            memset(u, 0, sizeof(userinfo_t));
            SHM->UTMPneedsort = 1;
        }
        return 0;
    }
    if (out_pid) {
        *out_pid = (int)pid;
    }
    if (out_uid) {
        *out_uid = u->uid;
    }
    if (out_userid) {
        strlcpy(out_userid, u->userid, IDLEN + 1);
    }
    return 1;
}


int
get_uid_by_userid(const char *userid)
{
    if (!SHM || !userid || !*userid) {
        return 0;
    }
    return searchuser(userid, NULL);
}


int
set_online_session_friends(int uip, int expected_pid, int expected_uid,
                           const unsigned int *entries, int count)
{
    if (!SHM || !VALID_USHM_ENTRY(uip)) {
        return 0;
    }
    userinfo_t *u = &SHM->uinfo[uip];
    if (u->pid <= 0 || u->userid[0] == '\0') {
        return 0;
    }
    if (expected_pid > 0 && (int)u->pid != expected_pid) {
        return 0;
    }
    if (expected_uid > 0 && u->uid != expected_uid) {
        return 0;
    }
    /* Before the cutoff the session may belong to a legacy binary, whose
     * userinfo_t has reject[] right after friend_online[MAX_FRIEND] + gap_2.
     * Only touch friend_online[0..MAX_FRIEND] (the last one is old gap_2,
     * zeroed as a terminator). */
    int limit = (time(NULL) < FRIEND_LEGACY_COMPAT_CUTOFF) ? MAX_FRIEND : MAX_FRIEND_ONLINE;
    int clear_end = (limit < MAX_FRIEND_ONLINE) ? limit + 1 : MAX_FRIEND_ONLINE;
    if (count < 0) {
        count = 0;
    }
    if (count > limit) {
        count = limit;
    }
    if (count > 0 && entries) {
        for (int i = 0; i < count; i++) {
            int slot = FRIEND_ONLINE_SLOT(entries[i]);
            int stat = FRIEND_ONLINE_STAT(entries[i]);
            unsigned int tag = FRIEND_ONLINE_EXTRACT_TAG(entries[i]);
            /* Called from friend.svc: reject bad input instead of aborting
             * the whole daemon (nothing has been written yet). */
            if (!VALID_USHM_ENTRY(slot) ||
                (stat & ~(int)(ST_FRIEND | ST_SUPER | ST_REJECT)) != 0 ||
                tag > FRIEND_ONLINE_UID_MASK)
                return 0;
        }
        memcpy(u->friend_online, entries, sizeof(unsigned int) * (size_t)count);
    }

    if (count < clear_end) {
        memset(&u->friend_online[count], 0,
               sizeof(unsigned int) * (size_t)(clear_end - count));
    }
    u->friend_svc_flag = 1;
    __sync_synchronize();
    u->friendtotal = count;
    return 1;
}

int
get_online_session_detail(int uip, int *out_pid, int *out_uid, char *out_userid,
                          int *out_friend_svc, int *out_friendtotal,
                          unsigned int *out_friend_online, int max_online)
{
    if (!SHM || !VALID_USHM_ENTRY(uip)) {
        return 0;
    }
    userinfo_t *u = &SHM->uinfo[uip];
    if (u->pid <= 0 || u->userid[0] == '\0') {
        return 0;
    }
    if (out_pid)
        *out_pid = (int)u->pid;
    if (out_uid)
        *out_uid = u->uid;
    if (out_userid)
        strlcpy(out_userid, u->userid, IDLEN + 1);
    if (out_friend_svc)
        *out_friend_svc = (int)u->friend_svc_flag;

    int ft = u->friendtotal;
    if (ft < 0)
        ft = 0;
    if (ft > MAX_FRIEND_ONLINE)
        ft = MAX_FRIEND_ONLINE;
    if (out_friendtotal)
        *out_friendtotal = ft;
    if (out_friend_online && max_online > 0) {
        int n = ft < max_online ? ft : max_online;
        memcpy(out_friend_online, u->friend_online, sizeof(unsigned int) * (size_t)n);
    }
    return 1;
}

int
get_max_board(void)
{
    return MAX_BOARD;
}

int
get_num_boards(void)
{
    if (!SHM) {
        return 0;
    }
    return num_boards();
}

int
get_board_info(int bid, char *out_brdname, unsigned int *out_brdattr)
{
    if (!SHM || bid <= 0 || bid > MAX_BOARD) {
        return 0;
    }
    boardheader_t *bh = &SHM->bcache[bid - 1];
    if (bh->brdname[0] == '\0') {
        return 0;
    }
    if (out_brdname) {
        strlcpy(out_brdname, bh->brdname, IDLEN + 1);
    }
    if (out_brdattr) {
        *out_brdattr = bh->brdattr;
    }
    return 1;
}

int
get_board_bid(const char *brdname)
{
    if (!SHM || !brdname || !*brdname) {
        return 0;
    }
    return getbnum(brdname);
}

int
get_hbfl_generation(void)
{
    if (!SHM) {
        return 0;
    }
    return __sync_fetch_and_add(&SHM->GV3.e.hbfl_generation, 0);
}

int
bump_hbfl_generation(void)
{
    if (!SHM) {
        return 0;
    }
    return __sync_add_and_fetch(&SHM->GV3.e.hbfl_generation, 1);
}

void
set_hbfl_generation(int gen)
{
    if (!SHM) {
        return;
    }
    int cur = __sync_fetch_and_add(&SHM->GV3.e.hbfl_generation, 0);
    while (gen > cur) {
        if (__sync_bool_compare_and_swap(&SHM->GV3.e.hbfl_generation, cur, gen)) {
            break;
        }
        cur = __sync_fetch_and_add(&SHM->GV3.e.hbfl_generation, 0);
    }
}
