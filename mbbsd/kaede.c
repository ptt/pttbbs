/* $Id$ */
#include "bbs.h"

char           *
Ptt_prints(char *str, int mode)
{
    char           *po, strbuf[256];

    while ((po = strstr(str, "\033*s"))) {
	po[0] = 0;
	snprintf(strbuf, sizeof(strbuf), "%s%s%s", str, cuser.userid, po + 3);
	strcpy(str, strbuf);
    }
    while ((po = strstr(str, "\033*t"))) {
	po[0] = 0;
	snprintf(strbuf, sizeof(strbuf), "%s%s%s", str, Cdate(&now), po + 3);
	strcpy(str, strbuf);
    }
    while ((po = strstr(str, "\033*u"))) {
	int             attempts;

	attempts = SHM->UTMPnumber;
	po[0] = 0;
	snprintf(strbuf, sizeof(strbuf), "%s%d%s", str, attempts, po + 3);
	strcpy(str, strbuf);
    }
    while ((po = strstr(str, "\033*b"))) {
	po[0] = 0;
	snprintf(strbuf, sizeof(strbuf),
		 "%s%d/%d%s", str, cuser.month, cuser.day, po + 3);
	strcpy(str, strbuf);
    }
    while ((po = strstr(str, "\033*l"))) {
	po[0] = 0;
	snprintf(strbuf, sizeof(strbuf),
		 "%s%d%s", str, cuser.numlogins, po + 3);
	strcpy(str, strbuf);
    }
    while ((po = strstr(str, "\033*p"))) {
	po[0] = 0;
	snprintf(strbuf, sizeof(strbuf),
		 "%s%d%s", str, cuser.numposts, po + 3);
	strcpy(str, strbuf);
    }
    while ((po = strstr(str, "\033*n"))) {
	po[0] = 0;
	snprintf(strbuf, sizeof(strbuf),
		 "%s%s%s", str, cuser.username, po + 3);
	strcpy(str, strbuf);
    }
    while ((po = strstr(str, "\033*m"))) {
	po[0] = 0;
	snprintf(strbuf, sizeof(strbuf),
		 "%s%d%s", str, cuser.money, po + 3);
	strcpy(str, strbuf);
    }
    strip_ansi(str, str, mode);
    return str;
}

int
Rename(char *src, char *dst)
{
    char            buf[256];
    if (rename(src, dst) == 0)
	return 0;
    if (!strchr(src, ';') && !strchr(dst, ';'))
	// Ptt 防不正常指令 // XXX 這樣是不夠的
    {
	snprintf(buf, sizeof(buf), "/bin/mv %s %s", src, dst);
	system(buf);
    }
    return -1;
}

int
Copy(char *src, char *dst)
{
    int fi, fo, bytes;
    char buf[8192];
    fi=open(src, O_RDONLY);
    if(fi<0) return -1;
    fo=open(dst, O_WRONLY | O_TRUNC | O_CREAT);
    if(fo<0) {close(fi); return -1;}
    while((bytes=read(fi, buf, 8192))>0)
         write(fo, buf, bytes);
    close(fo);
    close(fi);
    return 0;  
}
int
Link(char *src, char *dst)
{
    char            cmd[200];

    if (strcmp(src, BBSHOME "/home") == 0)
	return 1;
    if (symlink(dst, src) == 0)
	return 0;

    snprintf(cmd, sizeof(cmd), "/bin/cp -R %s %s", src, dst);
    return system(cmd);
}

char           *
my_ctime(const time_t * t, char *ans, int len)
{
    struct tm      *tp;

    tp = localtime(t);
    snprintf(ans, len,
	     "%02d/%02d/%02d %02d:%02d:%02d", (tp->tm_year % 100),
	     tp->tm_mon + 1, tp->tm_mday, tp->tm_hour, tp->tm_min, tp->tm_sec);
    return ans;
}
