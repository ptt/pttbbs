#include "cmbbs.h"
#include "common.h"
#include "var.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int send_alohad_req(const char *json_payload) {
    char sock_path[PATHLEN];
    SNPRINTF(sock_path, "%s/run/aloha.svc.sock", BBSHOME);

    int sfd = toconnect3(sock_path, 0, 10000); // non-blocking connect with 10ms timeout
    if (sfd < 0) {
        return -1;
    }

    int len = (int)strlen(json_payload);
    int n = towrite(sfd, json_payload, len);
    close(sfd);
    return (n == len) ? 0 : -1;
}

int aloha_notify_login(const char *userid, pid_t pid, int sid) {
    if (!userid || !*userid) {
        return -1;
    }
    char payload[256];
    SNPRINTF(payload, "{\"action\":\"login\",\"userid\":\"%s\",\"pid\":%d,\"sid\":%d}\n", userid, (int)pid, sid);
    return send_alohad_req(payload);
}

int aloha_notify_logout(const char *userid, pid_t pid) {
    if (!userid || !*userid) {
        return -1;
    }
    char payload[256];
    SNPRINTF(payload, "{\"action\":\"logout\",\"userid\":\"%s\",\"pid\":%d}\n", userid, (int)pid);
    return send_alohad_req(payload);
}



int is_aloha_svc_enabled(void) {
    assert(SHM);
    return SHM->GV2.e.aloha_svc != 0;
}

int send_aloha_message(int sid, pid_t to_pid, pid_t from_pid, const char *from_id) {
    char msg[128];
    time4_t now = time4(NULL);
    SNPRINTF(msg, "<<上站通知>> -- 我來啦! [%s]", Cdatelite(&now));
    return write_message(sid, to_pid, from_pid, from_id, msg, MSGMODE_ALOHA);
}
