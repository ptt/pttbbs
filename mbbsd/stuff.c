/* $Id$ */
#include "bbs.h"

// TODO remove this 
/* ----------------------------------------------------- */
/* set file path for boards/user home                    */
/* ----------------------------------------------------- */
static const char * const str_home_file = "home/%c/%s/%s";
static const char * const str_board_file = "boards/%c/%s/%s";

static const char * const str_dotdir = FN_DIR;

/* XXX set*() all assume buffer size = PATHLEN */

void
setuserfile(char *buf, const char *fname)
{
    assert(is_validuserid(cuser.userid));
    assert(fname[0]);
    snprintf(buf, PATHLEN, str_home_file, cuser.userid[0], cuser.userid, fname);
}

void
setbdir(char *buf, const char *boardname)
{
    //assert(boardname[0]);
    snprintf(buf, PATHLEN, str_board_file, boardname[0], boardname,
	    (currmode & MODE_DIGEST ? fn_mandex : str_dotdir));
}

/**
 * 給定文章標題 title，傳回指到主題的部分的指標。
 * @param title
 */
char           *
subject(char *title)
{
    if (!strncasecmp(title, str_reply, 3)) {
	title += 3;
	if (*title == ' ')
	    title++;
    }
    return title;
}

int is_validuserid(const char *id)
{
    int len, i;
    if(id==NULL)
	return 0;
    len = strlen(id);

    if (len < 2 || len>IDLEN)
	return 0;

    if (!isalpha(id[0]))
	return 0;
    for (i = 1; i < len; i++)
	if (!isalnum(id[i]))
	    return 0;
    return 1;
}

int
is_uBM(const char *list, const char *id)
{
    register int    len;

    if (list[0] == '[')
	list++;
    if (list[0] > ' ') {
	len = strlen(id);
	do {
	    if (!strncasecmp(list, id, len)) {
		list += len;
		if ((*list == 0) || (*list == '/') ||
		    (*list == ']') || (*list == ' '))
		    return 1;
	    }
	    if ((list = strchr(list, '/')) != NULL)
		list++;
	    else
		break;
	} while (1);
    }
    return 0;
}

int
is_BM(const char *list)
{
    if (is_uBM(list, cuser.userid)) {
	cuser.userlevel |= PERM_BM;	/* Ptt 自動加上BM的權利 */
	return 1;
    }
    return 0;
}

int
userid_is_BM(const char *userid, const char *list)
{
    register int    ch, len;

    // TODO merge with is_uBM
    ch = list[0];
    if ((ch > ' ') && (ch < 128)) {
	len = strlen(userid);
	do {
	    if (!strncasecmp(list, userid, len)) {
		ch = list[len];
		if ((ch == 0) || (ch == '/') || (ch == ']'))
		    return 1;
	    }
	    while ((ch = *list++)) {
		if (ch == '/')
		    break;
	    }
	} while (ch);
    }
    return 0;
}


time4_t
gettime(int line, time4_t dt, const char*head)
{
    char            yn[7];
    int i;
    struct tm      *ptime = localtime4(&dt), endtime;
    time_t          t;

    memcpy(&endtime, ptime, sizeof(struct tm));
    snprintf(yn, sizeof(yn), "%4d", ptime->tm_year + 1900);
    move(line, 0); outs(head);
    i=strlen(head);
    do {
	getdata_buf(line, i, " 西元年:", yn, 5, LCECHO);
	// signed:   limited on (2037, ...)
	// unsigned: limited on (..., 1970)
	// let's restrict inside the boundary.
    } while ((endtime.tm_year = atoi(yn) - 1900) < 70 || endtime.tm_year > 135);
    snprintf(yn, sizeof(yn), "%d", ptime->tm_mon + 1);
    do {
	getdata_buf(line, i+15, "月:", yn, 3, LCECHO);
    } while ((endtime.tm_mon = atoi(yn) - 1) < 0 || endtime.tm_mon > 11);
    snprintf(yn, sizeof(yn), "%d", ptime->tm_mday);
    do {
	getdata_buf(line, i+24, "日:", yn, 3, LCECHO);
    } while ((endtime.tm_mday = atoi(yn)) < 1 || endtime.tm_mday > 31);
    snprintf(yn, sizeof(yn), "%d", ptime->tm_hour);
    do {
	getdata_buf(line, i+33, "時(0-23):", yn, 3, LCECHO);
    } while ((endtime.tm_hour = atoi(yn)) < 0 || endtime.tm_hour > 23);
    t = mktime(&endtime);
    /* saturation check */
    if(t < 0)
      t = 1;
    if(t > INT_MAX)
      t = INT_MAX;
    return t;
}

// synchronize 'now'
void syncnow(void)
{
#ifdef OUTTA_TIMER
        now = SHM->GV2.e.now;
#else
	now = time(0);
#endif
}

#ifdef PLAY_ANGEL
void
pressanykey_or_callangel(){
    int             ch;

    outmsg(
	    ANSI_COLOR(1;34;44) " ▄▄▄▄ " 
	    ANSI_COLOR(32) "H " ANSI_COLOR(36) "呼叫小天使" ANSI_COLOR(34) 
	    " ▄▄▄▄" ANSI_COLOR(37;44) " 請按 " ANSI_COLOR(36) "空白鍵 " 
	    ANSI_COLOR(37) "繼續 " ANSI_COLOR(1;34) 
	    "▄▄▄▄▄▄▄▄▄▄▄▄▄▄ " ANSI_RESET);
    do {
	ch = igetch();
	if (ch == 'h' || ch == 'H'){
	    CallAngel();
	    break;
	}
    } while ((ch != ' ') && (ch != KEY_LEFT) && (ch != '\r') && (ch != '\n'));
    move(b_lines, 0);
    clrtoeol();
    refresh();
}
#endif

/**
 * 給 printf format 的參數，印到最底下一行。
 * 傳回使用者的選擇(char)。
 */
char
getans(const char *fmt,...)
{
    char   msg[256];
    char   ans[3];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg , sizeof(msg), fmt, ap);
    va_end(ap);

    getdata(b_lines, 0, msg, ans, sizeof(ans), LCECHO);
    return ans[0];
}

int
getkey(const char *fmt,...)
{
    char   msg[256], i;
    va_list ap;
    va_start(ap, fmt);
    i = vsnprintf(msg , sizeof(msg), fmt, ap);
    va_end(ap);
    return vmsg(msg);
}

static const char *msg_pressanykey_full =
    ANSI_COLOR(37;44) " 請按" ANSI_COLOR(36) " 任意鍵 " ANSI_COLOR(37) "繼續 " ANSI_COLOR(34);
#define msg_pressanykey_full_len (18)

    // what is 200/1431/506/201?
static const char* msg_pressanykey_trail =
    ANSI_COLOR(33;46) " [按任意鍵繼續] " ANSI_RESET;
#define msg_pressanykey_trail_len (16+1+4) /* 4 for head */

int
vmsg(const char *msg)
{
    int len = msg ? strlen(msg) : 0;
    int i = 0;

    if(len == 0) msg = NULL;

    move(b_lines, 0);
    clrtoeol();

    if(!msg)
    {
	/* msg_pressanykey_full */ 
	int w = (t_columns - msg_pressanykey_full_len - 8) / 2;
	int pad = 0;

	outs(ANSI_COLOR(1;34;44) " ");
	pad += 1;
	for (i = 0; i < w; i += 2)
	    outs("▄"), pad+=2;
	outs(msg_pressanykey_full), pad+= msg_pressanykey_full_len;
	/* pad now points to position of current cursor. */
	pad = t_columns - pad -2 ;
	/* pad is now those left . */
	if (pad > 0)
	{
	    for (i = 0; i <= pad-2; i += 2)
		outs("▄");
	    if (i == pad-1)
		outs(" ");
	}
	outs(ANSI_RESET);
    } else {
	/* msg_pressanykey_trail */ 
	outs(ANSI_COLOR(1;36;44) " ◆ ");
	if(len >= t_columns - msg_pressanykey_trail_len)
	    len = t_columns - msg_pressanykey_trail_len;
	while (i++ < len)
	    outc(*msg++);
	i--;
	while (i++ < t_columns - msg_pressanykey_trail_len)
	    outc(' ');
	outs(msg_pressanykey_trail);
    }

    do {
	i = igetch();
    } while( i == 0 );

    move(b_lines, 0);
    clrtoeol();
    return i;
}

int
vmsgf(const char *fmt,...)
{
    char   msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    msg[sizeof(msg)-1] = 0;
    return vmsg(msg);
}

/**
 * 從第 y 列開始 show 出 filename 檔案中的前 lines 行。
 * mode 為 output 的模式，參數同 strip_ansi。
 * @param filename
 * @param x
 * @param lines
 * @param mode: SHOWFILE_*, see modes.h
 * @return 失敗傳回 0，否則為 1。 
 *         2 表示有 PttPrints 碼
 */
int
show_file(const char *filename, int y, int lines, int mode)
{
    FILE *fp;
    char buf[1024];
    int  ret = 1;
    int  strpmode = STRIP_ALL; 

    if (mode & SHOWFILE_ALLOW_COLOR)
	strpmode = ONLY_COLOR;
    if (mode & SHOWFILE_ALLOW_MOVE)
	strpmode = NO_RELOAD;

    if (y >= 0)
	move(y, 0);
    clrtoln(lines + y);
    if ((fp = fopen(filename, "r"))) {
	while (fgets(buf, sizeof(buf), fp) && lines--)
	{
	    move(y++, 0);
	    if (mode == SHOWFILE_RAW) 
	    {
		outs(buf);
	    }
	    else if ((mode & SHOWFILE_ALLOW_STAR) && (strstr(buf, ESC_STR "*") != NULL))
	    {
		// because Ptt_prints escapes are not so often,
		// let's try harder to detect it.
		outs(Ptt_prints(buf, sizeof(buf), strpmode));
		ret = 2;
	    } else {
		// ESC is very common...
		strip_ansi(buf, buf, strpmode);
		outs(buf);
	    }
	}
	fclose(fp);
	outs(ANSI_RESET); // prevent some broken Welcome file
    } else
	return 0;
    return ret;
}

void
bell(void)
{
    char            c;

    c = Ctrl('G');
    write(1, &c, 1);
}

int
search_num(int ch, int max)
{
    int  clen = 1, y = b_lines - msg_occupied;
    char genbuf[10];

    genbuf[0] = ch; genbuf[1] = 0;
    clen = getdata_buf(y, 0, 
	    " 跳至第幾項: ", genbuf, sizeof(genbuf)-1, NUMECHO);

    move(y, 0); clrtoeol();
    genbuf[clen] = '\0';
    if (genbuf[0] == '\0')
	return -1;
    clen = atoi(genbuf);
    if (clen == 0)
	return 0;
    if (clen > max)
	return max;
    return clen - 1;
}

/**
 * 在螢幕左上角 show 出 "【title】"
 * @param title
 */
void
stand_title(const char *title)
{
    clear();
    prints(ANSI_COLOR(1;37;46) "【 %s 】" ANSI_RESET "\n", title);
}

void
cursor_show(int row, int column)
{
    move(row, column);
    outs(STR_CURSOR);
    move(row, column + 1);
}

void
cursor_clear(int row, int column)
{
    move(row, column);
    outs(STR_UNCUR);
}

int
cursor_key(int row, int column)
{
    int             ch;

    cursor_show(row, column);
    ch = igetch();
    move(row, column);
    outs(STR_UNCUR);
    return ch;
}

void
printdash(const char *mesg, int msglen)
{
    int             head = 0, tail;

    if(msglen <= 0)
	msglen = strlen(mesg);

    if (mesg)
	head = (msglen + 1) >> 1;

    tail = head;

    while (head++ < t_columns/2-2)
	outc('-');

    if (tail) {
	outc(' ');
	if(mesg) outs(mesg);
	outc(' ');
    }
    while (tail++ < t_columns/2-2)
	outc('-');

    outc('\n');
}

int
log_user(const char *fmt, ...)
{
    char msg[256], filename[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg , sizeof(msg), fmt, ap);
    va_end(ap);

    sethomefile(filename, cuser.userid, "USERLOG");
    return log_filef(filename, LOG_CREAT, "%s: %s %s", cuser.userid, msg,  Cdate(&now));
}

void
show_help(const char * const helptext[])
{
    const char     *str;
    int             i;

    clear();
    for (i = 0; (str = helptext[i]); i++) {
	if (*str == '\0')
	    prints(ANSI_COLOR(1) "【 %s 】" ANSI_COLOR(0) "\n", str + 1);
	else if (*str == '\01')
	    prints("\n" ANSI_COLOR(36) "【 %s 】" ANSI_RESET "\n", str + 1);
	else
	    prints("        %s\n", str);
    }
#ifdef PLAY_ANGEL
    if (HasUserPerm(PERM_LOGINOK))
	pressanykey_or_callangel();
    else
#endif
	pressanykey();
}

void
show_helpfile(const char *helpfile)
{
    clear();
    show_file((char *)helpfile, 0, b_lines, SHOWFILE_ALLOW_ALL);
#ifdef PLAY_ANGEL
    if (HasUserPerm(PERM_LOGINOK))
	pressanykey_or_callangel();
    else
#endif
    pressanykey();
}

/* ----------------------------------------------------- */
/* use mmap() to malloc large memory in CRITICAL_MEMORY  */
/* ----------------------------------------------------- */
#ifdef CRITICAL_MEMORY
void *MALLOC(int size)
{
    int     *p;
    p = (int *)mmap(NULL, (size + 4), PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_PRIVATE, -1, 0);
    p[0] = size;
#if defined(DEBUG)
    vmsgf("critical malloc %d bytes", size);
#endif
    return (void *)&p[1];
}

void FREE(void *ptr)
{
    int     size = ((int *)ptr)[-1];
    munmap((void *)(&(((int *)ptr)[-1])), size);
#if defined(DEBUG)
    vmsgf("critical free %d bytes", size);
#endif
}
#endif


unsigned
DBCS_StringHash(const char *s)
{
    return fnv1a_32_dbcs_strcase(s, FNV1_32_INIT);
}

inline int *intbsearch(int key, const int *base0, int nmemb)
{
    /* 改自 /usr/src/lib/libc/stdlib/bsearch.c ,
       專給搜 int array 用的, 不透過 compar function 故較快些 */
    const   char *base = (const char *)base0;
    size_t  lim;
    int     *p;

    for (lim = nmemb; lim != 0; lim >>= 1) {
	p = (int *)(base + (lim >> 1) * 4);
	if( key == *p )
	    return p;
	if( key > *p ){/* key > p: move right */
	    base = (char *)p + 4;
	    lim--;
	}               /* else move left */
    }
    return (NULL);
}

inline unsigned int *
uintbsearch(const unsigned int key, const unsigned int *base0, const int nmemb)
{
    /* 改自 /usr/src/lib/libc/stdlib/bsearch.c ,
       專給搜 int array 用的, 不透過 compar function 故較快些 */
    const   char *base = (const char *)base0;
    size_t  lim;
    unsigned int     *p;

    for (lim = nmemb; lim != 0; lim >>= 1) {
        p = (unsigned int *)(base + (lim >> 1) * 4);
        if( key == *p )
            return p;
        if( key > *p ){/* key > p: move right */
            base = (char *)p + 4;
            lim--;
        }               /* else move left */
    }
    return (NULL);
}

/* AIDS */
aidu_t fn2aidu(char *fn)
{
  aidu_t aidu = 0;
  aidu_t type = 0;
  aidu_t v1 = 0;
  aidu_t v2 = 0;
  char *sp = fn;

  if(fn == NULL)
    return 0;

  switch(*(sp ++))
  {
    case 'M':
      type = 0;
      break;
    case 'G':
      type = 1;
      break;
    default:
      return 0;
      break;
  }

  if(*(sp ++) != '.')
    return 0;
  v1 = strtoul(sp, &sp, 10);
  if(sp == NULL)
    return 0;
  if(*sp != '.' || *(sp + 1) != 'A')
    return 0;
  sp += 2;
  if(*(sp ++) == '.')
  {
    v2 = strtoul(sp, &sp, 16);
    if(sp == NULL)
      return 0;
  }
  aidu = ((type & 0xf) << 44) | ((v1 & 0xffffffff) << 12) | (v2 & 0xfff);

  return aidu;
}

/* IMPORTANT:
 *   size of buf must be at least 8+1 bytes
 */
char *aidu2aidc(char *buf, aidu_t aidu)
{
  const char aidu2aidc_table[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_";
  const int aidu2aidc_tablesize = sizeof(aidu2aidc_table) - 1;
  char *sp = buf + 8;
  aidu_t v;

  *(sp --) = '\0';
  while(sp >= buf)
  {
    /* FIXME: 能保證 aidu2aidc_tablesize 是 2 的冪次的話，
              這裡可以改用 bitwise operation 做 */
    v = aidu % aidu2aidc_tablesize;
    aidu = aidu / aidu2aidc_tablesize;
    *(sp --) = aidu2aidc_table[v];
  }
  return buf;
}

/* IMPORTANT:
 *   size of fn must be at least FNLEN bytes
 */
char *aidu2fn(char *fn, aidu_t aidu)
{
  aidu_t type = ((aidu >> 44) & 0xf);
  aidu_t v1 = ((aidu >> 12) & 0xffffffff);
  aidu_t v2 = (aidu & 0xfff);

  if(fn == NULL)
    return NULL;
  snprintf(fn, FNLEN - 1, "%c.%d.A.%03X", ((type == 0) ? 'M' : 'G'), (unsigned int)v1, (unsigned int)v2);
  fn[FNLEN - 1] = '\0';
  return fn;
}

aidu_t aidc2aidu(char *aidc)
{
  char *sp = aidc;
  aidu_t aidu = 0;

  if(aidc == NULL)
    return 0;

  while(*sp != '\0' && /* ignore trailing spaces */ *sp != ' ')
  {
    aidu_t v = 0;
    /* FIXME: 查表法會不會比較快？ */
    if(*sp >= '0' && *sp <= '9')
      v = *sp - '0';
    else if(*sp >= 'A' && *sp <= 'Z')
      v = *sp - 'A' + 10;
    else if(*sp >= 'a' && *sp <= 'z')
      v = *sp - 'a' + 36;
    else if(*sp == '-')
      v = 62;
    else if(*sp == '_')
      v = 63;
    else
      return 0;
    aidu <<= 6;
    aidu |= (v & 0x3f);
    sp ++;
  }

  return aidu;
}

int search_aidu(char *bfile, aidu_t aidu)
{
  char fn[FNLEN];
  int fd;
  fileheader_t fhs[64];
  int len, i;
  int pos = 0;
  int found = 0;
  int lastpos = 0;

  if(aidu2fn(fn, aidu) == NULL)
    return -1;
  if((fd = open(bfile, O_RDONLY, 0)) < 0)
    return -1;

  while(!found && (len = read(fd, fhs, sizeof(fhs))) > 0)
  {
    len /= sizeof(fileheader_t);
    for(i = 0; i < len; i ++)
    {
      int l;
      if(strcmp(fhs[i].filename, fn) == 0 ||
         ((aidu & 0xfff) == 0 && (l = strlen(fhs[i].filename)) > 6 &&
          strncmp(fhs[i].filename, fn, l) == 0))
      {
        if(fhs[i].filemode & FILE_BOTTOM)
        {
          lastpos = pos;
        }
        else
        {
          found = 1;
          break;
        }
      }
      pos ++;
    }
  }
  close(fd);

  return (found ? pos : (lastpos ? lastpos : -1));
}
/* end of AIDS */
