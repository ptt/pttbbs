/* $Id: io.c,v 1.16 2002/05/15 08:55:30 ptt Exp $ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <signal.h>
#include "config.h"
#include "pttstruct.h"
#include "perm.h"
#include "modes.h"
#include "common.h"
#include "proto.h"

#if defined(linux)
#define OBUFSIZE  2048
#define IBUFSIZE  128
#else
#define OBUFSIZE  4096
#define IBUFSIZE  256
#endif

extern int current_font_type;
extern char *fn_proverb;
extern userinfo_t *currutmp;
extern unsigned int currstat;
extern pid_t currpid;
extern int errno;
extern screenline_t *big_picture;
extern int t_lines, t_columns;  /* Screen size / width */
extern water_t water[6], *swater[5], *water_which;
extern char water_usies;

static char outbuf[OBUFSIZE], inbuf[IBUFSIZE];
static int obufsize = 0, ibufsize = 0;
static int icurrchar = 0;

time_t now;

/* ----------------------------------------------------- */
/* output routines                                       */
/* ----------------------------------------------------- */

void oflush() {
    if(obufsize) {
	write(1, outbuf, obufsize);
	obufsize = 0;
    }
}

void init_buf()
{

   memset(inbuf,0,IBUFSIZE);
}
void output(char *s, int len) {
    /* Invalid if len >= OBUFSIZE */

    if(obufsize + len > OBUFSIZE) {
	write(1, outbuf, obufsize);
	obufsize = 0;
    }
    memcpy(outbuf + obufsize, s, len);
    obufsize += len;
}

int ochar(int c) {
    if(obufsize > OBUFSIZE - 1) {
	write(1, outbuf, obufsize);
	obufsize = 0;
    }
    outbuf[obufsize++] = c;
    return 0;
}

/* ----------------------------------------------------- */
/* input routines                                        */
/* ----------------------------------------------------- */

static int i_newfd = 0;
static struct timeval i_to, *i_top = NULL;
static int (*flushf) () = NULL;

void add_io(int fd, int timeout) {
    i_newfd = fd;
    if(timeout) {
	i_to.tv_sec = timeout;
	i_to.tv_usec = 16384;  /* Ptt: 改成16384 避免不按時for loop吃cpu time
				  16384 約每秒64次 */
	i_top = &i_to;
    } else
	i_top = NULL;
}

int num_in_buf() {
    return icurrchar - ibufsize;
}

int watermode = -1, wmofo = -1; 
/*
  WATERMODE(WATER_ORIG) | WATERMODE(WATER_NEW):
  Ptt 水球回顧用的參數
      watermode = -1  沒在回水球
                = 0   在回上一顆水球  (Ctrl-R)
	        > 0   在回前 n 顆水球 (Ctrl-R Ctrl-R)

  WATERMODE(WATER_OFO)  by in2
        wmofo     = -1  沒在回水球
	          = 0   正在回水球
		  = 1   回水球間又接到水球
        wmofo     >=0 時收到水球將只顯示, 不會到water[]裡,
	              待回完水球的時候一次寫入.
*/

/*
	dogetch() is not reentrant-safe. SIGUSR[12] might happen at any time,
	and dogetch() might be called again, and then ibufsize/icurrchar/inbuf
	might be inconsistent.
	We try to not segfault here... 
*/

static int dogetch() {
    int len;
    static time_t lastact;
    if(ibufsize <= icurrchar) {

	if(flushf)
	    (*flushf)();

	refresh();

	if(i_newfd) {
	
	    struct timeval timeout;
	    fd_set readfds;
			
	    if(i_top) timeout=*i_top; /* copy it because select() might change it */
			
	    FD_ZERO(&readfds);
	    FD_SET(0, &readfds);
	    FD_SET(i_newfd, &readfds);

	    /* jochang: modify first argument of select from FD_SETSIZE */
	    /* since we are only waiting input from fd 0 and i_newfd(>0) */
		
	    while((len = select(i_newfd+1, &readfds, NULL, NULL, i_top?&timeout:NULL))<0)
	    {
		if(errno != EINTR)
		    abort_bbs(0);
		    /* raise(SIGHUP); */
	    }

	    if(len == 0)
		return I_TIMEOUT;

	    if(i_newfd && FD_ISSET(i_newfd, &readfds))
		return I_OTHERDATA;
	}


        while((len = read(0, inbuf, IBUFSIZE)) <= 0) {
            if(len == 0 || errno != EINTR)
                abort_bbs(0);
                /* raise(SIGHUP); */
        }              
	ibufsize = len;
	icurrchar = 0;
    }

    if(currutmp) 
      {
        now= time(0);
        if(now-lastact < 3)	
            currutmp->lastact=now;
        lastact=now;
      }
    return inbuf[icurrchar++];
}
extern userec_t cuser;
static int water_which_flag=0;
int igetch() {
    register int ch;
    while((ch = dogetch())) {
	switch(ch) {
	case Ctrl('L'):
	    redoscr();
	    continue;
	case Ctrl('U'):
	    if(currutmp != NULL &&  currutmp->mode != EDITING
	       && currutmp->mode !=  LUSERS && currutmp->mode) {

		screenline_t *screen0 = calloc(t_lines, sizeof(screenline_t));
		int y, x, my_newfd;

		getyx(&y, &x);
		memcpy(screen0, big_picture, t_lines * sizeof(screenline_t));
		my_newfd = i_newfd;
		i_newfd = 0;
		t_users();
		i_newfd = my_newfd;
		memcpy(big_picture, screen0, t_lines * sizeof(screenline_t));
		move(y, x);
		free(screen0);
		redoscr();
		continue;
	    } else
		return (ch);
        case KEY_TAB:
	    if( WATERMODE(WATER_ORIG) || WATERMODE(WATER_NEW) )
		if( currutmp != NULL && watermode > 0 ){
		    watermode = (watermode + water_which->count)
			% water_which->count + 1;
		    t_display_new();
		    continue;
		}
	    return ch;
	    break;

        case Ctrl('R'):
	    if(currutmp == NULL)
		return (ch);

	    if( currutmp->msgs[0].pid && 
		WATERMODE(WATER_OFO) && wmofo == -1 ){
		int y, x, my_newfd;
		screenline_t *screen0 = calloc(t_lines, sizeof(screenline_t));
		memcpy(screen0, big_picture, t_lines * sizeof(screenline_t));
		getyx(&y, &x);		
		my_newfd = i_newfd;
		i_newfd = 0;
		my_write2();
		memcpy(big_picture, screen0, t_lines * sizeof(screenline_t));
		i_newfd = my_newfd;
		move(y, x);
		free(screen0);
		redoscr();
		continue;
	    }
	   else if(!WATERMODE(WATER_OFO))
            {
		if( watermode > 0 ){
		    watermode = (watermode + water_which->count)
			% water_which->count + 1;
		    t_display_new();
		    continue;
		}
		else if( currutmp->mode == 0 &&
			 (currutmp->chatid[0]==2 || currutmp->chatid[0]==3) &&
			 water_which->count != 0 && watermode == 0) {
		    /* 第二次按 Ctrl-R */
		    watermode = 1;
		    t_display_new();
		    continue;
		}
		else if(watermode==-1 && currutmp->msgs[0].pid) {
		    /* 第一次按 Ctrl-R (必須先被丟過水球) */
		    screenline_t *screen0;
		    int y, x, my_newfd;
		    screen0 = calloc(t_lines, sizeof(screenline_t));
		    getyx(&y, &x);
		    memcpy(screen0, big_picture, t_lines*sizeof(screenline_t));
		    
		    /* 如果正在talk的話先不處理對方送過來的封包 (不去select) */
		    my_newfd = i_newfd;
		    i_newfd = 0;
		    show_call_in(0, 0);
		    watermode = 0;
		    my_write(currutmp->msgs[0].pid, "水球丟過去 ： ",
			     currutmp->msgs[0].userid, 0, NULL);
		    i_newfd = my_newfd;
		
		    /* 還原螢幕 */
		    memcpy(big_picture, screen0, t_lines*sizeof(screenline_t));
		    move(y, x);
		    free(screen0);
		    redoscr();
		    continue;
		}
             }
	    return ch;
        case '\n':   /* Ptt把 \n拿掉 */
	    continue;
        case Ctrl('T'):
	    if( WATERMODE(WATER_ORIG) || WATERMODE(WATER_NEW) ){
		if(watermode > 0) {
		    if(watermode>1)
			watermode--;
		    else
			watermode = water_which->count;	
		    t_display_new();
		    continue;
		}
	    }
	    return (ch);

        case Ctrl('F'):
	    if( WATERMODE(WATER_NEW) ){
		if(watermode >0){
		    if( water_which_flag == (int)water_usies )
			water_which_flag = 0;
		    else
			water_which_flag =
		                   (water_which_flag+1) % (int)(water_usies+1);
		    if(water_which_flag==0)
			water_which = &water[0];
		    else
			water_which = swater[water_which_flag-1];
		    watermode = 1;
		    t_display_new();
		    continue;
		}
	    }
	    return ch;

        case Ctrl('G'):
	    if( WATERMODE(WATER_NEW) ){
		if( watermode > 0 ){
		    water_which_flag=(water_which_flag+water_usies)%(water_usies+1);
		    if(water_which_flag==0)
			water_which = &water[0];
		    else
			water_which = swater[water_which_flag-1];
		    watermode = 1;
		    t_display_new();
		    continue;
		}
	    }
	    return ch;

        default:
	    return ch;
	}
    }
    return 0;
}

int oldgetdata(int line, int col, char *prompt, char *buf, int len, int echo) {
    register int ch, i;
    int clen;
    int x = col, y = line;
    extern unsigned char scr_cols;
#define MAXLASTCMD 12
    static char lastcmd[MAXLASTCMD][80];

    strip_ansi(buf, buf, STRIP_ALL);

    if(prompt) {

	move(line, col);

	clrtoeol();

	outs(prompt);

	x += strip_ansi(NULL,prompt,0);
    }

    if(!echo) {
	len--;
	clen = 0;
	while((ch = igetch()) != '\r') {
	    if(ch == '\177' || ch == Ctrl('H')) {
		if(!clen) {
		    bell();
		    continue;
		}
		clen--;
		if(echo) {
		    ochar(Ctrl('H'));
		    ochar(' ');
		    ochar(Ctrl('H'));
		}
		continue;
	    }
// Ptt
#ifdef BIT8
	    if(!isprint2(ch))
#else
		if(!isprint(ch))
#endif
		{
		    if(echo)
			bell();
		    continue;
		}

	    if(clen >= len)  {
		if(echo)
		    bell();
		continue;
	    }
	    buf[clen++] = ch;
	    if(echo)
		ochar(ch);
	}
	buf[clen] = '\0';
	outc('\n');
	oflush();
    } else {
	int cmdpos = -1;
	int currchar = 0;

	standout();
	for(clen = len--; clen; clen--)
	    outc(' ');
	standend();
	buf[len] = 0;
	move(y, x);
	edit_outs(buf);
	clen = currchar = strlen(buf);

	while(move(y, x + currchar), (ch = igetkey()) != '\r') {
	    switch(ch) {
	    case KEY_DOWN:
	    case Ctrl('N'):
                buf[clen] = '\0';  /* Ptt */
                strncpy(lastcmd[cmdpos], buf, 79);
                cmdpos += MAXLASTCMD - 2;
	    case Ctrl('P'):
	    case KEY_UP:
                if(ch == KEY_UP || ch == Ctrl('P')) {
		    buf[clen] = '\0';  /* Ptt */
		    strncpy(lastcmd[cmdpos], buf, 79);
                }
                cmdpos++;
                cmdpos %= MAXLASTCMD;
                strncpy(buf, lastcmd[cmdpos], len);
                buf[len] = 0;

                move(y, x);                   /* clrtoeof */
                for(i = 0; i <= clen; i++)
		    outc(' ');
                move(y, x);
                edit_outs(buf);
                clen = currchar = strlen(buf);
                break;
	    case KEY_LEFT:
                if(currchar)
		    --currchar;
                break;
	    case KEY_RIGHT:
                if(buf[currchar])
		    ++currchar;
                break;
	    case '\177':
	    case Ctrl('H'):
                if(currchar) {
		    currchar--;
		    clen--;
		    for(i = currchar; i <= clen; i++)
			buf[i] = buf[i + 1];
		    move(y, x + clen);
		    outc(' ');
		    move(y, x);
		    edit_outs(buf);
                }
                break;
	    case Ctrl('Y'):
                currchar = 0;
	    case Ctrl('K'):
                buf[currchar] = 0;
                move(y, x + currchar);
                for(i = currchar; i < clen; i++)
		    outc(' ');
                clen = currchar;
                break;
	    case Ctrl('D'):
                if(buf[currchar]) {
		    clen--;
		    for(i = currchar; i <= clen; i++)
			buf[i] = buf[i + 1];
		    move(y, x + clen);
		    outc(' ');
		    move(y, x);
		    edit_outs(buf);
                }
                break;
	    case Ctrl('A'):
                currchar = 0;
                break;
	    case Ctrl('E'):
                currchar = clen;
                break;
	    default:
                if(isprint2(ch) && clen < len && x + clen < scr_cols) {
		    for(i = clen + 1; i > currchar;i--)
			buf[i] = buf[i - 1];
		    buf[currchar] = ch;
		    move(y, x + currchar);
		    edit_outs(buf + currchar);
		    currchar++;
		    clen++;
		}
                break;
	    }/* end case */
	}  /* end while */

	if(clen > 1)
	    for(cmdpos = MAXLASTCMD - 1; cmdpos; cmdpos--) {
                strcpy(lastcmd[cmdpos], lastcmd[cmdpos - 1]);
                strncpy(lastcmd[0], buf, len);
	    }
	if(echo)
	    outc('\n');
	refresh();
    }
    if((echo == LCECHO) && ((ch = buf[0]) >= 'A') && (ch <= 'Z'))
	buf[0] = ch | 32;
#ifdef SUPPORT_GB    
    if(echo == DOECHO &&  current_font_type == TYPE_GB)
    {
	strcpy(buf,hc_convert_str(buf, HC_GBtoBIG, HC_DO_SINGLE));
    }
#endif
    return clen;
}

/* Ptt */
int getdata_buf(int line, int col, char *prompt, char *buf, int len, int echo) {
    return oldgetdata(line, col, prompt, buf, len, echo);
}

char
getans(char *prompt)
{
  char ans[5];

  getdata(t_lines-1, 0, prompt, ans, sizeof(ans), LCECHO);
  return ans[0];
}

int getdata_str(int line, int col, char *prompt, char *buf, int len, int echo, char *defaultstr) {
    strncpy(buf, defaultstr, len);
    
    buf[len - 1] = 0;
    return oldgetdata(line, col, prompt, buf, len, echo);
}

int getdata(int line, int col, char *prompt, char *buf, int len, int echo) {
    buf[0] = 0;
    return oldgetdata(line, col, prompt, buf, len, echo);
}

int
rget(int x,char *prompt)
{
  register int ch;

  move(x,0);
  clrtobot(); 
  outs(prompt);
  refresh();

  ch = igetch();
  if( ch >= 'A' && ch <= 'Z') ch |= 32;

   return ch;
}


int KEY_ESC_arg;

int igetkey() {
    int mode;
    int ch, last;

    mode = last = 0;
    while(1) {
	ch = igetch();
	if(mode == 0) {
	    if(ch == KEY_ESC)
		mode = 1;
	    else
		return ch;              /* Normal Key */
	} else if (mode == 1) {         /* Escape sequence */
	    if(ch == '[' || ch == 'O')
		mode = 2;
	    else if(ch == '1' || ch == '4')
		mode = 3;
	    else {
		KEY_ESC_arg = ch;
		return KEY_ESC;
	    }
	} else if(mode == 2) {          /* Cursor key */
	    if(ch >= 'A' && ch <= 'D')
		return KEY_UP + (ch - 'A');
	    else if(ch >= '1' && ch <= '6')
		mode = 3;
	    else
		return ch;
	} else if (mode == 3) {         /* Ins Del Home End PgUp PgDn */
	    if(ch == '~')
		return KEY_HOME + (last - '1');
	    else
		return ch;
	}
	last = ch;
    }
}

