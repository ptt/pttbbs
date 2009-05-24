/* $Id$ */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pwd.h>
#include <syslog.h>
#include "config.h"

#define MAX_REMOTE_IP_LEN 32

static void get_remote_ip(int len, char *remote_ip)
{
    char frombuf[100];
    // note, SSH_CLIENT is deprecated since 2002
    char *ssh_client = getenv("SSH_CONNECTION");

    if (ssh_client) {
	// SSH_CONNECTION format: "client-ip client-port server-ip server-port"
	sscanf(ssh_client, "%s", frombuf);
    } else {
	strcpy(frombuf, "127.0.0.1");
    }

    strlcpy(remote_ip, frombuf, len);
}

/*
   show ban file
   if filename exist, print it out, sleep 10 second, and return 0;
   otherwise, return -1.
 */
static int showbanfile(const char *filename)
{
    FILE *fp;
    char buf[256];

    fp = fopen(filename, "r");
    if (fp)
    {
	while (fgets(buf, sizeof(buf), fp))
	    fputs(buf, stdout);
	printf("\n============================="
	       "=============================\n");
	fclose(fp);
	sleep(10);
    }
    return fp ? 0 : -1;
}

int main(void)
{
    int uid;
    char *tty, remote_ip[MAX_REMOTE_IP_LEN + 1];

    openlog("bbsrf", LOG_PID | LOG_PERROR, LOG_USER);
    chdir(BBSHOME);
    uid = getuid();

    if (uid != BBSUID) {
	syslog(LOG_ERR, "UID DOES NOT MATCH");
	return -1;
    }
    if (!getpwuid(uid)) {
	syslog(LOG_ERR, "YOU DONT EXIST");
	return -1;
    }


    if (!showbanfile(BAN_FILE)) {
	return 1;
    }

    get_remote_ip(sizeof(remote_ip), remote_ip);

    tty = ttyname(0);
    if (tty == NULL)
	tty = "notty";

    execl(BBSPROG, "mbbsd", remote_ip, tty, NULL);
    syslog(LOG_ERR, "execl(): %m");
    sleep(3); // prevent flooding

    return -1;
}
