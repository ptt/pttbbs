/* $Id$ */
#include "bbs.h"

char           *
Ptt_prints(char *str, int mode)
{
    char            strbuf[256];
    int             r, w;
    for( r = w = 0 ; str[r] != 0 && w < (sizeof(strbuf) - 1) ; ++r )
	if( str[r] != '\033' )
	    strbuf[w++] = str[r];
	else{
	    if( str[++r] != '*' ){
		strbuf[w++] = '\033';
		strbuf[w++] = str[r];
	    }
	    else{
		switch( str[++r] ){
		case 's':
		    w += snprintf(&strbuf[w], sizeof(strbuf) - w,
				  "%s", cuser.userid);
		    break;
		case 't':
		    w += snprintf(&strbuf[w], sizeof(strbuf) - w,
				  "%s", Cdate(&now));
		    break;
		case 'u':
		    w += snprintf(&strbuf[w], sizeof(strbuf) - w,
				  "%d", SHM->UTMPnumber);
		    break;
		case 'b':
		    w += snprintf(&strbuf[w], sizeof(strbuf) - w,
				  "%d/%d", cuser.month, cuser.day);
		    break;
		case 'l':
		    w += snprintf(&strbuf[w], sizeof(strbuf) - w,
				  "%d", cuser.numlogins);
		    break;
		case 'p':
		    w += snprintf(&strbuf[w], sizeof(strbuf) - w,
				  "%d", cuser.numposts);
		    break;
		case 'n':
		    w += snprintf(&strbuf[w], sizeof(strbuf) - w,
				  "%s", cuser.username);
		    break;
		case 'm':
		    w += snprintf(&strbuf[w], sizeof(strbuf) - w,
				  "%d", cuser.money);
		    break;
		default:
		    strbuf[w++] = '\033';
		    strbuf[w++] = '*';
		    strbuf[w++] = str[r];
		    ++w; /* 後面有 --w */
		}
		--w;
	    }
	}
    strbuf[w] = 0;
    strcpy(str, strbuf);
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
    fo=open(dst, O_WRONLY | O_TRUNC | O_CREAT, 0600);
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
