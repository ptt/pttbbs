#include "cmbbs.h"
#include "common.h"
#include "var.h"
#include "modes.h"
#include "perm.h"
#include <assert.h>
#include <ctype.h>
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
get_online_session(int uslot, int *out_pid, int *out_uid, char *out_userid)
{
    if (!SHM || !VALID_USHM_ENTRY(uslot)) {
        return 0;
    }
    userinfo_t *u = &SHM->uinfo[uslot];
    pid_t pid = u->pid;
    if (pid <= 0 || u->userid[0] == '\0') {
        return 0;
    }
    if (kill(pid, 0) == -1 && errno == ESRCH) {
        if (u->pid == pid) {
            memset(u, 0, sizeof(userinfo_t));
            SHM->UTMPneedupdate = 1;
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
set_online_session_friends(int uslot, int expected_pid, int expected_uid,
                           const unsigned int *entries, int count)
{
    if (!SHM || !VALID_USHM_ENTRY(uslot)) {
        return 0;
    }
    userinfo_t *u = &SHM->uinfo[uslot];
    if (u->pid <= 0 || u->userid[0] == '\0') {
        return 0;
    }
    if (expected_pid > 0 && (int)u->pid != expected_pid) {
        return 0;
    }
    if (expected_uid > 0 && u->uid != expected_uid) {
        return 0;
    }
    if (count < 0) {
        count = 0;
    }
    if (count > MAX_FRIEND_ONLINE) {
        count = MAX_FRIEND_ONLINE;
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

    if (count < MAX_FRIEND_ONLINE) {
        memset(&u->friend_online[count], 0,
               sizeof(unsigned int) * (size_t)(MAX_FRIEND_ONLINE - count));
    }
    u->friend_svc_flag = 1;
    __sync_synchronize();
    u->friendtotal = count;
    return 1;
}

int
get_online_session_detail(int uslot, int *out_pid, int *out_uid, char *out_userid,
                          int *out_friend_svc, int *out_friendtotal,
                          unsigned int *out_friend_online, int max_online)
{
    if (!SHM || !VALID_USHM_ENTRY(uslot)) {
        return 0;
    }
    userinfo_t *u = &SHM->uinfo[uslot];
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
get_board_nuser(int bid)
{
    if (!SHM || bid <= 0 || bid > MAX_BOARD) {
        return 0;
    }
    return SHM->bcache[bid - 1].nuser;
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

void
purge_utmp_slot(int uslot)
{
    if (!SHM || !VALID_USHM_ENTRY(uslot))
        return;
    userinfo_t *uentp = &SHM->uinfo[uslot];
    logout_friend_online(uentp);
    int uid = uentp->uid;
    __atomic_store_n(&SHM->utmp_user.session_user[uslot], 0, __ATOMIC_RELEASE);
    int next_val;
    do {
        next_val = __atomic_load_n(&SHM->utmp_user.next_session[uslot], __ATOMIC_ACQUIRE);
        if (UTMP_IS_DELETED(next_val))
            break;
    } while (!__atomic_compare_exchange_n(&SHM->utmp_user.next_session[uslot],
                                          &next_val,
                                          UTMP_MARK_DELETED(next_val),
                                          false,
                                          __ATOMIC_RELEASE,
                                          __ATOMIC_ACQUIRE));
    if (uid > 0 && uid <= MAX_USERS) {
        int head = uslot;
        int next_slot = UTMP_DECODE_SLOT(next_val);
        if (next_slot == uslot || !VALID_USHM_ENTRY(next_slot))
            next_slot = -1;
        __atomic_compare_exchange_n(&SHM->utmp_user.user_head[uid], &head, next_slot,
                                    false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE);
    }
    memset(uentp, 0, sizeof(userinfo_t));
    SHM->UTMPneedupdate = 1;
}

void
utmp_update(void)
{
    if (!SHM)
        return;
    userinfo_t *uentp;
    int count = 0, i;
    int nusers[MAX_BOARD];

    SHM->UTMPbusystate = 1;
    SHM->UTMPuptime = time(NULL);

    memset(nusers, 0, sizeof(nusers));
    for (i = 0; i < USHM_SIZE; ++i) {
        uentp = &SHM->uinfo[i];
        if (uentp->pid) {
            count++;
            if (uentp->mode != DEBUGSLEEPING &&
                0 < uentp->brc_id && uentp->brc_id < MAX_BOARD)
                ++nusers[uentp->brc_id - 1];
        }
    }
    SHM->UTMPnumber = count;

    {
        int k, r, last = 0, top = 0;
        int hot_bids[MAX_HOTBOARDS];
        for (i = 0; i < MAX_HOTBOARDS; i++)
            hot_bids[i] = -1;

        for (i = 0; i < SHM->Bnumber; i++) {
            if (SHM->bcache[i].brdname[0] != 0) {
                SHM->bcache[i].nuser = nusers[i];
                if (nusers[i] > 1 &&
                    (top < MAX_HOTBOARDS || nusers[i] > last) &&
                    IS_BOARD(&SHM->bcache[i]) &&
                    !(SHM->bcache[i].brdattr & BRD_COOLDOWN) &&
                    IS_OPENBRD(&SHM->bcache[i])) {
                    for (k = top - 1; k >= 0; --k)
                        if (hot_bids[k] >= 0 &&
                            nusers[i] < SHM->bcache[hot_bids[k]].nuser)
                            break;
                    if (top < MAX_HOTBOARDS)
                        ++top;
                    for (r = top - 1; r > (k + 1); --r)
                        hot_bids[r] = hot_bids[r - 1];
                    hot_bids[k + 1] = i;
                    last = nusers[hot_bids[top - 1]];
                }
            }
        }
        memcpy(SHM->hotboards.bids, hot_bids, sizeof(hot_bids));
        SHM->hotboards.num = top;
#if HOTBOARDCACHE
        {
            int old_max = (int)HOTBOARDCACHE;
            int n_copy = top < old_max ? top : old_max;
            if (n_copy > 255)
                n_copy = 255;
            for (i = 0; i < old_max; i++) {
                SHM->HBcache[i] = (i < n_copy) ? hot_bids[i] : -1;
            }
            SHM->nHOTs = (unsigned char)n_copy;
        }
#endif
    }
    SHM->UTMPbusystate = 0;
}

void
get_utmp_status(long *out_uptime, int *out_number, int *out_busystate, int *out_needupdate)
{
    if (!SHM) {
        if (out_uptime) *out_uptime = 0;
        if (out_number) *out_number = 0;
        if (out_busystate) *out_busystate = 0;
        if (out_needupdate) *out_needupdate = 0;
        return;
    }
    if (out_uptime) *out_uptime = (long)SHM->UTMPuptime;
    if (out_number) *out_number = SHM->UTMPnumber;
    if (out_busystate) *out_busystate = (int)SHM->UTMPbusystate;
    if (out_needupdate) *out_needupdate = (int)SHM->UTMPneedupdate;
}

void
reset_utmp_busystate(void)
{
    if (SHM)
        SHM->UTMPbusystate = 0;
}

int
get_utmp_busystate(void)
{
    return SHM ? (int)SHM->UTMPbusystate : 0;
}

void
set_utmp_busystate(int val)
{
    if (SHM)
        SHM->UTMPbusystate = (char)val;
}

int
get_utmp_needupdate(void)
{
    return SHM ? (int)SHM->UTMPneedupdate : 0;
}

void
set_utmp_needupdate(int val)
{
    if (SHM)
        SHM->UTMPneedupdate = (char)val;
}

int
get_utmp_number(void)
{
    return SHM ? SHM->UTMPnumber : 0;
}

int
get_hotboards(int *out_bids, int max_boards)
{
    if (!SHM || !out_bids || max_boards <= 0)
        return 0;
    int n = SHM->hotboards.num;
    if (n > max_boards)
        n = max_boards;
    if (n > MAX_HOTBOARDS)
        n = MAX_HOTBOARDS;
    for (int i = 0; i < n; i++) {
        out_bids[i] = SHM->hotboards.bids[i] + 1;
    }
    return n;
}

int
fix_utmp_user_table(void)
{
    if (!SHM)
        return 0;
    int changeflag = 0;
    for (int i = 0; i < USHM_SIZE; ++i) {
        int next_val = SHM->utmp_user.next_session[i];
        int next_slot = UTMP_DECODE_SLOT(next_val);
        if (next_slot == i) {
            SHM->utmp_user.next_session[i] = 0;
            changeflag = 1;
        }
    }
    for (int i = 1; i <= MAX_USERS; ++i) {
        int h = SHM->utmp_user.user_head[i];
        if (VALID_USHM_ENTRY(h)) {
            if (SHM->utmp_user.session_user[h] != i ||
                SHM->uinfo[h].uid != i ||
                SHM->uinfo[h].pid <= 0) {
                SHM->utmp_user.user_head[i] = -1;
                changeflag = 1;
            }
        } else if (h != -1) {
            SHM->utmp_user.user_head[i] = -1;
            changeflag = 1;
        }
    }
    return changeflag;
}

void
rebuild_utmp_user(void)
{
    init_utmp_user();
}

int
get_utmp_candidates(cgo_utmp_candidate_t *out_candidates, int max_candidates, int *out_count)
{
    if (!SHM || !out_candidates || max_candidates <= 0) {
        if (out_count) *out_count = 0;
        return 0;
    }
    time_t now = time(NULL);
    int count = 0;
    for (int i = 0; i < USHM_SIZE && count < max_candidates; i++) {
        userinfo_t *u = &SHM->uinfo[i];
        if (u->pid <= 0)
            continue;
        cgo_utmp_candidate_t *c = &out_candidates[count++];
        c->slot = i;
        c->pid = (int)u->pid;
        c->uid = u->uid;
        strlcpy(c->userid, u->userid, sizeof(c->userid));
        c->lastact = (long)u->lastact;
        c->idle_sec = (int)time4_diff(now, u->lastact);
        c->friendtotal = u->friendtotal;
        c->mode = u->mode;
        c->brc_id = u->brc_id;
        c->is_guest = (strcasecmp(u->userid, STR_GUEST) == 0) ? 1 : 0;
        int valid_name = (isalpha((unsigned char)u->userid[0]) &&
                          memchr(u->userid, '\0', IDLEN + 1) != NULL);
        int valid_friends = (u->friendtotal >= 0 && u->friendtotal <= MAX_FRIEND_ONLINE);
        c->is_userid_valid = (valid_name && valid_friends) ? 1 : 0;
        c->user_exists = (searchuser(u->userid, NULL) != 0) ? 1 : 0;
    }
    if (out_count)
        *out_count = count;
    return 1;
}
