/* $Id: stuff.c,v 1.4 2002/06/23 03:33:07 ptt Exp $ */
#include "bbs.h"

/* ----------------------------------------------------- */
/* set file path for boards/user home                    */
/* ----------------------------------------------------- */
static char *str_home_file = "home/%c/%s/%s";
static char *str_board_file = "boards/%c/%s/%s";

#define STR_DOTDIR  ".DIR"
static char *str_dotdir = STR_DOTDIR;

void setcalfile(char *buf, char *userid) {
    sprintf(buf, "home/%c/%s/calendar", userid[0], userid);
}

void sethomepath(char *buf, char *userid) {
    sprintf(buf, "home/%c/%s", userid[0], userid);
}

void sethomedir(char *buf, char *userid) {
    sprintf(buf, str_home_file, userid[0], userid, str_dotdir);
}

void sethomeman(char *buf, char *userid) {
    sprintf(buf, str_home_file, userid[0], userid, "man");
}

void sethomefile(char *buf, char *userid, char *fname) {
    sprintf(buf, str_home_file, userid[0], userid, fname);
}

void setuserfile(char *buf, char *fname) {
    sprintf(buf, str_home_file, cuser.userid[0], cuser.userid, fname);
}

void setapath(char *buf, char *boardname) {
    sprintf(buf, "man/boards/%c/%s", boardname[0], boardname);
}

void setadir(char *buf, char *path) {
    sprintf(buf, "%s/%s", path, str_dotdir);
}

void setbpath(char *buf, char *boardname) {
    sprintf(buf, "boards/%c/%s", boardname[0], boardname);
}

void setbdir(char *buf, char *boardname) {
    sprintf(buf, str_board_file, boardname[0], boardname,
	    currmode & MODE_ETC ? ".ETC" :
	    (currmode & MODE_DIGEST ? fn_mandex : str_dotdir));
}

void setbfile(char *buf, char *boardname, char *fname) {
    sprintf(buf, str_board_file, boardname[0], boardname, fname);
}

void setdirpath(char *buf, char *direct, char *fname) {
    strcpy(buf, direct);
    direct = strrchr(buf, '/');
    strcpy(direct + 1, fname);
}

char *subject(char *title) {
    if(!strncasecmp(title, str_reply, 3)) {
	title += 3;
	if(*title == ' ')
	    title++;
    }
    return title;
}

/* ----------------------------------------------------- */
/* 字串轉換檢查函數                                      */
/* ----------------------------------------------------- */
int str_checksum(char *str) {
    int n = 1;
    if(strlen(str) < 6)
	return 0;
    while(*str)
        n += *(str++) * (n);
    return n;
}

void str_lower(char *t, char *s) {
    register unsigned char ch;

    do {
	ch = *s++;
	*t++ = char_lower(ch);
    } while(ch);
}

int strstr_lower(char *str, char *tag) {
    char buf[STRLEN];
    
    str_lower(buf, str);
    return (int)strstr(buf, tag);
}

void trim(char *buf) {            /* remove trailing space */
    char *p = buf;

    while(*p)
	p++;
    while(--p >= buf) {
	if(*p == ' ')
	    *p = '\0';
	else
	    break;
    }
}

/* ----------------------------------------------------- */
/* 字串檢查函數：英文、數字、檔名、E-mail address        */
/* ----------------------------------------------------- */
int isprint2(char ch) {
    return ((ch & 0x80) ? 1 : isprint(ch));
    //return 1;
}

int not_alpha(char ch) {
    return (ch < 'A' || (ch > 'Z' && ch < 'a') || ch > 'z');
}

int not_alnum(char ch) {
    return (ch < '0' || (ch > '9' && ch < 'A') ||
	    (ch > 'Z' && ch < 'a') || ch > 'z');
}

int invalid_pname(char *str) {
    char *p1, *p2, *p3;
    
    p1 = str;
    while(*p1) {
	if(!(p2 = strchr(p1, '/')))
	    p2 = str + strlen(str);
	if(p1 + 1 > p2 || p1 + strspn(p1, ".") == p2)
	    return 1;
	for(p3 = p1; p3 < p2; p3++)
	    if(not_alnum(*p3) && !strchr("@[]-._", *p3))
		return 1;
	p1 = p2 + (*p2 ? 1 : 0);
    }
    return 0;
}

int valid_ident(char *ident) {
    static char *invalid[] = {"unknown@", "root@", "gopher@", "bbs@",
			      "@bbs", "guest@", "@ppp", "@slip", NULL};
    char buf[128];
    int i;
    
    str_lower(buf, ident);
    for(i = 0; invalid[i]; i++)
	if(strstr(buf, invalid[i]))
	    return 0;
    return 1;
}

int is_uBM(char *list, char *id) {
    register int len;

    if(list[0] == '[')
	list++;
    if(list[0] > ' ') {
	len = strlen(id);
	do {
	    if(!strncasecmp(list, id, len)) {
		list += len;
		if((*list == 0) || (*list == '/') ||
		   (*list == ']') || (*list == ' '))
		    return 1;
	    }
	    if((list = strchr(list,'/')) != NULL)
		list++;
	    else
		break;
	} while(1);
    }
    return 0;
}

int is_BM(char *list) {
    if(is_uBM(list,cuser.userid)) {
	cuser.userlevel |= PERM_BM; /* Ptt 自動加上BM的權利 */
	return 1;
    }
    return 0;
}

int userid_is_BM(char *userid, char *list) {
    register int ch, len;
    
    ch = list[0];
    if((ch > ' ') && (ch < 128)) {
	len = strlen(userid);
	do {
	    if(!strncasecmp(list, userid, len)) {
		ch = list[len];
		if((ch == 0) || (ch == '/') || (ch == ']'))
		    return 1;
	    }
	    while((ch = *list++)) {
		if(ch == '/')
		    break;
	    }
	} while(ch);
    }
    return 0;
}

/* ----------------------------------------------------- */
/* 檔案檢查函數：檔案、目錄、屬於                        */
/* ----------------------------------------------------- */
off_t dashs(char *fname) {
    struct stat st;
    
    if(!stat(fname, &st))
        return st.st_size;
    else
        return -1;
}

long dasht(char *fname) {
    struct stat st;
    
    if(!stat(fname, &st))
        return st.st_mtime;
    else
        return -1;
}

int dashl(char *fname) {
    struct stat st;
    
    return (lstat(fname, &st) == 0 && S_ISLNK(st.st_mode));
}

int dashf(char *fname) {
    struct stat st;
    
    return (stat(fname, &st) == 0 && S_ISREG(st.st_mode));
}

int dashd(char *fname) {
    struct stat st;

    return (stat(fname, &st) == 0 && S_ISDIR(st.st_mode));
}

int belong(char *filelist, char *key) {
    FILE *fp;
    int rc = 0;
    
    if((fp = fopen(filelist, "r"))) {
	char buf[STRLEN], *ptr;
	
	while(fgets(buf, STRLEN, fp)) {
	    if((ptr = strtok(buf, str_space)) && !strcasecmp(ptr, key)) {
		rc = 1;
		break;
	    }
	}
	fclose(fp);
    }
    return rc;
}

time_t gettime(int line, time_t dt)
{
        char yn[7];
        struct tm *ptime = localtime(&dt), endtime;

        sprintf(yn, "%4d", ptime->tm_year+1900);
        do{
           getdata_buf(line, 0,  "西元年:", yn, 5, LCECHO);
           }while( (endtime.tm_year = atoi(yn)-1900)<0 || endtime.tm_year>200);
        sprintf(yn, "%d", ptime->tm_mon+1);
        do{
           getdata_buf(line, 13,  "月:", yn, 3, LCECHO);
          }while((endtime.tm_mon = atoi(yn)-1)<0 || endtime.tm_mon>11);
        sprintf(yn, "%d", ptime->tm_mday);
        do{
           getdata_buf(line, 22,  "日:", yn, 3, LCECHO);
          }while((endtime.tm_mday = atoi(yn))<1 || endtime.tm_mday>31);
        sprintf(yn, "%d", ptime->tm_hour);
        do{
           getdata_buf(line, 22,  "時(0-23):", yn, 3, LCECHO);
          }while(( endtime.tm_hour = atoi(yn))<0 || endtime.tm_hour>23);
        return mktime(&endtime);
}
char *Cdate(time_t *clock) {
    static char foo[32];
    struct tm *mytm = localtime(clock);
    
    strftime(foo, 32, "%m/%d/%Y %T %a", mytm);
    return foo;
}

char *Cdatelite(time_t *clock) {
    static char foo[32];
    struct tm *mytm = localtime(clock);
    
    strftime(foo, 32, "%m/%d/%Y %T", mytm);
    return foo;
}

char *Cdatedate(time_t *clock){
    static char foo[32];
    struct tm *mytm = localtime(clock);
    
    strftime(foo, 32, "%m/%d/%Y", mytm);
    return foo;
}

static void capture_screen() {
    char fname[200];
    FILE* fp;
    int i;

    getdata(b_lines - 2, 0, "把這個畫面收入到暫存檔？[y/N] ",
	    fname, 4, LCECHO);
    if(fname[0] != 'y' ) return;
    
    setuserfile(fname, ask_tmpbuf(b_lines - 1));
    if((fp = fopen(fname, "w"))) {
	for(i = 0; i < scr_lns; i++)
	    fprintf(fp, "%.*s\n", big_picture[i].len, big_picture[i].data);
	fclose(fp);
    }
}

void pressanykey() {
    int ch;
    
    outmsg("\033[37;45;1m                        "
	   "● 請按 \033[33m(Space/Return)\033[37m 繼續 ●"
	   "       \033[33m(^T)\033[37m 存暫存檔   \033[m");
    do {
	ch = igetkey();
	
	if(ch == Ctrl('T')) {
	    capture_screen();
	    break;
	}
    } while((ch != ' ') && (ch != KEY_LEFT) && (ch != '\r') && (ch != '\n'));
    move(b_lines, 0);
    clrtoeol();
    refresh();
}

int vmsg (const char *fmt, ...)
{
  va_list ap;
  char msg[80] = {0};
  int ch;

  va_start (ap, fmt);
  vsprintf (msg, fmt, ap);
  va_end (ap);

  move (b_lines, 0);
  clrtoeol ();

  if (*msg)
    prints ("\033[1;36;44m ◆ %-55.54s \033[33;46m \033[200m\033[1431m\033[506m[請按任意鍵繼續]\033[201m \033[m", msg);
  else
    outs ("\033[46;1m                        \033[37m"
      "\033[200m\033[1431m\033[506m□ 請按 \033[33m(Space/Return)\033[37m 繼續 □\033[201m"
      "                       \033[m");

  do {
	ch = igetkey();
	
	if(ch == Ctrl('T')) {
	    capture_screen();
	    break;
	}
    } while((ch != ' ') && (ch != KEY_LEFT) && (ch != '\r') && (ch != '\n'));

  
  move (b_lines, 0);
  clrtoeol ();
  refresh ();
  return ch;
}

void bell() {
    char c;
    
    c = Ctrl('G');
    write(1, &c, 1);
}

int search_num(int ch, int max) {
    int clen = 1;
    int x, y;
    char genbuf[10];
    
    outmsg("\033[7m 跳至第幾項：\033[m");
    outc(ch);
    genbuf[0] = ch;
    getyx(&y, &x);
    x--;
    while((ch = igetch()) != '\r') {
	if(ch == 'q' || ch == 'e')
	    return -1;
	if(ch == '\n')
	    break;
	if(ch == '\177' || ch == Ctrl('H')) {
	    if(clen == 0) {
		bell();
		continue;
	    }
	    clen--;
	    move(y, x + clen);
	    outc(' ');
	    move(y, x + clen);
	    continue;
	}
	if(!isdigit(ch)) {
	    bell();
	    continue;
	}
	if(x + clen >= scr_cols || clen >= 6) {
	    bell();
	    continue;
	}
	genbuf[clen++] = ch;
	outc(ch);
    }
    genbuf[clen] = '\0';
    move(b_lines, 0);
    clrtoeol();
    if(genbuf[0] == '\0')
	return -1;
    clen = atoi(genbuf);
    if(clen == 0)
	return 0;
    if(clen > max)
	return max;
    return clen - 1;
}

void stand_title(char *title) {
    clear();
    prints("\033[1;37;46m【 %s 】\033[m\n", title);
}

void cursor_show(int row, int column) {
    move(row, column);
    outs(STR_CURSOR);
    move(row, column + 1);
}

void cursor_clear(int row, int column) {
    move(row, column);
    outs(STR_UNCUR);
}

int cursor_key(int row, int column) {
    int ch;
    
    cursor_show(row, column);
    ch = egetch();
    move(row, column);
    outs(STR_UNCUR);
    return ch;
}

void printdash(char *mesg) {
    int head = 0, tail;
    
    if(mesg)
	head = (strlen(mesg) + 1) >> 1;
    
    tail = head;
    
    while(head++ < 38)
	outch('-');
    
    if(tail) {
	outch(' ');
	outs(mesg);
	outch(' ');
    }
    
    while(tail++ < 38)
	outch('-');
    outch('\n');
}

int log_file(char *filename,char *buf) {
    FILE *fp;

    if((fp = fopen(filename, "a" )) != NULL ) {
        fputs( buf, fp );
        if(!strchr(buf,'\n'))
	    fputc('\n',fp);
        fclose( fp );
        return 0;
    }
    else
	return -1;
}

void show_help(char *helptext[]) {
    char *str;
    int i;
    
    clear();
    for(i = 0; (str = helptext[i]); i++) {
	if(*str == '\0')
	    prints("\033[1m【 %s 】\033[0m\n", str + 1);
	else if(*str == '\01')
	    prints("\n\033[36m【 %s 】\033[m\n", str + 1);
	else
	    prints("        %s\n", str);
    }
    pressanykey();
}
