#include "bbs.h"

/* XXX set*() all assume buffer size = PATHLEN */

void
setuserfile(char *buf, const char *fname)
{
    sethomefile(buf, cuser.userid, fname);
}

void
setbdir(char *buf, const char *boardname)
{
    //assert(boardname[0]);
    snprintf(buf, PATHLEN, "boards/%c/%s/%s", boardname[0], boardname,
	    (currmode & MODE_DIGEST ? fn_mandex : FN_DIR));
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
    snprintf(yn, sizeof(yn), "%4d", ptime.tm_year + 1900);
    move(line, 0); SOLVE_ANSI_CACHE(); clrtoeol();
    snprintf(prompt, sizeof(prompt), "%s 西元年:", head);
    do {
	getdata_buf(line, 0, prompt, yn, 5, NUMECHO);
	// signed:   limited on (2037, ...)
	// unsigned: limited on (..., 1970)
	// let's restrict inside the boundary.
    } while ((endtime.tm_year = atoi(yn) - 1900) < 70 || endtime.tm_year > 135);
    strlcat(prompt, yn, sizeof(prompt));
    strlcat(prompt, " 月:", sizeof(prompt));
    snprintf(yn, sizeof(yn), "%d", ptime.tm_mon + 1);
    do {
	getdata_buf(line, 0, prompt, yn, 3, NUMECHO);
    } while ((endtime.tm_mon = atoi(yn) - 1) < 0 || endtime.tm_mon > 11);
    strlcat(prompt, yn, sizeof(prompt));
    strlcat(prompt, " 日:", sizeof(prompt));
    snprintf(yn, sizeof(yn), "%d", ptime.tm_mday);
    do {
	getdata_buf(line, 0, prompt, yn, 3, NUMECHO);
    } while ((endtime.tm_mday = atoi(yn)) < 1 || endtime.tm_mday > 31);
    snprintf(yn, sizeof(yn), "%d", ptime.tm_hour);
    strlcat(prompt, yn, sizeof(prompt));
    strlcat(prompt, " 時(0-23):", sizeof(prompt));
    do {
	getdata_buf(line, 0, prompt, yn, 3, NUMECHO);
    } while ((endtime.tm_hour = atoi(yn)) < 0 || endtime.tm_hour > 23);
    strlcat(prompt, yn, sizeof(prompt));
    strlcat(prompt, " 分(0-59):", sizeof(prompt));
    snprintf(yn, sizeof(yn), "%d", ptime.tm_min);
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
#ifdef OUTTA_TIMER
        now = SHM->GV2.e.now;
#else
	now = time(0);
#endif
}

void
wait_penalty(int sec)
{
    static time4_t lastWait = 0;

    syncnow();
    if (now - lastWait < sec)
    {
        sec = now - lastWait;
        if (sec < 0 || sec >= 5)
            sec = 5;
        sleep(sec);
        vkey_purge();
    }
    lastWait = now;
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

    strlcpy(buf, date, sizeof(buf));
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

/* AIDS */
aidu_t fn2aidu(const char *fn)
{
  aidu_t aidu = 0;
  aidu_t type = 0;
  aidu_t v1 = 0;
  aidu_t v2 = 0;
  char *sp = (char*)fn;

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

aidu_t aidc2aidu(const char *aidc)
{
  const char *sp = aidc;
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
