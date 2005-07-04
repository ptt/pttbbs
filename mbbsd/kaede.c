/* $Id$ */
#include "bbs.h"

char           *
Ptt_prints(char *str, int mode)
{
    char            strbuf[256];
    int             r, w;
    for( r = w = 0 ; str[r] != 0 && w < (sizeof(strbuf) - 1) ; ++r )
	if( str[r] != ESC_CHR )
	    strbuf[w++] = str[r];
	else{
	    if( str[++r] != '*' ){
		strbuf[w++] = ESC_CHR;
		strbuf[w++] = str[r];
	    }
	    else{
		/* Note, w will increased by copied length after */
		switch( str[++r] ){
		case 's':
		    strlcpy(strbuf+w, cuser.userid, sizeof(strbuf)-w);
		    w += strlen(strbuf+w);
		    break;
		case 't':
		    strlcpy(strbuf+w, Cdate(&now), sizeof(strbuf)-w);
		    w += strlen(strbuf+w);
		    break;

		    /* disabled for security issue.
		     * we support only entries can be queried by others now.
		     */
#ifdef LOW_SECURITY
		case 'u':
		    w += snprintf(&strbuf[w], sizeof(strbuf) - w,
				  "%d", SHM->UTMPnumber);
		    break;
		case 'b':
		    w += snprintf(&strbuf[w], sizeof(strbuf) - w,
				  "%d/%d", cuser.month, cuser.day);
		    break;
		case 'm':
		    w += snprintf(&strbuf[w], sizeof(strbuf) - w,
				  "%d", cuser.money);
		    break;
#else
		case 'm':
		    w += snprintf(&strbuf[w], sizeof(strbuf) - w,
				  "%s", money_level(cuser.money));
		    break;
#endif

		case 'l':
		    w += snprintf(&strbuf[w], sizeof(strbuf) - w,
				  "%d", cuser.numlogins);
		    break;
		case 'p':
		    w += snprintf(&strbuf[w], sizeof(strbuf) - w,
				  "%d", cuser.numposts);
		    break;
		case 'n':
		    strlcpy(strbuf+w, cuser.nickname, sizeof(strbuf)-w);
		    w += strlen(strbuf+w);
		    break;
		/* It's saver not to send these undefined escape string. 
		default:
		    strbuf[w++] = ESC_CHR;
		    strbuf[w++] = '*';
		    strbuf[w++] = str[r];
		    */
		}
	    }
	}
    strbuf[w] = 0;
    strip_ansi(str, strbuf, mode);
    return str;
}

int
Rename(const char *src, const char *dst)
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
Copy(const char *src, const char *dst)
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
Link(const char *src, const char *dst)
{
    if (strcmp(src, BBSHOME "/home") == 0)
	return 1;
    if (symlink(src, dst) == 0)
	return 0;

    return Copy(src, dst);
}

char           *
my_ctime(const time4_t * t, char *ans, int len)
{
    struct tm      *tp;

    tp = localtime4((time4_t*)t);
    snprintf(ans, len,
	     "%02d/%02d/%02d %02d:%02d:%02d", (tp->tm_year % 100),
	     tp->tm_mon + 1, tp->tm_mday, tp->tm_hour, tp->tm_min, tp->tm_sec);
    return ans;
}
