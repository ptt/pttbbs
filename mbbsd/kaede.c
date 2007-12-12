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

