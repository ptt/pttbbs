/* $Id$ */
#include "bbs.h"
#include "fnv_hash.h"

/* ----------------------------------------------------- */
/* set file path for boards/user home                    */
/* ----------------------------------------------------- */
const static char    *str_home_file = "home/%c/%s/%s";
const static char    *str_board_file = "boards/%c/%s/%s";
const static char    *str_board_n_file = "boards/%c/%s/%s.%d";

static char cdate_buffer[32];

#define STR_DOTDIR  ".DIR"
const static char    *str_dotdir = STR_DOTDIR;

void
sethomepath(char *buf, const char *userid)
{
    sprintf(buf, "home/%c/%s", userid[0], userid);
}

void
sethomedir(char *buf, const char *userid)
{
    sprintf(buf, str_home_file, userid[0], userid, str_dotdir);
}

void
sethomeman(char *buf, const char *userid)
{
    sprintf(buf, str_home_file, userid[0], userid, "man");
}


void
sethomefile(char *buf, const char *userid, const char *fname)
{
    sprintf(buf, str_home_file, userid[0], userid, fname);
}

void
setuserfile(char *buf, const char *fname)
{
    sprintf(buf, str_home_file, cuser.userid[0], cuser.userid, fname);
}

void
setapath(char *buf, const char *boardname)
{
    sprintf(buf, "man/boards/%c/%s", boardname[0], boardname);
}

void
setadir(char *buf, const char *path)
{
    sprintf(buf, "%s/%s", path, str_dotdir);
}

void
setbpath(char *buf, const char *boardname)
{
    sprintf(buf, "boards/%c/%s", boardname[0], boardname);
}

void
setbdir(char *buf, const char *boardname)
{
    sprintf(buf, str_board_file, boardname[0], boardname,
	    (currmode & MODE_DIGEST ? fn_mandex : str_dotdir));
}

void
setbfile(char *buf, const char *boardname, const char *fname)
{
    sprintf(buf, str_board_file, boardname[0], boardname, fname);
}

void
setbnfile(char *buf, const char *boardname, const char *fname, int n)
{
    sprintf(buf, str_board_n_file, boardname[0], boardname, fname, n);
}

/*
 * input	direct
 * output	buf: copy direct
 * 		fname: direct 的檔名部分
 */
void
setdirpath(char *buf, char *direct, char *fname)
{
    strcpy(buf, direct);
    direct = strrchr(buf, '/');
    assert(direct);
    strcpy(direct + 1, fname);
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

/* ----------------------------------------------------- */
/* 字串轉換檢查函數                                      */
/* ----------------------------------------------------- */
int
str_checksum(char *str)
{
    int             n = 1;
    if (strlen(str) < 6)
	return 0;
    while (*str)
	n += *(str++) * (n);
    return n;
}

/**
 * 將字串 s 轉為小寫存回 t
 * @param t allocated char array
 * @param s
 */
void
str_lower(char *t, char *s)
{
    register unsigned char ch;

    do {
	ch = *s++;
	*t++ = char_lower(ch);
    } while (ch);
}

/**
 * 移除字串 buf 後端多餘的空白。
 * @param buf
 */
void
trim(char *buf)
{				/* remove trailing space */
    char           *p = buf;

    while (*p)
	p++;
    while (--p >= buf) {
	if (*p == ' ')
	    *p = '\0';
	else
	    break;
    }
}

/**
 * 移除 src 的 '\n' 並改成 '\0'
 * @param src
 */
void chomp(char *src)
{
    while(*src){
	if (*src == '\n')
	    *src = 0;
	else
	    src++;
    }
}

/* ----------------------------------------------------- */
/* 字串檢查函數：英文、數字、檔名、E-mail address        */
/* ----------------------------------------------------- */

int
invalid_pname(char *str)
{
    char           *p1, *p2, *p3;

    p1 = str;
    while (*p1) {
	if (!(p2 = strchr(p1, '/')))
	    p2 = str + strlen(str);
	if (p1 + 1 > p2 || p1 + strspn(p1, ".") == p2)
	    return 1;
	for (p3 = p1; p3 < p2; p3++)
	    if (not_alnum(*p3) && !strchr("@[]-._", *p3))
		return 1;
	p1 = p2 + (*p2 ? 1 : 0);
    }
    return 0;
}

int
valid_ident(char *ident)
{
    char    *invalid[] = {"unknown@", "root@", "gopher@", "bbs@",
    "@bbs", "guest@", "@ppp", "@slip", NULL};
    char            buf[128];
    int             i;

    str_lower(buf, ident);
    for (i = 0; invalid[i]; i++)
	if (strstr(buf, invalid[i]))
	    return 0;
    return 1;
}

int
is_uBM(char *list, char *id)
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
is_BM(char *list)
{
    if (is_uBM(list, cuser.userid)) {
	cuser.userlevel |= PERM_BM;	/* Ptt 自動加上BM的權利 */
	return 1;
    }
    return 0;
}

int
userid_is_BM(char *userid, char *list)
{
    register int    ch, len;

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

/* ----------------------------------------------------- */
/* 檔案檢查函數：檔案、目錄、屬於                        */
/* ----------------------------------------------------- */

/**
 * 傳回 fname 的檔案大小
 * @param fname
 */
off_t
dashs(char *fname)
{
    struct stat     st;

    if (!stat(fname, &st))
	return st.st_size;
    else
	return -1;
}

/**
 * 傳回 fname 的 mtime
 * @param fname
 */
time4_t
dasht(char *fname)
{
    struct stat     st;

    if (!stat(fname, &st))
	return st.st_mtime;
    else
	return -1;
}

/**
 * 傳回 fname 是否為 symbolic link
 * @param fname
 */
int
dashl(char *fname)
{
    struct stat     st;

    return (lstat(fname, &st) == 0 && S_ISLNK(st.st_mode));
}

/**
 * 傳回 fname 是否為一般的檔案
 * @param fname
 */
int
dashf(char *fname)
{
    struct stat     st;

    return (stat(fname, &st) == 0 && S_ISREG(st.st_mode));
}

/**
 * 傳回 fname 是否為目錄
 * @param fname
 */
int
dashd(char *fname)
{
    struct stat     st;

    return (stat(fname, &st) == 0 && S_ISDIR(st.st_mode));
}

#define BUFFER_SIZE	8192
static int copy_file_to_file(char *src, char *dst)
{
    char buf[BUFFER_SIZE];
    int fdr, fdw, len;

    if ((fdr = open(src, O_RDONLY)) < 0)
	return -1;

    if ((fdw = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0) {
	close(fdr);
	return -1;
    }

    while (1) {
	len = read(fdr, buf, sizeof(buf));
	if (len <= 0)
	    break;
	write(fdw, buf, len);
	if (len < BUFFER_SIZE)
	    break;
    }

    close(fdr);
    close(fdw);
    return 0;
}
#undef BUFFER_SIZE

static int copy_file_to_dir(char *src, char *dst)
{
    char buf[256];
    char *slash;
    if ((slash = rindex(src, '/')) == NULL)
	sprintf(buf, "%s/%s", dst, src);
    else
	sprintf(buf, "%s/%s", dst, slash);
    return copy_file_to_file(src, buf);
}

static int copy_dir_to_dir(char *src, char *dst)
{
    DIR *dir;
    struct dirent *entry;
    struct stat st;
    char buf[256], buf2[256];

    if (stat(dst, &st) < 0)
	if (mkdir(dst, 0700) < 0)
	    return -1;

    if ((dir = opendir(src)) == NULL)
	return -1;

    while ((entry = readdir(dir)) != NULL) {
	if (strcmp(entry->d_name, ".") == 0 ||
	    strcmp(entry->d_name, "..") == 0)
	    continue;
	sprintf(buf, "%s/%s", src, entry->d_name);
	sprintf(buf2, "%s/%s", dst, entry->d_name);
	if (stat(buf, &st) < 0)
	    continue;
	if (S_ISDIR(st.st_mode))
	    mkdir(buf2, 0700);
	copy_file(buf, buf2);
    }

    closedir(dir);
    return 0;
}

/**
 * copy src to dst (recursively)
 * @param src and dst are file or dir
 * @return -1 if failed
 */
int copy_file(char *src, char *dst)
{
    struct stat st;

    if (stat(dst, &st) == 0 && S_ISDIR(st.st_mode)) {
	if (stat(src, &st) < 0)
	    return -1;
	
    	if (S_ISDIR(st.st_mode))
	    return copy_dir_to_dir(src, dst);
	else if (S_ISREG(st.st_mode))
	    return copy_file_to_dir(src, dst);
	return -1;
    }
    else if (stat(src, &st) == 0 && S_ISDIR(st.st_mode))
	return copy_dir_to_dir(src, dst);
    return copy_file_to_file(src, dst);
}

int
belong(char *filelist, char *key)
{
    return file_exist_record(filelist, key);
}

unsigned int
ipstr2int(char *ip)
{
    unsigned int i, val = 0;
    char buf[32];
    char *nil, *p;
    strcpy(buf, ip);
    p = buf;
    for (i = 0; i < 4; i++) {
	nil = strchr(p, '.');
	if (nil != NULL)
	    *nil = 0;
	val *= 256;
	val += atoi(p);
	if (nil != NULL)
	    p = nil + 1;
    }
    return val;
}

#ifndef _BBS_UTIL_C_ /* getdata_buf */
time4_t
gettime(int line, time4_t dt, char*head)
{
    char            yn[7];
    int i;
    struct tm      *ptime = localtime4(&dt), endtime;

    memcpy(&endtime, ptime, sizeof(struct tm));
    snprintf(yn, sizeof(yn), "%4d", ptime->tm_year + 1900);
    move(line, 0); outs(head);
    i=strlen(head);
    do {
	getdata_buf(line, i, " 西元年:", yn, 5, LCECHO);
    } while ((endtime.tm_year = atoi(yn) - 1900) < 0 || endtime.tm_year > 200);
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
    return mktime(&endtime);
}
#endif

char           *
Cdate(time4_t *clock)
{
    time_t          temp = (time_t)*clock;
    struct tm      *mytm = localtime(&temp);

    strftime(cdate_buffer, sizeof(cdate_buffer), "%m/%d/%Y %T %a", mytm);
    return cdate_buffer;
}

char           *
Cdatelite(time4_t *clock)
{
    time_t          temp = (time_t)*clock;
    struct tm      *mytm = localtime(&temp);

    strftime(cdate_buffer, sizeof(cdate_buffer), "%m/%d/%Y %T", mytm);
    return cdate_buffer;
}

char           *
Cdatedate(time4_t * clock)
{
    time_t          temp = (time_t)*clock;
    struct tm      *mytm = localtime(&temp);

    strftime(cdate_buffer, sizeof(cdate_buffer), "%m/%d/%Y", mytm);
    return cdate_buffer;
}

#ifndef _BBS_UTIL_C_
/* 這一區都是有關於畫面處理的, 故 _BBS_UTIL_C_ 不須要 */
static void
capture_screen()
{
    char            fname[200];
    FILE           *fp;
    int             i;

    getdata(b_lines - 2, 0, "把這個畫面收入到暫存檔？[y/N] ",
	    fname, 4, LCECHO);
    if (fname[0] != 'y')
	return;

    setuserfile(fname, ask_tmpbuf(b_lines - 1));
    if ((fp = fopen(fname, "w"))) {
	for (i = 0; i < scr_lns; i++)
	    fprintf(fp, "%.*s\n", big_picture[i].len, big_picture[i].data);
	fclose(fp);
    }
}

int
vmsg_lines(const int lines, const char msg[])
{
    int             ch;

    move(lines, 0);
    clrtoeol();

    if (msg)
        outs((char *)msg);
    else
        outs("\033[1;34;44m ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄\033[1;37;44m 請按 "
"\033[36m任意鍵 \033[37m繼續 \033[1;34m▄▄▄▄▄\033[36m^T 收錄暫存檔\033[34m▄▄▄ \033[m");

    do {
	if( (ch = igetch()) == Ctrl('T') )
	    capture_screen();
    } while( ch == 0 );

    move(lines, 0);
    clrtoeol();
    return ch;
}

#ifdef PLAY_ANGEL
void
pressanykey_or_callangel(){
    int             ch;

    outmsg(
"\033[1;34;44m ▄▄▄▄ \033[32mH \033[36m呼叫小天使\033[34m ▄▄▄▄\033[1;37;44m 請按 "
"\033[36m任意鍵 \033[37m繼續 \033[1;34m▄▄▄▄▄\033[36m^T 收錄暫存檔\033[34m▄▄▄ \033[m");
    do {
	ch = igetch();

	if (ch == Ctrl('T')) {
	    capture_screen();
	    break;
	}else if (ch == 'h' || ch == 'H'){
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
    char   ans[5];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg , 128, fmt, ap);
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
    i = vsnprintf(msg , 128, fmt, ap);
    va_end(ap);
    return vmsg_lines(b_lines, msg);
}

/* TODO 極少 caller 用到 format, 考慮拆開成 vmsgf 節省 cpu */
int
vmsg(const char *fmt,...)
{
    char   msg[256] = "\033[1;36;44m ◆ ";
    int i=14; // 14=strlen(msg)
    va_list ap;
    va_start(ap, fmt);
    i += vsnprintf(msg + i, 128, fmt, ap);
    va_end(ap);
    for(; i < 71; i++)
	msg[i] = ' ';
    strcat(msg + 71,
	   "\033[33;46m \033[200m\033[1431m\033[506m[按任意鍵繼續]\033[201m \033[m");
    return vmsg_lines(b_lines, msg);
}

/**
 * 從第 y 列開始 show 出 filename 檔案中的前 lines 行。
 * mode 為 output 的模式，參數同 strip_ansi。
 * @param filename
 * @param x
 * @param lines
 * @param mode
 * @return 失敗傳回 0，否則為 1。
 */
int
show_file(char *filename, int y, int lines, int mode)
{
    FILE           *fp;
    char            buf[256];

    if (y >= 0)
	move(y, 0);
    clrtoline(lines + y);
    if ((fp = fopen(filename, "r"))) {
	while (fgets(buf, sizeof(buf), fp) && lines--)
	    outs(Ptt_prints(buf, mode));
	fclose(fp);
    } else
	return 0;
    return 1;
}

void
bell()
{
    char            c;

    c = Ctrl('G');
    write(1, &c, 1);
}

int
search_num(int ch, int max)
{
    int             clen = 1;
    int             x, y;
    char            genbuf[10];

    outmsg("\033[7m 跳至第幾項：\033[m");
    outc(ch);
    genbuf[0] = ch;
    getyx(&y, &x);
    x--;
    while ((ch = igetch()) != '\r') {
	if (ch == 'q' || ch == 'e')
	    return -1;
	if (ch == '\n')
	    break;
	if (ch == '\177' || ch == Ctrl('H')) {
	    if (clen == 0) {
		bell();
		continue;
	    }
	    clen--;
	    move(y, x + clen);
	    outc(' ');
	    move(y, x + clen);
	    continue;
	}
	if (!isdigit(ch)) {
	    bell();
	    continue;
	}
	if (x + clen >= scr_cols || clen >= 6) {
	    bell();
	    continue;
	}
	genbuf[clen++] = ch;
	outc(ch);
    }
    genbuf[clen] = '\0';
    move(b_lines, 0);
    clrtoeol();
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
 * 在瑩幕左上角 show 出 "【title】"
 * @param title
 */
void
stand_title(char *title)
{
    clear();
    prints("\033[1;37;46m【 %s 】\033[m\n", title);
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
printdash(char *mesg)
{
    int             head = 0, tail;

    if (mesg)
	head = (strlen(mesg) + 1) >> 1;

    tail = head;

    while (head++ < 38)
	outc('-');

    if (tail) {
	outc(' ');
	outs(mesg);
	outc(' ');
    }
    while (tail++ < 38)
	outc('-');
    outc('\n');
}

int
log_user(const char *fmt, ...)
{
    char msg[256], filename[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg , 128, fmt, ap);
    va_end(ap);

    sethomefile(filename, cuser.userid, "USERLOG");
    return log_file(filename, LOG_CREAT | LOG_VF,
		    "%s: %s %s", cuser.userid, msg,  Cdate(&now));
}

int
log_file(char *fn, int flag, const char *fmt,...)
{
    int        fd;
    char       msg[256];
    const char *realmsg;
    if( !(flag & LOG_VF) ){
	realmsg = fmt;
    }
    else{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg , 128, fmt, ap);
	va_end(ap);
	realmsg = msg;
    }

    if( (fd = open(fn, O_APPEND | O_WRONLY | ((flag & LOG_CREAT)? O_CREAT : 0),
		   ((flag & LOG_CREAT) ? 0664 : 0))) < 0 )
	return -1;
    if( write(fd, realmsg, strlen(realmsg)) < 0 ){
	close(fd);
	return -1;
    }
    close(fd);
    return 0;
}

void
show_help(char * const helptext[])
{
    char           *str;
    int             i;

    clear();
    for (i = 0; (str = helptext[i]); i++) {
	if (*str == '\0')
	    prints("\033[1m【 %s 】\033[0m\n", str + 1);
	else if (*str == '\01')
	    prints("\n\033[36m【 %s 】\033[m\n", str + 1);
	else
	    prints("        %s\n", str);
    }
#ifdef PLAY_ANGEL
    if (HAS_PERM(PERM_LOGINOK))
	pressanykey_or_callangel();
    else
#endif
	pressanykey();
}

void
show_helpfile(const char *helpfile)
{
    clear();
    show_file((char *)helpfile, 0, b_lines, NO_RELOAD);
#ifdef PLAY_ANGEL
    if (HAS_PERM(PERM_LOGINOK))
	pressanykey_or_callangel();
    else
#endif
    pressanykey();
}

#endif // _BBS_UTIL_C_

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
#if defined(DEBUG) && !defined(_BBS_UTIL_C_)
    vmsg("critical malloc %d bytes", size);
#endif
    return (void *)&p[1];
}

void FREE(void *ptr)
{
    int     size = ((int *)ptr)[-1];
    munmap((void *)(&(((int *)ptr)[-1])), size);
#if defined(DEBUG) && !defined(_BBS_UTIL_C_)
    vmsg("critical free %d bytes", size);
#endif
}
#endif

unsigned
StringHash(unsigned char *s)
{
    return fnv1a_32_strcase(s, FNV1_32_INIT);
}

inline int *intbsearch(int key, int *base0, int nmemb)
{
    /* 改自 /usr/src/lib/libc/stdlib/bsearch.c ,
       專給搜 int array 用的, 不透過 compar function 故較快些 */
    const   char *base = (char *)base0;
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

int qsort_intcompar(const void *a, const void *b)
{
    return *(int *)a - *(int *)b;
}

#ifdef TIMET64
char           *
ctime4(time4_t *clock)
{
    time_t temp = (time_t)*clock;
    
    return ctime(&temp);
}

struct tm *localtime4(time4_t *t)
{
    if( t == NULL )
	return localtime(NULL);
    else {
	time_t  temp = (time_t)*t;
	return localtime(&temp);
    }
}

time4_t time4(time4_t *ptr)
{
    if( ptr == NULL )
	return time(NULL);
    else
	return *ptr = (time4_t)time(NULL);
}
#endif

#ifdef OUTTACACHE
int tobind(int port)
{
    int     sockfd, val;
    struct  sockaddr_in     servaddr;

    if( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
	perror("socket()");
	exit(1);
    }
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
	       (char *)&val, sizeof(val));
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    if( bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0 ) {
	perror("bind()");
	exit(1);
    }
    if( listen(sockfd, 5) < 0 ) {
	perror("listen()");
	exit(1);
    }

    return sockfd;
}

int toconnect(char *host, int port)
{
    int    sock;
    struct sockaddr_in serv_name;
    if( (sock = socket(AF_INET, SOCK_STREAM, 0)) < 0 ){
        perror("socket");
        return -1;
    }

    serv_name.sin_family = AF_INET;
    serv_name.sin_addr.s_addr = inet_addr(host);
    serv_name.sin_port = htons(port);
    if( connect(sock, (struct sockaddr*)&serv_name, sizeof(serv_name)) < 0 ){
        close(sock);
        return -1;
    }
    return sock;
}

/**
 * same as read(2), but read until exactly size len 
 */
int toread(int fd, void *buf, int len)
{
    int     l;
    for( l = 0 ; len > 0 ; )
	if( (l = read(fd, buf, len)) <= 0 )
	    return -1;
	else{
	    buf += l;
	    len -= l;
	}
    return l;
}

/**
 * same as write(2), but write until exactly size len 
 */
int towrite(int fd, void *buf, int len)
{
    int     l;
    for( l = 0 ; len > 0 ; )
	if( (l = write(fd, buf, len)) <= 0 )
	    return -1;
	else{
	    buf += l;
	    len -= l;
	}
    return l;
}
#endif
