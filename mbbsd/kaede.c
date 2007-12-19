/* $Id$ */
#include "bbs.h"

// TODO move stuff to libbbs(or util)/string.c, ...
// this file can be removed (or not?)

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
		case 'n':
		    strlcpy(strbuf+w, cuser.nickname, size-w);
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
		case 'l':
		    w += snprintf(&strbuf[w], size - w,
				  "%d", cuser.numlogins);
		    break;
		case 'p':
		    w += snprintf(&strbuf[w], size - w,
				  "%d", cuser.numposts);
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
#endif

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

// utility from screen.c
void
outs_n(const char *str, int n)
{
    while (*str && n--) {
	outc(*str++);
    }
}

// XXX left-right (for large term)
// TODO someday please add ANSI detection version
void 
outslr(const char *left, int leftlen, const char *right, int rightlen)
{
    if (left == NULL)
	left = "";
    if (right == NULL)
	right = "";
    if(*left && leftlen < 0)
	leftlen = strlen(left);
    if(*right && rightlen < 0)
	rightlen = strlen(right);
    // now calculate padding
    rightlen = t_columns - leftlen - rightlen;
    outs(left);

    // ignore right msg if we need to.
    if(rightlen >= 0)
    {
	while(--rightlen > 0)
	    outc(' ');
	outs(right);
    } else {
	rightlen = t_columns - leftlen;
	while(--rightlen > 0)
	    outc(' ');
    }
}


/* Jaky */
void
out_lines(const char *str, int line)
{
    while (*str && line) {
	outc(*str);
	if (*str == '\n')
	    line--;
	str++;
    }
}

void
outmsg(const char *msg)
{
    move(b_lines - msg_occupied, 0);
    clrtoeol();
    outs(msg);
}

void
outmsglr(const char *msg, int llen, const char *rmsg, int rlen)
{
    move(b_lines - msg_occupied, 0);
    clrtoeol();
    outslr(msg, llen, rmsg, rlen);
    outs(ANSI_RESET ANSI_CLRTOEND);
}

void
prints(const char *fmt,...)
{
    va_list         args;
    char            buff[1024];

    va_start(args, fmt);
    vsnprintf(buff, sizeof(buff), fmt, args);
    va_end(args);
    outs(buff);
}

void
mouts(int y, int x, const char *str)
{
    move(y, x);
    clrtoeol();
    outs(str);
}

// vim:ts=4
