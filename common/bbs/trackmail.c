// Client of trackmail.svc: tells it about the mails users send.
#include "cmbbs.h"
#include "common.h"
#include "var.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int trackmail_notify_send(const char *sender, const char *recipient, const char *subject, const char *filename, const char *mail_path, time4_t ts) {
    char sock_path[PATHLEN];
    char payload[1024];

    if (!sender || !recipient || !subject)
        return -1;

    char utf8_subject[STRLEN * 3 + 1];
    big5_to_utf8(subject, utf8_subject, sizeof(utf8_subject));

    SNPRINTF(sock_path, "%s/run/trackmail.svc.sock", BBSHOME);

    int sfd = toconnect3(sock_path, 0, 10000); // 10ms non-blocking connect timeout
    if (sfd < 0) {
        return -1; // Daemon not running or connection failed
    }

    if (ts <= 0)
        ts = time4(NULL);

    SNPRINTF(payload, "{\"action\":\"track_mail\",\"sender\":\"%s\",\"recipient\":\"%s\",\"subject\":\"%s\",\"filename\":\"%s\",\"mail_path\":\"%s\",\"timestamp\":%u}\n",
             sender, recipient, utf8_subject, filename ? filename : "", mail_path ? mail_path : "", (unsigned int)ts);

    int len = (int)strlen(payload);
    int n = towrite(sfd, payload, len);
    close(sfd);
    return (n == len) ? 0 : -1;
}
