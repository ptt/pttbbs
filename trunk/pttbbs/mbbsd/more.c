/* $Id: more.c,v 1.14 2002/06/04 13:08:34 in2 Exp $ */
#include "bbs.h"
#define MORE_BUFSIZE	4096
#define MORE_WINSIZE	4096
#define STR_ANSICODE    "[0123456789;,"

static int more_base, more_size, more_head;
static unsigned char more_pool[MORE_BUFSIZE];


#define MAXPATHLEN 256
static char *more_help[] = {
    "\0閱\讀文章功\能鍵使用說明",
    "\01游標移動功\能鍵",
    "(↑)                  上捲一行",
    "(↓)(Enter)           下捲一行",
    "(^B)(PgUp)(BackSpace) 上捲一頁",
    "(→)(PgDn)(Space)     下捲一頁",
    "(0)(g)(Home)          檔案開頭",
    "($)(G) (End)          檔案結尾",
    "\01其他功\能鍵",
    "(/)                   搜尋字串",
    "(n/N)                 重複正/反向搜尋",
    "(TAB)                 URL連結",
    "(Ctrl-T)              存到暫存檔",
    "(:/f/b)               跳至某頁/下/上篇",
    "(F/B)                 跳至同一搜尋主題下/上篇",
    "(a/A)                 跳至同一作者下/上篇",
    "([/])                 主題式閱\讀 上/下",
    "(t)                   主題式循序閱\讀",
    "(Ctrl-C)              小計算機",
    "(q)(←)               結束",
    "(h)(H)(?)             輔助說明畫面",
    NULL
};

int beep = 0;

static void
more_goto (int fd, off_t off)
{ 
  int base = more_base;

  if (off < base || off >= base + more_size)
  { 
    more_base = base = off & (-MORE_WINSIZE);
    lseek (fd, base, SEEK_SET);
    more_size = read (fd, more_pool, MORE_BUFSIZE);
  }
  more_head = off - base;
}

static int
more_readln (int fd, unsigned char *buf)
{
  int ch;

  unsigned char *data, *tail, *cc;
  int len, bytes, in_ansi;
  int size, head, ansilen;

  len = bytes = in_ansi = ansilen = 0;
  tail = buf + ANSILINELEN - 1;
  size = more_size;
  head = more_head;
  data = &more_pool[head];

  do
  {
    if (head >= size)
    {
      more_base += size;
      data = more_pool;
      more_size = size = read (fd, data, MORE_BUFSIZE);
      if (size == 0)
        break;
      head = 0;
    }

    ch = *data++;
    head++;
    bytes++;
    if (ch == '\n')
    { 
      break;
    }
    if (ch == '\t')
    { 
      do
      { 
        *buf++ = ' ';
      }
      while ((++len & 7) && len < 80);
    }
    else if (ch == '\033')
    { 
      if (atoi ((char *)(data + 1)) > 47)
      { 
        if ((cc = (unsigned char *)strchr ((char *)(data + 1), 'm')) != NULL)
        { 
          ch = cc - data + 1;

          data += ch;
          head += ch;
          bytes += ch;
        }
      }
      else
      { 
        if (showansi)
          *buf++ = ch;
        in_ansi = 1;
      }
    }
    else if (in_ansi)
    {
      if (showansi)
        *buf++ = ch;
      if (!strchr (STR_ANSICODE, ch))
        in_ansi = 0;
    }
    else if (isprint2 (ch))
    { 
      len++;
      *buf++ = ch;
    }
  }
  while (len < 80 && buf < tail);
  *buf = '\0';
  more_head = head;
  return bytes;
}

/* not used 
static int readln(FILE *fp, char *buf) {
    register int ch, i, len, bytes, in_ansi;

    len = bytes = in_ansi = i = 0;
    while(len < 80 && i < ANSILINELEN && (ch = getc(fp)) != EOF) {
	bytes++;
	if(ch == '\n')
	    break;
	else if(ch == '\t')
	    do {
		buf[i++] = ' ';
	    } while((++len & 7) && len < 80);
	else if(ch == '\a')
	    beep = 1;
	else if(ch == '\033') {
	    if(showansi)
		buf[i++] = ch;
	    in_ansi = 1;
	} else if(in_ansi) {
	    if(showansi)
		buf[i++] = ch;
	    if(!strchr("[0123456789;,", ch))
		in_ansi = 0;
	} else if(isprint2(ch)) {
	    len++;
	    buf[i++] = ch;
	}
    }
    buf[i] = '\0';
    return bytes;
}
*/

static int more_web(char *fpath, int promptend);
int more(char *fpath, int promptend) {
    static char *head[4] = {"作者", "標題", "時間" ,"轉信"};
    char *ptr, *word = NULL, buf[ANSILINELEN + 1], *ch1;
    struct stat st;

/* rocker */
    //FILE *fp;
    int fd, fsize;

    unsigned int pagebreak[MAX_PAGES], pageno, lino = 0;
    int line, ch, viewed, pos, numbytes;
    int header = 0;
    int local = 0;
    char search_char0=0;
    static char search_str[81]="";
    typedef char* (*FPTR)();
    static FPTR fptr;
    int searching = 0;
    int scrollup = 0;
    char *printcolor[3]= {"44","33;45","0;34;46"}, color =0;
    char *http[80]={NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
		    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
		    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
		    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
		    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
		    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
		    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
		    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
    /* Ptt */
    char pagemode = 0;
    char pagecount = 0;

    memset(pagebreak, 0, sizeof(pagebreak));
    if(*search_str)
	search_char0 = *search_str;
    *search_str = 0;

#ifdef MDCACHE
    fd = mdcacheopen(fpath);
#else    
    fd = open (fpath, O_RDONLY, 0600); 
#endif
    if (fd < 0) return -1;
    
    if(fstat(fd, &st) || ((fsize = st.st_size) <= 0) || S_ISDIR (st.st_mode))
     {
        close(fd); //Ptt
	return -1;
     }
    pagebreak[0] = pageno = viewed = line = pos = 0;
    clear();

/* rocker */

   more_base = more_head = more_size = 0;
    
    while((numbytes = more_readln(fd, (unsigned char *)buf)) || (line == t_lines)) {
	if(scrollup) {
	    rscroll();
	    move(0, 0);
	}
	if(numbytes) {              /* 一般資料處理 */
	    if(!viewed) {             /* begin of file */
		if(showansi) {          /* header processing */
		    if(!strncmp(buf, str_author1, LEN_AUTHOR1)) {
			line = 3;
			word = buf + LEN_AUTHOR1;
			local = 1;
		    } else if(!strncmp(buf, str_author2, LEN_AUTHOR2)) {
			line = 4;
			word = buf + LEN_AUTHOR2;
		    }
		    
		    while(pos < line) {
			if(!pos && ((ptr = strstr(word, str_post1)) ||
				    (ptr = strstr(word, str_post2)))) {
			    ptr[-1] = '\0';
			    prints("\033[47;34m %s \033[44;37m%-53.53s"
				   "\033[47;34m %.4s \033[44;37m%-13s\033[m\n",
				   head[0], word, ptr, ptr + 5);
			} else if (pos < line)
			    prints("\033[47;34m %s \033[44;37m%-72.72s"
				   "\033[m\n", head[pos], word);
			
			viewed += numbytes;
			numbytes = more_readln(fd, (unsigned char *)buf);
			
			/* 第一行太長了 */			
			if(!pos && viewed > 79) {
                            /* 第二行不是 [標....] */
			    if(memcmp( buf, head[1], 2)) { 
				/* 讀下一行進來處理 */
				viewed += numbytes;
				numbytes = more_readln(fd, (unsigned char *)buf);
			    }
			}
			pos++;
		    }
		    if(pos) {
			header = 1;
			
			prints("\033[36m%s\033[m\n", msg_seperator);
			++line; ++pos;
		    }
		}
		lino = pos;
		word = NULL;
	    }

	    /* ※處理引用者 & 引言 */
	    if((buf[1] == ' ') && (buf[0] == ':' || buf[0] == '>'))
		word = "\033[36m";
	    else if(!strncmp(buf, "※", 2) || !strncmp(buf, "==>", 3))
		word = "\033[32m";
	    
	    ch1 = buf;
	    while(1) {
		int i;
		char e,*ch2;

		if((ch2 = strstr(ch1, "http://")))
		    ;
		else if((ch2 = strstr(ch1,"gopher://")))
		    ;
		else if((ch2 = strstr(ch1,"mailto:")))
		    ;
		else
		    break;
		for(e = 0; ch2[(int)e] != ' ' && ch2[(int)e] != '\n' &&
			ch2[(int)e] != '\0' && ch2[(int)e] != '"' &&
			ch2[(int)e] != ';' && ch2[(int)e] != ']'; e++);
		for(i = 0; http[i] && i < 80; i++)
		    if(!strncmp(http[i], ch2, e) && http[(int)e] == 0)
			break;
		if(!http[i]) {
		    http[i] = (char *)malloc(e + 1);
		    strncpy(http[i], ch2, e);
		    http[i][(int)e] = 0;
		    pagecount++;
                }
		ch1 = &ch2[7];
	    }
	    if(word)
		outs(word);
	    {
		char msg[500], *pos;
		
		if(*search_str && (pos = fptr(buf, search_str))) {
		    char SearchStr[81];
		    char buf1[100], *pos1;
		    
		    strncpy(SearchStr, pos, strlen(search_str));
		    SearchStr[strlen(search_str)] = 0;
		    searching = 0;
		    sprintf(msg, "%.*s\033[7m%s\033[m", pos - buf, buf,
			    SearchStr);
		    while((pos = fptr(pos1 = pos + strlen(search_str),
				      search_str))) {
			sprintf(buf1, "%.*s\033[7m%s\033[m", pos - pos1,
				pos1, SearchStr);
			strcat(msg, buf1);
		    }
		    strcat(msg, pos1);
		    outs(Ptt_prints(msg,NO_RELOAD));
		} else
		    outs(Ptt_prints(buf,NO_RELOAD));
	    }
	    if(word) {
		outs("\033[m");
		word = NULL;
	    }
	    outch('\n');
	    
	    if(beep) {
		bell();
		beep = 0;
	    }

	    if(line < b_lines)       /* 一般資料讀取 */
		line++;
	    
	    if(line == b_lines && searching == -1) {
		if(pageno > 0)
		    more_goto(fd, viewed = pagebreak[--pageno]);
		else
		    searching = 0;
		lino = pos = line = 0;
		clear();
		continue;
	    }
	    
	    if(scrollup) {
		move(line = b_lines, 0);
		clrtoeol();
		for(pos = 1; pos < b_lines; pos++)
		    viewed += more_readln(fd, (unsigned char *)buf);
	    } else if(pos == b_lines)  /* 捲動螢幕 */
		scroll();
	    else
		pos++;

	    if(!scrollup && ++lino >= b_lines && pageno < MAX_PAGES - 1) {
		pagebreak[++pageno] = viewed;
		lino = 1;
	    }

	    if(scrollup) {
		lino = scrollup;
		scrollup = 0;
	    }
	    viewed += numbytes;       /* 累計讀過資料 */
	} else
	    line = b_lines;           /* end of END */
	
	if(promptend &&
	   ((!searching && line == b_lines) || viewed == fsize)) {
	    /* Kaede 剛好 100% 時不停 */
	    move(b_lines, 0);
	    if(viewed == fsize) {
		if(searching == 1)
		    searching = 0;
		color = 0;
	    } else if(pageno == 1 && lino == 1) {
		if(searching == -1)
		    searching = 0;
		color = 1;
	    } else
		color = 2;

	    prints("\033[m\033[%sm  瀏覽 P.%d(%d%%)  %s  %-30.30s%s",
		   printcolor[(int)color], 
		   pageno, 
		   (int)((viewed * 100) / fsize),
		   pagemode ? "\033[30;47m" : "\033[31;47m",
		   pagemode ? http[pagemode-1] : "(h)\033[30m求助  \033[31m→↓[PgUp][",
		   pagemode ? "\033[31m[TAB]\033[30m切換 \033[31m[Enter]\033[30m選定 \033[31m←\033[30m放棄\033[m" : "PgDn][Home][End]\033[30m游標移動  \033[31m←[q]\033[30m結束   \033[m");
	    
	    
	    while(line == b_lines || (line > 0 && viewed == fsize)) {
		switch((ch = egetch())) {
		case ':':
		{
		    char buf[10];
		    int i = 0;
		    
		    getdata(b_lines - 1, 0, "Goto Page: ", buf, 5, DOECHO);
		    sscanf(buf, "%d", &i);
		    if(0 < i && i <  MAX_PAGES && (i == 1 || pagebreak[i - 1]))
			pageno = i - 1;
		    else if(pageno)
			pageno--;
		    lino = line = 0;
		    break;
		}
		case '/':
		{
		    char ans[4] = "n";
		    
		    *search_str = search_char0;
		    getdata_buf(b_lines - 1, 0,"[搜尋]關鍵字:", search_str,
				40, DOECHO);
		    if(*search_str) {
			searching = 1;
			if(getdata(b_lines - 1, 0, "區分大小寫(Y/N/Q)? [N] ",
				   ans, sizeof(ans), LCECHO) && *ans == 'y')
			    fptr = strstr;
			else
			    fptr = strcasestr;
		    }
		    if(*ans == 'q')
			searching = 0;
		    if(pageno)
			pageno--;
		    lino = line = 0;
		    break;
		}
		case 'n':
		    if(*search_str) {
			searching = 1;
			if(pageno)
			    pageno--;
			lino = line = 0;
		    }
		    break;
		case 'N':
		    if(*search_str) {
			searching = -1;
			if(pageno)
			    pageno--;
			lino = line = 0;
		    }
		    break;
		case 'r':
		case 'R':
		case 'Y':
		    close(fd);
		    return 7;
		case 'y':
		    close(fd);
		    return 8;
		case 'A':
		    close(fd);
		    return 9;
		case 'a':
		    close(fd);
		    return 10;
		case 'F':
		    close(fd);
		    return 11;
		case 'B':
		    close(fd);
		    return 12;
		case KEY_LEFT:
		    if(pagemode) {
			pagemode = 0;
			*search_str = 0;
			if(pageno)
			    pageno--;
			lino = line = 0;
			break;
		    }
		    close(fd);
		    return 6;
		case 'q':
		    close(fd);
		    return 0;
		case 'b':
		    close(fd);
		    return 1;
		case 'f':
		    close(fd);
		    return 3;
		case ']':       /* Kaede 為了主題閱讀方便 */
		    close(fd);
		    return 4;
		case '[':       /* Kaede 為了主題閱讀方便 */
		    close(fd);
		    return 2;
		case '=':       /* Kaede 為了主題閱讀方便 */
		    close(fd);
		    return 5;
		case Ctrl('F'):
		case KEY_PGDN:
		    line = 1;
		    break;
		case 't':
		    if(viewed == fsize) {
			close(fd);
			return 4;
		    }
		    line = 1;
		    break;
		case ' ':
		    if(viewed == fsize) {
			close(fd);
			return 3;
		    }
		    line = 1;
		    break;
		case KEY_RIGHT:
		    if(viewed == fsize) {
			close(fd);
			return 0;
		    }
		    line = 1;
		    break;
		case '\r':
		case '\n':
		    if(pagemode) {
			more_web(http[pagemode-1],YEA);
			pagemode = 0;
			*search_str = 0;
			if(pageno)
			    pageno--;
			lino = line = 0;
			break;
		    }
		case KEY_DOWN:
		    if(viewed == fsize ||
		       (promptend == 2 && (ch == '\r' || ch == '\n'))) {
			close(fd);
			return 3;
		    }
		    line = t_lines - 2;
		    break;
		case '$':
		case 'G':
		case KEY_END:
		    line = t_lines;
		    break;
		case '0':
		case 'g':
		case KEY_HOME:
		    pageno = line = 0;
		    break;
		case 'h':
		case 'H':
		case '?':
		    /* Kaede Buggy ... */
		    show_help(more_help);
		    if(pageno)
			pageno--;
		    lino = line = 0;
		    break;
		case 'E':
		    if(HAS_PERM(PERM_SYSOP) && strcmp(fpath, "etc/ve.hlp")) {
			close(fd);
			vedit(fpath, NA, NULL);
			return 0;
		    }
		    break;
		case Ctrl('C'):
		    cal();
		    if(pageno)
			pageno--;
		    lino = line = 0;
		    break;

		case Ctrl('T'):
		    getdata(b_lines - 2, 0, "把這篇文章收入到暫存檔？[y/N] ",
			    buf, 4, LCECHO);
		    if(buf[0] == 'y') {
			char tmpbuf[128];
			
			setuserfile(tmpbuf, ask_tmpbuf(b_lines - 1));
			sprintf(buf, "cp -f %s %s", fpath, tmpbuf);
			system(buf);
		    }
		    if(pageno)
			pageno--;
		    lino = line = 0;
		    break;
#if 0
		case Ctrl('I'):
		    if(!pagecount)
			break;
		    pagemode = (pagemode % pagecount) + 1;
		    strncpy(search_str,http[pagemode-1],80);
		    search_str[80] =0;
		    fptr = strstr;
		    if(pageno)
			pageno--;
		    lino = line = 0;
		    break;
#endif
		case KEY_UP:
		    line = -1;
		    break;
		case Ctrl('B'):
		case KEY_PGUP:
		    if(pageno > 1) {
			if(lino < 2)
			    pageno -= 2;
			else
			    pageno--;
			lino = line = 0;
		    } else if(pageno && lino > 1)
			pageno = line = 0;
		    break;
		case Ctrl('H'):
		    if(pageno > 1) {
			if(lino < 2)
			    pageno -= 2;
			else
			    pageno--;
			lino = line = 0;
		    } else if(pageno && lino > 1)
			pageno = line = 0;
		    else {
			close(fd);
			return 1;
		    }
		}
	    }
	    
	    if(line > 0) {
		move(b_lines, 0);
		clrtoeol();
		refresh();
	    } else if(line < 0) {                      /* Line scroll up */
		if(pageno <= 1) {
		    if(lino == 1 || !pageno) {
			close(fd);
			return 1;
		    }
		    if(header && lino <= 5) {
			more_goto(fd, viewed = pagebreak[scrollup = lino =
						    pageno = 0] = 0);
			clear();
		    }
		}
		if(pageno && lino > 1 + local) {
		    line =  (lino - 2) - local;
		    if(pageno > 1 && viewed == fsize)
			line += local;
		    scrollup = lino - 1;
		    more_goto(fd, viewed = pagebreak[pageno - 1]);
		    while(line--)
			viewed += more_readln(fd, (unsigned char *)buf);
		} else if(pageno > 1) {
		    scrollup = b_lines - 1;
		    line = (b_lines - 2) - local;
		    more_goto(fd, viewed = pagebreak[--pageno - 1]);
		    while(line--)
			viewed += more_readln(fd, (unsigned char *)buf);
		}
		line = pos = 0;
	    } else {
		pos = 0;
		more_goto (fd, viewed = pagebreak[pageno]);
		move(0,0);
		clear();
	    }
	}
    }

    close(fd);
    if(promptend) {
	pressanykey();
	clear();
    } else
	outs(reset_color);
    return 0;
}

static int more_web(char *fpath, int promptend) {
    char *ch, *ch1 = NULL;
    char *hostname = fpath,userfile[MAXPATHLEN],file[MAXPATHLEN]="/";
    char genbuf[200];
    time_t dtime;
#if !defined(USE_LYNX) && defined(USE_PROXY)
    int a;
    FILE *fp;
    struct hostent *h;
    struct sockaddr_in sin;
#endif

    if((ch = strstr(fpath, "mailto:"))) {
        if(!HAS_PERM(PERM_LOGINOK)) {
	    move(b_lines - 1,0);
	    outs("\033[41m 您的權限不足無法使用internet mail... \033[m");
	    refresh();
	    return 0;
	}
        if(!invalidaddr(&ch[7]) &&
	   getdata(b_lines - 1, 0, "[寄信]主題：", genbuf, 40, DOECHO))
	    do_send(&ch[7], genbuf);
	else {
	    move(b_lines - 1,0);
	    outs("\033[41m 收信人email 或 標題 有誤... \033[m");
	    refresh();
	}
        return 0;
    }
    if((ch = strstr(fpath, "gopher://"))) {
	item_t item;

	strcpy(item.X.G.server, &ch[9]);
	strcpy(item.X.G.path, "1/");
	item.X.G.port = 70;
	gem(fpath , &item, 0);
        return 0;
    }
    if((ch = strstr(fpath, "http://")))
	hostname=&ch[7];
    if((ch = strchr(hostname, '/'))) {
        *ch = 0;
        if(&ch1[1])
	    strcat(file,&ch[1]);
    }
    if(file[strlen(file) - 1] == '/')
	strcat(file,"index.html");
    move(b_lines-1,0);
    clrtoeol();
#ifdef USE_PROXY
    sprintf(genbuf, "\033[33;44m 正在連往%s.(proxy:%s).....請稍候....\033[m",
	    hostname, PROXYSERVER);
#else
    sprintf(genbuf, "\033[33;44m 正在連往%s......請稍候....\033[m", hostname);
#endif
    outs(genbuf);
    refresh();

#ifdef LOCAL_PROXY
/* 先找 local disk 的 proxy */
    dtime=now;
    sprintf(userfile,"hproxy/%s%s",hostname,file);
    if(dashf(userfile) && (dtime - dasht(userfile)) <  HPROXYDAY * 24 * 60
       && more(userfile,promptend)) {
        return 1;
    }
    ch=userfile - 1;
    while((ch1 = strchr(ch + 1,'/'))) {
        *ch1 = 0;
        if(!dashd(ch))
	    mkdir(ch+1,0755);
        chdir(ch+1);
        *ch1 = '/';
        ch = ch1;
    }
    chdir(BBSHOME);
#endif

#ifndef USE_LYNX
#ifdef USE_PROXY
    if(!(h = gethostbyname(PROXYSERVER))) {
	outs("\033[33;44m 找不到這個proxy server!..\033[m");
	refresh();
	return;
    }
    ()memset((char *)&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;

    if(h == NULL)
	sin.sin_addr.s_addr = inet_addr(PROXYSERVER);
    else
	()memcpy(&sin.sin_addr.s_addr, h->h_addr, h->h_length);

    sin.sin_port = htons((ushort)PROXYPORT);  /* HTTP port */
    a = socket(AF_INET, SOCK_STREAM, 0);
    if((connect(a, (struct sockaddr *) & sin, sizeof sin)) < 0) {
	outs("\033[1;44m 連接到proxy受到拒絕 ! \033[m");
	refresh();
	return;
    }
    sprintf(genbuf,"GET http://%s/%s HTTP/1.1\n",hostname,file);
#else
    if(!(h = gethostbyname(hostname))) {
	outs("\033[33;44m 找不到這個server!..\033[m");
	refresh();
	return;
    }
    ()memset((char *) &sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    
    if(h == NULL)
	sin.sin_addr.s_addr = inet_addr(hostname);
    else
	()memcpy(&sin.sin_addr.s_addr, h->h_addr, h->h_length);
    
    sin.sin_port =  htons((ushort)80);  
    a = socket(AF_INET, SOCK_STREAM, 0);
    if((connect(a, (struct sockaddr *) & sin, sizeof sin)) < 0) {
	outs("\033[1;44m 連接受到拒絕 ! \033[m");
	refresh();
	return;
    }
    sprintf(genbuf, "GET %s\n", file);
#endif

    for(i = strlen(file); file[i - 1] != '/' && i > 0 ; i--);
    file[i] = 0;

    i = strlen(genbuf);
    write(a, genbuf, i);

#define BLANK   001
#define ISPRINT 002
#define PRE     004
#define CENTER  010
    if((fp = fopen(userfile,"w"))) {
	int flag = 2, c;
	char path[MAXPATHLEN];
	unsigned char j, k;

	while((i = read(a,genbuf,200))) {
	    if(i < 0)
		return;
	    genbuf[i]=0;
	    
	    for(j = 0, k = 0; genbuf[j] && j < i; j++) {
		if((flag & ISPRINT) && genbuf[j] == '<')
		    flag |= BLANK;
		else if((flag & ISPRINT) && genbuf[j] == '>')
		    flag &= ~BLANK;
		else {
		    if(!(flag & BLANK)) {
			if(j != k && (genbuf[j] != '\n' || flag & PRE))
			    genbuf[k++] = genbuf[j];
		    } else {
			switch(char_lower(genbuf[j])) {
			case 'a':
			    break;
			case 'b':
			    if(genbuf[j + 1] == 'r' && genbuf[j + 2] == '>') 
				genbuf[k++] = '\n';
			    break;
			case 'h':
			    if(genbuf[j + 1] == 'r' && 
			       (genbuf[j + 2] == '>' ||
				genbuf[j + 2] == 's')) {
				strncpy(&genbuf[k], "\n--\n", 4);
				k += 4;
			    }
			    break;
			case 'l':
			    if(genbuf[j + 1] == 'i' && genbuf[j + 2]=='>') {
				strncpy(&genbuf[k], "\n◎ ", 4);
				k += 4;
			    }
			    break;
			case 'p':
			    if(genbuf[j + 1]=='>') {
				genbuf[k++] = '\n';
				genbuf[k++] = '\n';
			    } else if(genbuf[j + 1] == 'r' &&
				      genbuf[j + 2] == 'e')
				flag ^= PRE;
			    break;
			case 't':
			    if(genbuf[j + 1] == 'd' && genbuf[j + 2]=='>') {
				strncpy(&genbuf[k], "\n-\n", 3);
				k += 3;
			    }
			    break;
			}
		    }
		    if((genbuf[j] & 0x80) && (flag & ISPRINT))
			flag &= ~ISPRINT;
		    else
			flag |= ISPRINT;
                }
	    }
	    genbuf[k]=0;
	    fputs(genbuf, fp);
	}
	fclose(fp);
	close(a);
	return more(userfile, promptend);
    }
    return 0;
#else  /* use lynx dump */
    sprintf(genbuf, "lynx -dump http://%s%s > %s", hostname, file, userfile);
    system(genbuf);
    return more(userfile, promptend);
#endif
}
