/* $Id$ */
#include "bbs.h"

char           *
Ptt_prints(char *str, size_t size, int mode)
{
    char           *strbuf = alloca(size);
    int             r, w;
    for( r = w = 0 ; str[r] != 0 && w < (size - 1) ; ++r )
	if( str[r] != ESC_CHR )
	    strbuf[w++] = str[r];
	else{
	    if( str[++r] != '*' ){
		if(w+2>=size-1) break;
		strbuf[w++] = ESC_CHR;
		strbuf[w++] = str[r];
	    }
	    else{
		/* Note, w will increased by copied length after */
		switch( str[++r] ){
		case 's':
		    strlcpy(strbuf+w, cuser.userid, size-w);
		    w += strlen(strbuf+w);
		    break;
		case 't':
		    strlcpy(strbuf+w, Cdate(&now), size-w);
		    w += strlen(strbuf+w);
		    break;
		case 'u':
		    w += snprintf(&strbuf[w], size - w,
				  "%d", SHM->UTMPnumber);
		    break;

		    /* disabled for security issue.
		     * we support only entries can be queried by others now.
		     */
#ifdef LOW_SECURITY
		case 'b':
		    w += snprintf(&strbuf[w], size - w,
				  "%d/%d", cuser.month, cuser.day);
		    break;
		case 'm':
		    w += snprintf(&strbuf[w], size - w,
				  "%d", cuser.money);
		    break;
#else

#if 0
		case 'm':
		    w += snprintf(&strbuf[w], size - w,
				  "%s", money_level(cuser.money));
		    break;
#endif

#endif

		case 'l':
		    w += snprintf(&strbuf[w], size - w,
				  "%d", cuser.numlogins);
		    break;
		case 'p':
		    w += snprintf(&strbuf[w], size - w,
				  "%d", cuser.numposts);
		    break;
		case 'n':
		    strlcpy(strbuf+w, cuser.nickname, size-w);
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
    if (rename(src, dst) == 0)
	return 0;
    if (!strchr(src, ';') && !strchr(dst, ';'))
    {
	pid_t pid = fork();
	if (pid == 0)
	    execl("/bin/mv", "mv", "-f", src, dst, (char *)NULL);
	else if (pid > 0)
	    waitpid(pid, NULL, 0);
	else
	    return -1;
    }
    return 0;
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
    while((bytes=read(fi, buf, sizeof(buf)))>0)
         write(fo, buf, bytes);
    close(fo);
    close(fi);
    return 0;  
}

int
CopyN(const char *src, const char *dst, int n)
{
    int fi, fo, bytes;
    char buf[8192];

    fi=open(src, O_RDONLY);
    if(fi<0) return -1;

    fo=open(dst, O_WRONLY | O_TRUNC | O_CREAT, 0600);
    if(fo<0) {close(fi); return -1;}

    while(n > 0 && (bytes=read(fi, buf, sizeof(buf)))>0)
    {
	 n -= bytes;
	 if (n < 0)
	     bytes += n;
         write(fo, buf, bytes);
    }
    close(fo);
    close(fi);
    return 0;  
}

/* append data from tail of src (starting point=off) to dst */
int
AppendTail(const char *src, const char *dst, int off)
{
    int fi, fo, bytes;
    char buf[8192];

    fi=open(src, O_RDONLY);
    if(fi<0) return -1;

    fo=open(dst, O_WRONLY | O_APPEND | O_CREAT, 0600);
    if(fo<0) {close(fi); return -1;}
    
    if(off > 0)
	lseek(fi, (off_t)off, SEEK_SET);

    while((bytes=read(fi, buf, sizeof(buf)))>0)
    {
         write(fo, buf, bytes);
    }
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
