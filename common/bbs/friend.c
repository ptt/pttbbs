#include "cmbbs.h"
#include "common.h"
#include "var.h"
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int send_friendd_req(const char *json_payload) {
    char sock_path[PATHLEN];
    SNPRINTF(sock_path, "%s/run/friend.svc.sock", BBSHOME);

    int sfd = toconnect3(sock_path, 0, 10000); // non-blocking connect with 10ms timeout
    if (sfd < 0) {
        return -1;
    }

    int len = (int)strlen(json_payload);
    if (towrite(sfd, json_payload, len) != len) {
        close(sfd);
        return -1;
    }

    struct pollfd pfd;
    pfd.fd = sfd;
    pfd.events = POLLIN;
    int ret = -1;
    int pr;
    /* mbbsd receives signals (e.g. water balls); don't treat EINTR as a failure. */
    do {
        pr = poll(&pfd, 1, 50);
    } while (pr < 0 && errno == EINTR);
    if (pr > 0 && (pfd.revents & POLLIN)) {
        char resp[256];
        ssize_t r = read(sfd, resp, sizeof(resp) - 1);
        if (r > 0) {
            resp[r] = '\0';
            if (strstr(resp, "\"success\":true") != NULL) {
                ret = 0;
            }
        }
    }
    close(sfd);
    return ret;
}

/* For state-changing, idempotent requests: with no local fallback, a request
 * dropped by friend.svc ("service overloaded") must not be silently lost. */
static int send_friendd_req_retry(const char *json_payload) {
    int ret = -1;
    for (int tries = 0; tries < 3 && ret != 0; tries++) {
        if (tries)
            usleep(20000);
        ret = send_friendd_req(json_payload);
    }
    return ret;
}

int friend_svc_login(const char *userid, pid_t pid, int sid) {
    if (!userid || !*userid) {
        return -1;
    }
    char payload[256];
    SNPRINTF(payload, "{\"action\":\"login\",\"userid\":\"%s\",\"pid\":%d,\"sid\":%d}\n", userid, (int)pid, sid);
    return send_friendd_req(payload);
}

int friend_svc_logout(const char *userid, pid_t pid) {
    if (!userid || !*userid) {
        return -1;
    }
    char payload[256];
    SNPRINTF(payload, "{\"action\":\"logout\",\"userid\":\"%s\",\"pid\":%d}\n", userid, (int)pid);
    return send_friendd_req_retry(payload);
}

int friend_svc_reload(const char *userid) {
    if (!userid || !*userid) {
        return -1;
    }
    char payload[256];
    SNPRINTF(payload, "{\"action\":\"reload\",\"userid\":\"%s\"}\n", userid);
    return send_friendd_req(payload);
}

int friend_svc_sync(const char *userid, int uid, pid_t pid, int sid) {
    if (!userid || !*userid || uid <= 0 || pid <= 0 || sid < 0) {
        return -1;
    }
    char payload[256];
    SNPRINTF(payload, "{\"action\":\"friend_sync\",\"userid\":\"%s\",\"uid\":%d,\"pid\":%d,\"sid\":%d}\n",
             userid, uid, (int)pid, sid);
    return send_friendd_req_retry(payload);
}

static int     hbfl_cached_uid = 0;
static int     hbfl_cached_gen = -1;
static time4_t hbfl_cached_time = 0;
static time4_t hbfl_fail_time = 0;
static int     hbfl_cached_count = 0;
static int     hbfl_cached_cap = 0;
static int    *hbfl_cached_bids = NULL;

int friend_svc_hbfl_reload(int bid) {
    hbfl_cached_gen = -1;
    hbfl_fail_time = 0;
    bump_hbfl_generation();

    const char *brdname = "";
    if (SHM && bid >= 1 && bid <= MAX_BOARD && SHM->bcache[bid - 1].brdname[0]) {
        brdname = SHM->bcache[bid - 1].brdname;
    }

    char payload[256];
    SNPRINTF(payload, "{\"action\":\"hbfl_reload\",\"bid\":%d,\"brdname\":\"%s\"}\n", bid, brdname);
    /* A dropped reload (e.g. "service overloaded") would leave friend.svc
     * serving a stale list with no later mtime poll; reload is idempotent. */
    return send_friendd_req_retry(payload);
}

static int friend_svc_hbfl_query_user(int uid) {
    char sock_path[PATHLEN];
    SNPRINTF(sock_path, "%s/run/friend.svc.sock", BBSHOME);

    int sfd = toconnect3(sock_path, 0, 10000);
    if (sfd < 0) {
        return -1;
    }

    char payload[128];
    SNPRINTF(payload, "{\"action\":\"hbfl_user\",\"uid\":%d}\n", uid);
    int len = (int)strlen(payload);
    if (towrite(sfd, payload, len) != len) {
        close(sfd);
        return -1;
    }

    char resp[16384];
    size_t total = 0;
    struct pollfd pfd;
    pfd.fd = sfd;
    pfd.events = POLLIN;

    while (total < sizeof(resp) - 1) {
        if (poll(&pfd, 1, 50) <= 0 || !(pfd.revents & (POLLIN | POLLHUP))) {
            break;
        }
        ssize_t r = read(sfd, resp + total, sizeof(resp) - 1 - total);
        if (r <= 0) {
            break;
        }
        total += (size_t)r;
        resp[total] = '\0';
        if (strchr(resp, '\n') != NULL) {
            break;
        }
    }
    close(sfd);

    if (total == 0 || strchr(resp, '\n') == NULL) {
        return -1;
    }
    resp[total] = '\0';
    if (strstr(resp, "\"success\":true") == NULL) {
        return -1;
    }

    /* The cache is rewritten below; invalidate first so a parse failure
     * can't leave another uid's cache entry "valid" but emptied. */
    hbfl_cached_uid = 0;
    hbfl_cached_gen = -1;
    hbfl_cached_count = 0;
    const char *bids_ptr = strstr(resp, "\"bids\":[");
    if (bids_ptr != NULL) {
        bids_ptr += 8;
        if (strchr(bids_ptr, ']') == NULL) {
            return -1;
        }
        while (*bids_ptr && *bids_ptr != ']') {
            while (*bids_ptr && (*bids_ptr < '0' || *bids_ptr > '9') && *bids_ptr != ']') {
                bids_ptr++;
            }
            if (*bids_ptr == ']' || !*bids_ptr) {
                break;
            }
            char *endptr = NULL;
            long b = strtol(bids_ptr, &endptr, 10);
            if (endptr == bids_ptr) {
                break;
            }
            if (b >= 1 && b <= MAX_BOARD) {
                int bid_val = (int)b;
                if (hbfl_cached_count == 0 || bid_val > hbfl_cached_bids[hbfl_cached_count - 1]) {
                    if (hbfl_cached_count >= hbfl_cached_cap) {
                        int new_cap = hbfl_cached_cap ? hbfl_cached_cap * 2 : 16;
                        int *new_buf = (int *)realloc(hbfl_cached_bids, (size_t)new_cap * sizeof(int));
                        if (!new_buf) {
                            return -1;
                        }
                        hbfl_cached_bids = new_buf;
                        hbfl_cached_cap = new_cap;
                    }
                    hbfl_cached_bids[hbfl_cached_count++] = bid_val;
                }
            }
            bids_ptr = endptr;
        }
    }
    return 0;
}

int friend_svc_is_hidden_board_friend(int bid, int uid) {
    if (bid <= 0 || bid > MAX_BOARD || uid <= 0) {
        return 0;
    }
    int shm_gen = get_hbfl_generation();
    time4_t cur_time = COMMON_TIME;

    if (hbfl_cached_uid != uid ||
        hbfl_cached_gen < 0 ||
        hbfl_cached_gen != shm_gen ||
        (cur_time - hbfl_cached_time) >= HBFLexpire) {
        if (hbfl_fail_time > 0 && (cur_time - hbfl_fail_time) < 5) {
            return -1;
        }
        if (friend_svc_hbfl_query_user(uid) < 0) {
            hbfl_fail_time = cur_time;
            return -1;
        }
        hbfl_fail_time = 0;
        hbfl_cached_uid = uid;
        hbfl_cached_gen = shm_gen; /* generation observed before the query */
        hbfl_cached_time = cur_time;
    }

    if (hbfl_cached_count <= 0 || !hbfl_cached_bids) {
        return 0;
    }
    int lo = 0, hi = hbfl_cached_count - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        int v = hbfl_cached_bids[mid];
        if (v == bid) {
            return 1;
        }
        if (v < bid) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return 0;
}



int is_aloha_svc_enabled(void) {
    assert(SHM);
    return true;
}

int send_aloha_message(int sid, pid_t to_pid, pid_t from_pid, const char *from_id) {
    char msg[128];
    time4_t now = time4(NULL);
    SNPRINTF(msg, "<<上站通知>> -- 我來啦! [%s]", Cdatelite(&now));
    return write_message(sid, to_pid, from_pid, from_id, msg, MSGMODE_ALOHA);
}

/* Deregister the session from friend.svc. Peers' friend_online[] entries are
 * owned by friend.svc (it removes them, or rescans on restart), so never touch
 * them here; only our own array is cleared. */
int logout_friend_online(userinfo_t *utmp) {
    if (!utmp || !SHM) {
        return 0;
    }
    if (utmp->pid > 0 && utmp->userid[0])
        friend_svc_logout(utmp->userid, utmp->pid);
    memset(utmp->friend_online, 0, sizeof(utmp->friend_online));
    utmp->friendtotal = 0;
    return 0;
}
