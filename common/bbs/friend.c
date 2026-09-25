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
