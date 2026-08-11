#include "bbs.h"

// TODO remove this
/* ----------------------------------------------------- */
/* set file path for boards/user home                    */
/* ----------------------------------------------------- */
static const char * const str_dotdir = FN_DIR;

/* XXX set*() all assume buffer size = PATHLEN */

void
setuserfile(char *buf, const char *fname)
{
    sethomefile(buf, cuser.userid, fname);
}

void
setbdir(char *buf, const char *boardname)
{
    setbfile(buf, boardname, (currmode & MODE_DIGEST ? fn_mandex : str_dotdir));
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

time4_t
gettime(int line, time4_t dt, const char* head)
{
    char            yn[7];
    struct tm      ptime, endtime;
    time_t          t;
    char prompt[STRLEN*2];

    localtime4_r(&dt, &ptime);
    endtime = ptime;
    SNPRINTF(yn, "%4d", ptime.tm_year + 1900);
    move(line, 0); SOLVE_ANSI_CACHE(); clrtoeol();
    SNPRINTF(prompt, "%s 西元年:", head);
    do {
	getdata_buf(line, 0, prompt, yn, 5, NUMECHO);
	// signed:   limited on (2037, ...)
	// unsigned: limited on (..., 1970)
	// let's restrict inside the boundary.
    } while ((endtime.tm_year = atoi(yn) - 1900) < 70 || endtime.tm_year > 135);
    STRLCAT(prompt, yn);
    STRLCAT(prompt, " 月:");
    SNPRINTF(yn, "%d", ptime.tm_mon + 1);
    do {
	getdata_buf(line, 0, prompt, yn, 3, NUMECHO);
    } while ((endtime.tm_mon = atoi(yn) - 1) < 0 || endtime.tm_mon > 11);
    STRLCAT(prompt, yn);
    STRLCAT(prompt, " 日:");
    SNPRINTF(yn, "%d", ptime.tm_mday);
    do {
	getdata_buf(line, 0, prompt, yn, 3, NUMECHO);
    } while ((endtime.tm_mday = atoi(yn)) < 1 || endtime.tm_mday > 31);
    SNPRINTF(yn, "%d", ptime.tm_hour);
    STRLCAT(prompt, yn);
    STRLCAT(prompt, " 時(0-23):");
    do {
	getdata_buf(line, 0, prompt, yn, 3, NUMECHO);
    } while ((endtime.tm_hour = atoi(yn)) < 0 || endtime.tm_hour > 23);
    STRLCAT(prompt, yn);
    STRLCAT(prompt, " 分(0-59):");
    SNPRINTF(yn, "%d", ptime.tm_min);
    do {
	getdata_buf(line, 0, prompt, yn, 3, NUMECHO);
    } while ((endtime.tm_min = atoi(yn)) < 0 || endtime.tm_min > 59);
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
	now = time(0);
}

void
wait_penalty(int sec)
{
    static time4_t lastWait = 0;

    syncnow();
    int diff = (int)time4_diff(now, lastWait);
    if (diff < sec)
    {
        sec = diff;
        if (sec < 0 || sec >= 5)
            sec = 5;
        sleep(sec);
        vkey_purge();
    }
    lastWait = now;
}

// TODO
// move this function to vtuikit.c
/**
 * 從第 y 列開始 show 出 filename 檔案中的前 lines 行。
 * mode 為 output 的模式，參數同 strip_ansi。
 * @param filename: the file to show
 * @param y:	    starting line on screen
 * @param lines:    max lines to be displayed
 * @param mode:	    SHOWFILE_*, see modes.h
 * @return 失敗傳回 0，否則為 1。
 *         2 表示有 PttPrints 碼
 */
int
show_file(const char *filename, int y, int lines, int mode)
{
    FILE *fp;
    char buf[ANSILINELEN];
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

int
show_80x24_screen(const char *filename)
{
    clear();
    // max 24 lines, holding one more line for pause/messages
    return show_file(filename, 0, 24, SHOWFILE_ALLOW_ALL);
}

// TODO
// move this function to vtuikit.c
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

// TODO
// move this function to vtuikit.c
void
cursor_show(int row, int column)
{
    move(row, column);
    outs(STR_CURSOR);
    move(row, column);
}

// TODO
// move this function to vtuikit.c
void
cursor_clear(int row, int column)
{
    move(row, column);
    outs(STR_UNCUR);
}

// TODO
// move this function to vtuikit.c
int
cursor_key(int row, int column)
{
    int             ch;

    cursor_show(row, column);
    ch = vkey();
    cursor_clear(row, column);
    return ch;
}

// TODO
// move this function to vtuikit.c
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

// TODO
// move this function to vtuikit.c
void
show_help(const char * const helptext[])
{
    const char     *str;
    int             i;

    clear();
    for (i = 0; (str = helptext[i]); i++) {
	if (*str == '\0')
	    prints(ANSI_COLOR(1) "【 %s 】" ANSI_RESET "\n", str + 1);
	else if (*str == '\01')
	    prints("\n" ANSI_COLOR(36) "【 %s 】" ANSI_RESET "\n", str + 1);
	else
	    prints("        %s\n", str);
    }
    PRESSANYKEY();
}

// TODO
// move this function to vtuikit.c
void
show_helpfile(const char *helpfile)
{
    clear();
    show_file((char *)helpfile, 0, b_lines, SHOWFILE_ALLOW_ALL);
    PRESSANYKEY();
}

// vgets/getdata compatible helpers
static int
getdata2vgetflag(int echo)
{
    assert(echo != GCARRY);

    if (echo == LCECHO)
	echo = VGET_LOWERCASE;
    else if (echo == NUMECHO)
	echo = VGET_DIGITS;
    else if (echo == NOECHO)
	echo = VGETSET_NOECHO;
    else if (echo == PASSECHO)
	echo = VGETSET_PASSWORD;
    else
	echo = VGET_DEFAULT;

    return echo;
}

int
getdata_buf(int line, int col, const char *prompt, char *buf, int len, int echo)
{
    move(line, col); SOLVE_ANSI_CACHE();
    if(prompt && *prompt) outs(prompt);
    return vgetstr(buf, len, getdata2vgetflag(echo), buf);
}

int
getdata_str(int line, int col, const char *prompt, char *buf, int len, int echo,
            const char *defaultstr)
{
    move(line, col); SOLVE_ANSI_CACHE();
    if(prompt && *prompt) outs(prompt);
    return vgetstr(buf, len, getdata2vgetflag(echo), defaultstr);
}

int
getdata(int line, int col, const char *prompt, char *buf, int len, int echo)
{
    move(line, col); SOLVE_ANSI_CACHE();
    if(prompt && *prompt) outs(prompt);
    return vgets(buf, len, getdata2vgetflag(echo));
}

int
get_new_passwd(int y, const char *userid, char *out_passwd, size_t out_size)
{
    char confirm[PW_PLAIN_SIZE];

    mvouts(y + 1, 0, ANSI_RESET
           "為避免被偷看，您的密碼會顯示為 * ，輸入完後按 Enter 鍵即可。\n");
    mvprints(y + 2, 0, ANSI_RESET ANSI_COLOR(1;33)
           "請注意本站密碼上限已改為 %d 個字元，超過 8 個字的部份不會再被忽略。"
           ANSI_RESET, PW_PLAIN_LEN);

    while (1) {

      if (!getdata(y, 0, "新密碼:", out_passwd, out_size, PASSECHO)) {
          mvouts(y, 0, "未輸入新密碼。");
          mvouts(y+2, 0, "");
          mvouts(y+1, 0, "");
          return 0;
      }

      if (strlen(out_passwd) < PW_PLAIN_MIN) {
          mvprints(y+1, 0, "請重新輸入，密碼至少要有 %d 個字元", PW_PLAIN_MIN);
          continue;
      }

      if (userid && *userid && strcmp(out_passwd, userid) == 0) {
          mvouts(y+1, 0, "密碼不可以跟帳號一樣，請重新輸入");
          continue;
      }

      mvouts(y, 0, "請再輸入一次新密碼以確認。");
      getdata(y+1, 0, "新密碼:", confirm, sizeof(confirm), PASSECHO);

      if (strcmp(out_passwd, confirm) != 0) {
          mvouts(y+1, 0, "新密碼兩次輸入結果不符合，請重新輸入");
          continue;
      }

      explicit_bzero(confirm, sizeof(confirm));
      mvouts(y, 0, "已確認兩次輸入新密碼結果一致。");
      mvouts(y+2, 0, "");
      break;
    }
    return 1;
}

int
set_user_new_passwd(int y, userec_t *u)
{
    char passbuf[PW_PLAIN_SIZE];
    int ok = 0;

    if (get_new_passwd(y, u->userid, passbuf, sizeof(passbuf))) {
        setuser_passwd(u, passbuf);
        ok = 1;
    }
    explicit_bzero(passbuf, sizeof(passbuf));
    return ok;
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


static int
MonthDay(int m, int leap)
{
    int      day[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    assert(1<=m && m<=12);
    return leap && m == 2 ? 29 : day[m - 1];
}

/**
 * return 1 if date and time is invalid
 */
int ParseDateTime(const char *date, int *year, int *month, int *day,
		  int *hour, int *min, int *sec)
{
    char           *y, *m, *d, *hh, *mm, *ss;
    char           buf[128];
    char *strtok_pos;

    STRLCPY(buf, date);
    y = strtok_r(buf, "/", &strtok_pos); if (!y) return 1;
    m = strtok_r(NULL, "/", &strtok_pos);if (!m) return 1;
    d = strtok_r(NULL, " ", &strtok_pos); if (!d) return 1;

    if (hour) {
	hh = strtok_r(NULL, ":", &strtok_pos);
	if (!hh) return 1;
	*hour = atoi(hh);
    }
    if (min ) {
	mm = strtok_r(NULL, ":", &strtok_pos);
	if (!mm) return 1;
	*min  = atoi(mm);
    }
    if (sec ) {
	ss = strtok_r(NULL, "",  &strtok_pos);
	if (!ss) return 1;
	*sec  = atoi(ss);
    }

    *year = atoi(y);
    *month = atoi(m);
    *day = atoi(d);

    if (hour && (*hour < 0 || *hour > 23)) return 1;
    if (min  && (*min  < 0 || *min  > 59)) return 1;
    if (sec  && (*sec  < 0 || *sec  > 59)) return 1;

    if (*year < 1 || *month < 1 || *month > 12 ||
	*day < 1 || *day > MonthDay(*month, is_leap_year(*year)))
	return 1;
    return 0;
}

/**
 * return 1 if date is invalid
 */
int ParseDate(const char *date, int *year, int *month, int *day)
{
    return ParseDateTime(date, year, month, day, NULL, NULL, NULL);
}

