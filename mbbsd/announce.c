/* $Id$ */
#include "bbs.h"

/* copy temp queue operation -------------------------------------- */

/* TODO
 * change this to char* instead of char[]
 */
typedef struct {
    char     copyfile[PATHLEN];
    char     copytitle[TTLEN + 1];
    char     copyowner[IDLEN + 2];
} CopyQueue ;

#define COPYQUEUE_COMMON_SIZE (10)

static CopyQueue *copyqueue;
static int allocated_copyqueue = 0, used_copyqueue = 0, head_copyqueue = 0;

int copyqueue_testin(CopyQueue *pcq)
{
    int i = 0;
    for (i = head_copyqueue; i < used_copyqueue; i++)
	if (strcmp(pcq->copyfile, copyqueue[i].copyfile) == 0)
	    return 1;
    return 0;
}

int copyqueue_locate(CopyQueue *pcq)
{
    int i = 0;
    for (i = head_copyqueue; i < used_copyqueue; i++)
	if (strcmp(pcq->copyfile, copyqueue[i].copyfile) == 0)
	    return i;
    return -1;
}

int copyqueue_fileinqueue(const char *fn)
{
    int i = 0;
    for (i = head_copyqueue; i < used_copyqueue; i++)
	if (strcmp(fn, copyqueue[i].copyfile) == 0)
	    return 1;
    return 0;
}

void copyqueue_reset()
{
    allocated_copyqueue = 0;
    used_copyqueue = 0; 
    head_copyqueue = 0;
}

int copyqueue_append(CopyQueue *pcq)
{
    if(copyqueue_testin(pcq))
	return 0;
    if(head_copyqueue == used_copyqueue) 
    {
	// empty queue, happy happy reset
	if(allocated_copyqueue > COPYQUEUE_COMMON_SIZE)
	{
	    // let's reduce it
	    allocated_copyqueue = COPYQUEUE_COMMON_SIZE;
	    copyqueue = (CopyQueue*)realloc( copyqueue,
		      allocated_copyqueue * sizeof(CopyQueue));
	}
	head_copyqueue = used_copyqueue = 0;
    }
    used_copyqueue ++;

    if(used_copyqueue > allocated_copyqueue)
    {
	allocated_copyqueue = 
	    used_copyqueue + COPYQUEUE_COMMON_SIZE; // half page
	copyqueue = (CopyQueue*) realloc (copyqueue,
		sizeof(CopyQueue) * allocated_copyqueue);
	if(!copyqueue)
	{
	    vmsg("記憶體不足，拷貝失敗");
	    // try to reset
	    copyqueue_reset();
	    if(copyqueue) free(copyqueue);
	    copyqueue = NULL;
	    return 0;
	}
    }
    memcpy(&(copyqueue[used_copyqueue-1]), pcq, sizeof(CopyQueue));
    return 1;
}

int copyqueue_toggle(CopyQueue *pcq)
{
    int i = copyqueue_locate(pcq);
    if(i >= 0)
    {
#if 0
	if (getans("已標記過此檔，要取消標記嗎 [y/N]: ") != 'y')
	    return 1;
#endif
	/* remove it */
	used_copyqueue --;
	if(head_copyqueue > used_copyqueue)
	    head_copyqueue =used_copyqueue;
	if (i < used_copyqueue)
	{
	    memcpy(copyqueue + i, copyqueue+i+1, 
		    sizeof(CopyQueue) * (used_copyqueue - i));
	}
	return 0;
    } else {
	copyqueue_append(pcq);
    }
    return 1;
}

CopyQueue *copyqueue_gethead()
{
    if(	used_copyqueue <= 0 ||
	head_copyqueue >= used_copyqueue)
	return NULL;
    return &(copyqueue[head_copyqueue++]);
}

int copyqueue_querysize()
{
    if(	used_copyqueue <= 0 ||
	head_copyqueue >= used_copyqueue)
	return 0;
    return (used_copyqueue - head_copyqueue);
}

/* end copy temp queue operation ----------------------------------- */

void
a_copyitem(const char *fpath, const char *title, const char *owner, int mode)
{
    CopyQueue cq;
    static int flFirstAlert = 1;

    memset(&cq, 0, sizeof(CopyQueue));
    strcpy(cq.copyfile, fpath);
    strcpy(cq.copytitle, title);
    if (owner)
	strcpy(cq.copyowner, owner);

    //copyqueue_append(&cq);
    copyqueue_toggle(&cq);
    if (mode && flFirstAlert) {
#if 0
	move(b_lines-2, 0); clrtoeol();
	prints("目前已標記 %d 個檔案。[注意] 拷貝後才能刪除原文!",
		copyqueue_querysize());
#else
	vmsg("[注意] 提醒您複製/標記後要貼上(p)或附加(a)後才能刪除原文!");
	flFirstAlert = 0;
#endif
    }
}

#define FHSZ            sizeof(fileheader_t)

static void
a_loadname(menu_t * pm)
{
    char            buf[PATHLEN];
    int             len;

    if(p_lines != pm->header_size) {
	pm->header_size = p_lines;
	pm->header = (fileheader_t *) realloc(pm->header, pm->header_size*FHSZ);
	assert(pm->header);
    }

    setadir(buf, pm->path);
    len = get_records(buf, pm->header, FHSZ, pm->page + 1, pm->header_size); // XXX if get_records() return -1
    if (len < pm->header_size)
	bzero(&pm->header[len], FHSZ * (pm->header_size - len));
}

static void
a_timestamp(char *buf, const time4_t *time)
{
    struct tm      *pt = localtime4(time);

    sprintf(buf, "%02d/%02d/%02d", pt->tm_mon + 1, pt->tm_mday, (pt->tm_year + 1900) % 100);
}

static void
a_showmenu(menu_t * pm)
{
    char           *title, *editor;
    int             n;
    fileheader_t   *item;
    char            buf[PATHLEN];
    time4_t         dtime;

    showtitle("精華文章", pm->mtitle);
    prints("   " ANSI_COLOR(1;36) "編號    標      題%56s" ANSI_COLOR(0),
	   "編    選      日    期");

    if (pm->num) {
	setadir(buf, pm->path);
	a_loadname(pm);
	for (n = 0; n < p_lines && pm->page + n < pm->num; n++) {
	    int flTagged = 0;
	    item = &pm->header[n];
	    title = item->title;
	    editor = item->owner;
	    /*
	     * Ptt 把時間改為取檔案時間 dtime = atoi(&item->filename[2]);
	     */
	    snprintf(buf, sizeof(buf), "%s/%s", pm->path, item->filename);
	    if(copyqueue_querysize() > 0 && copyqueue_fileinqueue(buf))
	    {
		flTagged = 1;
	    }
	    dtime = dasht(buf);
	    a_timestamp(buf, &dtime);
	    prints("\n%6d%c%c%-47.46s%-13s[%s]", pm->page + n + 1,
		   (item->filemode & FILE_BM) ? 'X' :
		   (item->filemode & FILE_HIDE) ? ')' : '.',
		   flTagged ? 'c' : ' ',
		   title, editor,
		   buf);
	}
    } else
	outs("\n  《精華區》尚在吸取天地間的日月精華中... :)");

    move(b_lines, 0);
    if(copyqueue_querysize() > 0)
    {		// something in queue
	prints(
	 ANSI_COLOR(37;44) "【已標記(複製) %d 項】"
	 ANSI_COLOR(31;47) " (c)" ANSI_COLOR(30) "標記/複製 "
		    , copyqueue_querysize());

	if(pm->level == 0)
	    outs(" - 無管理權限，無法貼上               " ANSI_RESET);
	else
	    outs(   ANSI_COLOR(31) "(p)" ANSI_COLOR(30) "貼上/取消/重設標記 "
		    ANSI_COLOR(31) "(a)" ANSI_COLOR(30) "附加至文章後    "
		    ANSI_RESET);
    } 
    else if(pm->level)
    {		// BM
	outs(
	 ANSI_COLOR(34;46) " 【板  主】 "
	 ANSI_COLOR(31;47) "  (h)" ANSI_COLOR(30) "說明  "
	 ANSI_COLOR(31) "(q/←)" ANSI_COLOR(30) "離開  "
	 ANSI_COLOR(31) "(n)" ANSI_COLOR(30) "新增文章  "
	 ANSI_COLOR(31) "(g)" ANSI_COLOR(30) "新增目錄  "
	 ANSI_COLOR(31) "(e)" ANSI_COLOR(30) "編輯檔案  " ANSI_RESET
	 );
    }
    else
    {		// normal user
	outs(
	 ANSI_COLOR(34;46) " 【功\能鍵】 "
	 ANSI_COLOR(31;47) "  (h)" ANSI_COLOR(30) "說明  "
	 ANSI_COLOR(31) "(q/←)" ANSI_COLOR(30) "離開  "
	 ANSI_COLOR(31) "(k↑j↓)" ANSI_COLOR(30) "移動游標  "
	 ANSI_COLOR(31) "(enter/→)" ANSI_COLOR(30) "讀取資料  " ANSI_RESET);
    }
}

static int
a_searchtitle(menu_t * pm, int rev)
{
    static char     search_str[40] = "";
    int             pos;

    getdata(b_lines - 1, 1, "[搜尋]關鍵字:", search_str, sizeof(search_str), DOECHO);

    if (!*search_str)
	return pm->now;

    str_lower(search_str, search_str);

    rev = rev ? -1 : 1;
    pos = pm->now;
    do {
	pos += rev;
	if (pos == pm->num)
	    pos = 0;
	else if (pos < 0)
	    pos = pm->num - 1;
	if (pos < pm->page || pos >= pm->page + p_lines) {
	    pm->page = pos - pos % p_lines;
	    a_loadname(pm);
	}
	if (strcasestr(pm->header[pos - pm->page].title, search_str))
	    return pos;
    } while (pos != pm->now);
    return pm->now;
}

enum {
    NOBODY, MANAGER, SYSOP
};

static void
a_showhelp(int level)
{
    clear();
    outs(ANSI_COLOR(36) "【 " BBSNAME "公佈欄使用說明 】" ANSI_RESET "\n\n"
	 "[←][q]         離開到上一層目錄\n"
	 "[↑][k]         上一個選項\n"
	 "[↓][j]         下一個選項\n"
	 "[→][r][enter]  進入目錄／讀取文章\n"
	 "[^B][PgUp]      上頁選單\n"
	 "[^F][PgDn][Spc] 下頁選單\n"
	 "[##]            移到該選項\n"
	 "[F][U]          將文章寄回 Internet 郵箱/"
	 "將文章 uuencode 後寄回郵箱\n");
    if (level >= MANAGER) {
	outs("\n" ANSI_COLOR(36) "【 板主專用鍵 】" ANSI_RESET "\n"
	     "[H]             切換為 公開/會員/板主 才能閱\讀\n"
	     "[n/g/G]         收錄精華文章/開闢目錄/建立連線\n"
	     "[m/d/D]         移動/刪除文章/刪除一個範圍的文章\n"
	     "[f/T/e]         編輯標題符號/修改文章標題/內容\n"
	     "[c/p/a]         精華區內 標記(複製)/貼上(可多篇)/附加單篇文章\n"
	     "[^P/^A]         貼上/附加精華區外已用't'標記文章\n");
    }
    if (level >= SYSOP) {
	outs("\n" ANSI_COLOR(36) "【 站長專用鍵 】" ANSI_RESET "\n"
	     "[l]             建 symbolic link\n"
	     "[N]             查詢檔名\n");
    }
    pressanykey();
}

static void
a_forward(const char *path, const fileheader_t * pitem, int mode)
{
    fileheader_t    fhdr;

    strlcpy(fhdr.filename, pitem->filename, sizeof(fhdr.filename));
    strlcpy(fhdr.title, pitem->title, sizeof(fhdr.title));
    switch (doforward(path, &fhdr, mode)) {
    case 0:
	outmsg(msg_fwd_ok);
	break;
    case -1:
	outmsg(msg_fwd_err1);
	break;
    case -2:
	outmsg(msg_fwd_err2);
	break;
    }
}

static void
a_additem(menu_t * pm, const fileheader_t * myheader)
{
    char            buf[PATHLEN];

    setadir(buf, pm->path);
    if (append_record(buf, myheader, FHSZ) == -1)
	return;
    pm->now = pm->num++;

    if (pm->now >= pm->page + p_lines) {
	pm->page = pm->now - ((pm->page == 10000 && pm->now > p_lines / 2) ?
			      (p_lines / 2) : (pm->now % p_lines));
    }
    /* Ptt */
    strlcpy(pm->header[pm->now - pm->page].filename,
	    myheader->filename,
	    sizeof(pm->header[pm->now - pm->page].filename));
}

#define ADDITEM         0
#define ADDGROUP        1
#define ADDLINK         2

static void
a_newitem(menu_t * pm, int mode)
{
    char    *mesg[3] = {
	"[新增文章] 請輸入標題：",	/* ADDITEM */
	"[新增目錄] 請輸入標題：",	/* ADDGROUP */
	"請輸入標題："		/* ADDLINK */
    };

    char            fpath[PATHLEN], buf[PATHLEN], lpath[PATHLEN];
    fileheader_t    item;
    int             d;

    strlcpy(fpath, pm->path, sizeof(fpath));

    switch (mode) {
    case ADDITEM:
	stampfile(fpath, &item);
	strlcpy(item.title, "◇ ", sizeof(item.title));	/* A1BA */
	break;

    case ADDGROUP:
	stampdir(fpath, &item);
	strlcpy(item.title, "◆ ", sizeof(item.title));	/* A1BB */
	break;
    case ADDLINK:
	stamplink(fpath, &item);
	if (!getdata(b_lines - 2, 1, "新增連線：", buf, 61, DOECHO))
	    return;
	if (invalid_pname(buf)) {
	    unlink(fpath);
	    outs("目的地路徑不合法！");
	    igetch();
	    return;
	}
	item.title[0] = 0;
	// XXX Is it alright?
	// ex: path= "PASSWD"
	for (d = 0; d <= 3; d++) {
	    switch (d) {
	    case 0:
		snprintf(lpath, sizeof(lpath), BBSHOME "/man/boards/%c/%s/%s",
			currboard[0], currboard, buf);
		break;
	    case 1:
		snprintf(lpath, sizeof(lpath), BBSHOME "/man/boards/%c/%s",
			buf[0], buf);
		break;
	    case 2:
		snprintf(lpath, sizeof(lpath), BBSHOME "/%s",
			buf);
		break;
	    case 3:
		snprintf(lpath, sizeof(lpath), BBSHOME "/etc/%s",
			buf);
		break;
	    }
	    if (dashf(lpath)) {
		strlcpy(item.title, "☆ ", sizeof(item.title));	/* A1B3 */
		break;
	    } else if (dashd(lpath)) {
		strlcpy(item.title, "★ ", sizeof(item.title));	/* A1B4 */
		break;
	    }
	    if (!HasUserPerm(PERM_BBSADM) && d == 1)
		break;
	}

	if (!item.title[0]) {
	    unlink(fpath);
	    outs("目的地路徑不合法！");
	    igetch();
	    return;
	}
    }

    if (!getdata(b_lines - 1, 1, mesg[mode], &item.title[3], 55, DOECHO)) {
	if (mode == ADDGROUP)
	    rmdir(fpath);
	else
	    unlink(fpath);
	return;
    }
    switch (mode) {
    case ADDITEM:
	if (vedit(fpath, 0, NULL) == -1) {
	    unlink(fpath);
	    pressanykey();
	    return;
	}
	break;
    case ADDLINK:
	unlink(fpath);
	if (symlink(lpath, fpath) == -1) {
	    outs("無法建立 symbolic link");
	    igetch();
	    return;
	}
	break;
    }

    strlcpy(item.owner, cuser.userid, sizeof(item.owner));
    a_additem(pm, &item);
}

void
a_pasteitem(menu_t * pm, int mode)
{
    char            newpath[PATHLEN];
    char            buf[PATHLEN];
    char            ans[2], skipAll = 0, multiple = 0;
    int             i, copied = 0;
    fileheader_t    item;

    CopyQueue *cq;

    move(b_lines - 1, 0);
    if(copyqueue_querysize() <= 0)
    {
	vmsg("請先執行複製(copy)命令後再貼上(paste)");
	return;
    }
    if(mode && copyqueue_querysize() > 1)
    {
	multiple = 1;
	move(b_lines-2, 0); clrtobot();
	outs("c: 對各項目個別確認是否要貼上, z: 全部不貼，同時重設並取消全部標記\n");
	snprintf(buf, sizeof(buf),
		"確定要貼上全部共 %d 個項目嗎 (c/z/y/N)？ ", 
		copyqueue_querysize());
	getdata(b_lines - 1, 0, buf, ans, sizeof(ans), LCECHO);
	if(ans[0] == 'y')
	    skipAll = 1;
	else if(ans[0] == 'z')
	{
	    copyqueue_reset();
	    vmsg("已重設複製記錄。");
	    return;
	}
	else if (ans[0] != 'c')
	    return;
	clear();
    }
    while (copyqueue_querysize() > 0)
    {
	cq = copyqueue_gethead();
	if(!cq->copyfile[0])
	    continue;
	if(mode && multiple)
	{
	    scroll();
	    move(b_lines-2, 0); clrtobot();
	    prints("%d. %s\n", ++copied,cq->copytitle);

	}

	if (dashd(cq->copyfile)) {
	    for (i = 0; cq->copyfile[i] && cq->copyfile[i] == pm->path[i]; i++);
	    if (!cq->copyfile[i]) {
		vmsg("將目錄拷進自己的子目錄中，會造成無窮迴圈！");
		continue;
	    }
	}
	if (mode && !skipAll) {
	    snprintf(buf, sizeof(buf),
		     "確定要拷貝[%s]嗎(Y/N)？[N] ", cq->copytitle);
	    getdata(b_lines - 1, 0, buf, ans, sizeof(ans), LCECHO);
	} else
	    ans[0] = 'y';
	if (ans[0] == 'y') {
	    strlcpy(newpath, pm->path, sizeof(newpath));

	    if (*cq->copyowner) {
		char           *fname = strrchr(cq->copyfile, '/');

		if (fname)
		    strcat(newpath, fname);
		else
		    return;
		if (access(pm->path, X_OK | R_OK | W_OK))
		    mkdir(pm->path, 0755);
		memset(&item, 0, sizeof(fileheader_t));
		strlcpy(item.filename, fname + 1, sizeof(item.filename));
		memcpy(cq->copytitle, "◎", 2);
		Copy(cq->copyfile, newpath);
	    } else if (dashf(cq->copyfile)) {
		stampfile(newpath, &item);
		memcpy(cq->copytitle, "◇", 2);
                Copy(cq->copyfile, newpath);
	    } else if (dashd(cq->copyfile)) {
		stampdir(newpath, &item);
		memcpy(cq->copytitle, "◆", 2);
		copy_file(cq->copyfile, newpath);
	    } else {
		copyqueue_reset();
		vmsg("無法拷貝！");
		return;
	    }
	    strlcpy(item.owner, *cq->copyowner ? cq->copyowner : cuser.userid,
		    sizeof(item.owner));
	    strlcpy(item.title, cq->copytitle, sizeof(item.title));
	    a_additem(pm, &item);
	    cq->copyfile[0] = '\0';
	}
    }
}

static void
a_appenditem(const menu_t * pm, int isask)
{
    char            fname[PATHLEN];
    char            buf[ANSILINELEN];
    char            ans[2] = "y";
    FILE           *fp, *fin;

    move(b_lines - 1, 0);
    if(copyqueue_querysize() <= 0)
    {
	vmsg("請先執行 copy 命令後再 append");
	copyqueue_reset();
	return;
    } 
    else
    {
	CopyQueue *cq = copyqueue_gethead();

	if (dashf(cq->copyfile)) {
	    snprintf(fname, sizeof(fname), "%s/%s", pm->path,
		    pm->header[pm->now - pm->page].filename);
	    if (dashf(fname)) {
		if (isask) {
		    snprintf(buf, sizeof(buf),
			     "確定要將[%s]附加於此嗎(Y/N)？[N] ", cq->copytitle);
		    getdata(b_lines - 2, 1, buf, ans, sizeof(ans), LCECHO);
		}
		if (ans[0] == 'y') {
		    if ((fp = fopen(fname, "a+"))) {
			if ((fin = fopen(cq->copyfile, "r"))) {
			    memset(buf, '-', 74);
			    buf[74] = '\0';
			    fprintf(fp, "\n> %s <\n\n", buf);
			    if (isask)
				getdata(b_lines - 1, 1,
					"是否收錄簽名檔部份(Y/N)？[Y] ",
					ans, sizeof(ans), LCECHO);
			    while (fgets(buf, sizeof(buf), fin)) {
				if ((ans[0] == 'n') &&
				    !strcmp(buf, "--\n"))
				    break;
				fputs(buf, fp);
			    }
			    fclose(fin);
			    cq->copyfile[0] = '\0';
			}
			fclose(fp);
		    }
		}
	    } else {
		vmsg("檔案不得附加於此！");
	    }
	} else {
	    vmsg("目錄不得附加於檔案後！");
	}
    }
}

static int
a_pastetagpost(menu_t * pm, int mode)
{
    fileheader_t    fhdr;
    boardheader_t  *bh = NULL;
    int             ans = 0, ent = 0, tagnum;
    char            title[TTLEN + 1] = "◇  ";
    char            dirname[200], buf[200];

    if (TagBoard == 0){
	sethomedir(dirname, cuser.userid);
    }
    else{
	bh = getbcache(TagBoard);
	setbdir(dirname, bh->brdname);
    }
    tagnum = TagNum;

    if (!tagnum)
	return ans;

    /* since we use different tag features,
     * copyqueue is not required/used. */
    copyqueue_reset();

    while (tagnum--) {
	EnumTagFhdr(&fhdr, dirname, ent++);
	if (TagBoard == 0) 
	    sethomefile(buf, cuser.userid, fhdr.filename);
	else
	    setbfile(buf, bh->brdname, fhdr.filename);

	if (dashf(buf)) {
	    strncpy(title + 3, fhdr.title, TTLEN - 3);
	    title[TTLEN] = '\0';
	    a_copyitem(buf, title, 0, 0);
	    if (mode) {
		mode--;
		a_pasteitem(pm, 0);
	    } else
		a_appenditem(pm, 0);
	    ++ans;
	    UnTagger(tagnum);
	}
    }

    return ans;
}

static void
a_moveitem(menu_t * pm)
{
    fileheader_t   *tmp;
    char            newnum[5];
    int             num, max, min;
    char            buf[PATHLEN];
    int             fail;

    snprintf(buf, sizeof(buf), "請輸入第 %d 選項的新次序：", pm->now + 1);
    if (!getdata(b_lines - 1, 1, buf, newnum, sizeof(newnum), DOECHO))
	return;
    num = (newnum[0] == '$') ? 9999 : atoi(newnum) - 1;
    if (num >= pm->num)
	num = pm->num - 1;
    else if (num < 0)
	num = 0;
    setadir(buf, pm->path);
    min = num < pm->now ? num : pm->now;
    max = num > pm->now ? num : pm->now;
    tmp = (fileheader_t *) calloc(max + 1, FHSZ);

    fail = 0;
    if (get_records(buf, tmp, FHSZ, 1, min) != min)
	fail = 1;
    if (num > pm->now) {
	if (get_records(buf, &tmp[min], FHSZ, pm->now + 2, max - min) != max - min)
	    fail = 1;
	if (get_records(buf, &tmp[max], FHSZ, pm->now + 1, 1) != 1)
	    fail = 1;
    } else {
	if (get_records(buf, &tmp[min], FHSZ, pm->now + 1, 1) != 1)
	    fail = 1;
	if (get_records(buf, &tmp[min + 1], FHSZ, num + 1, max - min) != max - min)
	    fail = 1;
    }
    if (!fail)
	substitute_record(buf, tmp, FHSZ * (max + 1), 1);
    pm->now = num;
    free(tmp);
}

static void
a_delrange(menu_t * pm)
{
    char            fname[PATHLEN];

    snprintf(fname, sizeof(fname), "%s/.DIR", pm->path);
    del_range(0, NULL, fname);
    pm->num = get_num_records(fname, FHSZ);
}

static void
a_delete(menu_t * pm)
{
    char            fpath[PATHLEN], buf[PATHLEN], cmd[PATHLEN];
    char            ans[4];
    fileheader_t    backup;

    snprintf(fpath, sizeof(fpath),
	     "%s/%s", pm->path, pm->header[pm->now - pm->page].filename);
    setadir(buf, pm->path);

    if (pm->header[pm->now - pm->page].filename[0] == 'H' &&
	pm->header[pm->now - pm->page].filename[1] == '.') {
	getdata(b_lines - 1, 1, "您確定要刪除此精華區連線嗎(Y/N)？[N] ",
		ans, sizeof(ans), LCECHO);
	if (ans[0] != 'y')
	    return;
	if (delete_record(buf, FHSZ, pm->now + 1) == -1)
	    return;
    } else if (dashl(fpath)) {
	getdata(b_lines - 1, 1, "您確定要刪除此 symbolic link 嗎(Y/N)？[N] ",
		ans, sizeof(ans), LCECHO);
	if (ans[0] != 'y')
	    return;
	if (delete_record(buf, FHSZ, pm->now + 1) == -1)
	    return;
	unlink(fpath);
    } else if (dashf(fpath)) {
	getdata(b_lines - 1, 1, "您確定要刪除此檔案嗎(Y/N)？[N] ", ans,
		sizeof(ans), LCECHO);
	if (ans[0] != 'y')
	    return;
	if (delete_record(buf, FHSZ, pm->now + 1) == -1)
	    return;

	setbpath(buf, "deleted");
	stampfile(buf, &backup);
	strlcpy(backup.owner, cuser.userid, sizeof(backup.owner));
	strlcpy(backup.title,
		pm->header[pm->now - pm->page].title + 2,
		sizeof(backup.title));

	snprintf(cmd, sizeof(cmd),
		 "mv -f %s %s", fpath, buf);
	system(cmd);
	setbdir(buf, "deleted");
	append_record(buf, &backup, sizeof(backup));
    } else if (dashd(fpath)) {
	getdata(b_lines - 1, 1, "您確定要刪除整個目錄嗎(Y/N)？[N] ", ans,
		sizeof(ans), LCECHO);
	if (ans[0] != 'y')
	    return;
	if (delete_record(buf, FHSZ, pm->now + 1) == -1)
	    return;

	setapath(buf, "deleted");
	stampdir(buf, &backup);

	snprintf(cmd, sizeof(cmd),
		 "rm -rf %s;/bin/mv -f %s %s", buf, fpath, buf);
	system(cmd);

	strlcpy(backup.owner, cuser.userid, sizeof(backup.owner));
        strcpy(backup.title, "◆");
	strlcpy(backup.title + 2,
		pm->header[pm->now - pm->page].title + 2,
		sizeof(backup.title) - 3);

	/* merge setapath(buf, "deleted"); setadir(buf, buf); */
	snprintf(buf, sizeof(buf), "man/boards/%c/%s/.DIR",
		 'd', "deleted");
	append_record(buf, &backup, sizeof(backup));
    } else {			/* Ptt 損毀的項目 */
	getdata(b_lines - 1, 1, "您確定要刪除此損毀的項目嗎(Y/N)？[N] ",
		ans, sizeof(ans), LCECHO);
	if (ans[0] != 'y')
	    return;
	if (delete_record(buf, FHSZ, pm->now + 1) == -1)
	    return;
    }
    pm->num--;
}

static void
a_newtitle(const menu_t * pm)
{
    char            buf[PATHLEN];
    fileheader_t    item;

    memcpy(&item, &pm->header[pm->now - pm->page], FHSZ);
    strlcpy(buf, item.title + 3, sizeof(buf));
    if (getdata_buf(b_lines - 1, 1, "新標題：", buf, 60, DOECHO)) {
	strlcpy(item.title + 3, buf, sizeof(item.title) - 3);
	setadir(buf, pm->path);
	substitute_record(buf, &item, FHSZ, pm->now + 1);
    }
}
static void
a_hideitem(const menu_t * pm)
{
    fileheader_t   *item = &pm->header[pm->now - pm->page];
    char            buf[PATHLEN];
    if (item->filemode & FILE_BM) {
	item->filemode &= ~FILE_BM;
	item->filemode &= ~FILE_HIDE;
    } else if (item->filemode & FILE_HIDE)
	item->filemode |= FILE_BM;
    else
	item->filemode |= FILE_HIDE;
    setadir(buf, pm->path);
    substitute_record(buf, item, FHSZ, pm->now + 1);
}
static void
a_editsign(const menu_t * pm)
{
    char            buf[PATHLEN];
    fileheader_t    item;

    memcpy(&item, &pm->header[pm->now - pm->page], FHSZ);
    snprintf(buf, sizeof(buf), "%c%c", item.title[0], item.title[1]);
    if (getdata_buf(b_lines - 1, 1, "符號", buf, 5, DOECHO)) {
	item.title[0] = buf[0] ? buf[0] : ' ';
	item.title[1] = buf[1] ? buf[1] : ' ';
	item.title[2] = buf[2] ? buf[2] : ' ';
	setadir(buf, pm->path);
	substitute_record(buf, &item, FHSZ, pm->now + 1);
    }
}

static void
a_showname(const menu_t * pm)
{
    char            buf[PATHLEN];
    int             len;
    int             i;
    int             sym;

    move(b_lines - 1, 0);
    snprintf(buf, sizeof(buf),
	     "%s/%s", pm->path, pm->header[pm->now - pm->page].filename);
    if (dashl(buf)) {
	prints("此 symbolic link 名稱為 %s\n",
	       pm->header[pm->now - pm->page].filename);
	if ((len = readlink(buf, buf, PATHLEN - 1)) >= 0) {
	    buf[len] = '\0';
	    for (i = 0; BBSHOME[i] && buf[i] == BBSHOME[i]; i++);
	    if (!BBSHOME[i] && buf[i] == '/') {
		if (HasUserPerm(PERM_BBSADM))
		    sym = 1;
		else {
		    sym = 0;
		    for (i++; BBSHOME "/man"[i] && buf[i] == BBSHOME "/man"[i];
			 i++);
		    if (!BBSHOME "/man"[i] && buf[i] == '/')
			sym = 1;
		}
		if (sym) {
		    vmsgf("此 symbolic link 指向 %s\n", &buf[i + 1]);
		}
	    }
	}
    } else if (dashf(buf))
	prints("此文章名稱為 %s", pm->header[pm->now - pm->page].filename);
    else if (dashd(buf))
	prints("此目錄名稱為 %s", pm->header[pm->now - pm->page].filename);
    else
	outs("此項目已損毀, 建議將其刪除！");
    pressanykey();
}

static int
isvisible_man(const menu_t * me)
{
    fileheader_t   *fhdr = &me->header[me->now - me->page];
    if (me->level < MANAGER && ((fhdr->filemode & FILE_BM) ||
				((fhdr->filemode & FILE_HIDE) &&
				 /* board friend only effact when
				  * in board reading mode             */
				 (currstat == ANNOUNCE ||
				  hbflcheck(currbid, currutmp->uid))
				)))
	return 0;
    return 1;
}
int
a_menu(const char *maintitle, const char *path, int lastlevel, char *trans_buffer)
{
    static char     Fexit;	// 用來跳出 recursion
    menu_t          me;
    char            fname[PATHLEN];
    int             ch, returnvalue = FULLUPDATE;

    if(trans_buffer)
	trans_buffer[0] = '\0';

    Fexit = 0;
    me.header_size = p_lines;
    me.header = (fileheader_t *) calloc(me.header_size, FHSZ);
    me.path = path;
    strlcpy(me.mtitle, maintitle, sizeof(me.mtitle));
    setadir(fname, me.path);
    me.num = get_num_records(fname, FHSZ);

    /* 精華區-tree 中部份結構屬於 cuser ==> BM */

    if (!(me.level = lastlevel)) {
	char           *ptr;

	if ((ptr = strrchr(me.mtitle, '[')))
	    me.level = is_BM(ptr + 1);
    }
    me.page = 9999;
    me.now = 0;
    for (;;) {
	if (me.now >= me.num)
	    me.now = me.num - 1;
	if (me.now < 0)
	    me.now = 0;

	if (me.now < me.page || me.now >= me.page + me.header_size) {
	    me.page = me.now - ((me.page == 10000 && me.now > p_lines / 2) ?
				(p_lines / 2) : (me.now % p_lines));
	    a_showmenu(&me);
	}
	ch = cursor_key(2 + me.now - me.page, 0);

	if (ch == 'q' || ch == 'Q' || ch == KEY_LEFT)
	    break;

	if (ch >= '1' && ch <= '9') {
	    if ((ch = search_num(ch, me.num)) != -1)
		me.now = ch;
	    me.page = 10000;
	    continue;
	}
	switch (ch) {
	case KEY_UP:
	case 'k':
	    if (--me.now < 0)
		me.now = me.num - 1;
	    break;

	case KEY_DOWN:
	case 'j':
	    if (++me.now >= me.num)
		me.now = 0;
	    break;

	case KEY_PGUP:
	case Ctrl('B'):
	    if (me.now >= p_lines)
		me.now -= p_lines;
	    else if (me.now > 0)
		me.now = 0;
	    else
		me.now = me.num - 1;
	    break;

	case ' ':
	case KEY_PGDN:
	case Ctrl('F'):
	    if (me.now < me.num - p_lines)
		me.now += p_lines;
	    else if (me.now < me.num - 1)
		me.now = me.num - 1;
	    else
		me.now = 0;
	    break;

	case '0':
	    me.now = 0;
	    break;
	case '?':
	case '/':
	    me.now = a_searchtitle(&me, ch == '?');
	    me.page = 9999;
	    break;
	case '$':
	    me.now = me.num - 1;
	    break;
	case 'h':
	    a_showhelp(me.level);
	    me.page = 9999;
	    break;

	case Ctrl('I'):
	    t_idle();
	    me.page = 9999;
	    break;

	case 'e':
	case 'E':
	    snprintf(fname, sizeof(fname),
		     "%s/%s", path, me.header[me.now - me.page].filename);
	    if (dashf(fname) && me.level >= MANAGER) {
		*quote_file = 0;
		if (vedit(fname, NA, NULL) != -1) {
		    char            fpath[200];
		    fileheader_t    fhdr;

		    strlcpy(fpath, path, sizeof(fpath));
		    stampfile(fpath, &fhdr);
		    unlink(fpath);
		    Rename(fname, fpath);
		    strlcpy(me.header[me.now - me.page].filename,
			    fhdr.filename,
			    sizeof(me.header[me.now - me.page].filename));
		    strlcpy(me.header[me.now - me.page].owner,
			    cuser.userid,
			    sizeof(me.header[me.now - me.page].owner));
		    setadir(fpath, path);
		    substitute_record(fpath, me.header + me.now - me.page,
				      sizeof(fhdr), me.now + 1);
		}
		me.page = 9999;
	    }
	    break;

	case 't':
	case 'c':
	    if (me.now < me.num) {
		if (!isvisible_man(&me))
		    break;

		snprintf(fname, sizeof(fname), "%s/%s", path,
			 me.header[me.now - me.page].filename);

		/* XXX: dirty fix
		   應該要改成如果發現該目錄裡面有隱形目錄的話才拒絕.
		   不過這樣的話須要整個搜一遍, 而且目前判斷該資料是目錄
		   還是檔案竟然是用 fstat(2) 而不是直接存在 .DIR 內 |||b
		   須等該資料寫入 .DIR 內再 implement才有效率.
		 */
		if( !lastlevel && !HasUserPerm(PERM_SYSOP) &&
		    !is_BM_cache(currbid) && dashd(fname) )
		    vmsg("只有板主才可以拷貝目錄唷!");
		else
		    a_copyitem(fname, me.header[me.now - me.page].title, 0, 1);
		me.page = 9999;
		/* move down */
		if (++me.now >= me.num)
		    me.now = 0;
		break;
	    }
	case '\n':
	case '\r':
	case KEY_RIGHT:
	case 'r':
	    if (me.now < me.num) {
		fileheader_t   *fhdr = &me.header[me.now - me.page];
		if (!isvisible_man(&me))
		    break;
#ifdef DEBUG
		vmsgf("%s/%s", &path[11], fhdr->filename);;
#endif
		snprintf(fname, sizeof(fname), "%s/%s", path, fhdr->filename);
		if (*fhdr->filename == 'H' && fhdr->filename[1] == '.') {
		  vmsg("不再支援 gopher mode, 請使用瀏覽器直接瀏覽");
		  vmsgf("gopher://%s/1/",fhdr->filename+2);
		} else if (dashf(fname)) {
		    int             more_result;

		    while ((more_result = more(fname, YEA))) {
			/* Ptt 範本精靈 plugin */
			if (trans_buffer && 
				(currstat == EDITEXP || currstat == OSONG)) {
			    char            ans[4];

			    move(22, 0);
			    clrtoeol();
			    getdata(22, 1,
				    currstat == EDITEXP ?
				    "要把範例 Plugin 到文章嗎?[y/N]" :
				    "確定要點這首歌嗎?[y/N]",
				    ans, sizeof(ans), LCECHO);
			    if (ans[0] == 'y') {
				strlcpy(trans_buffer, fname, PATHLEN);
				Fexit = 1;
				if (currstat == OSONG) {
				    log_file(FN_USSONG, LOG_CREAT | LOG_VF,
					     "%s\n", fhdr->title);
				}
				free(me.header);
				return FULLUPDATE;
			    }
			}
			if (more_result == READ_PREV) {
			    if (--me.now < 0) {
				me.now = 0;
				break;
			    }
			} else if (more_result == READ_NEXT) {
			    if (++me.now >= me.num) {
				me.now = me.num - 1;
				break;
			    }
			    /* we only load me.header_size pages */
			    if (me.now - me.page >= me.header_size)
				break;
			} else
			    break;
			if (!isvisible_man(&me))
			    break;
			snprintf(fname, sizeof(fname), "%s/%s", path,
				 me.header[me.now - me.page].filename);
			if (!dashf(fname))
			    break;
		    }
		} else if (dashd(fname)) {
		    a_menu(me.header[me.now - me.page].title, fname, me.level, trans_buffer);
		    /* Ptt  強力跳出recursive */
		    if (Fexit) {
			free(me.header);
			return FULLUPDATE;
		    }
		}
		me.page = 9999;
	    }
	    break;

	case 'F':
	case 'U':
	    if (me.now < me.num) {
                fileheader_t   *fhdr = &me.header[me.now - me.page];
                if (!isvisible_man(&me))
                    break;
		snprintf(fname, sizeof(fname),
			 "%s/%s", path, fhdr->filename);
		if (HasUserPerm(PERM_LOGINOK) && dashf(fname)) {
		    a_forward(path, fhdr, ch /* == 'U' */ );
		    /* By CharlieL */
		} else
		    vmsg("無法轉寄此項目");
		me.page = 9999;
	    }

	    break;

#ifdef BLOG
	case 'b':
	    if( !HasUserPerm(PERM_SYSOP) && !is_BM_cache(currbid) )
		vmsg("只有板主才可以用唷!");
	    else{
		char    genbuf[128];
		snprintf(genbuf, sizeof(genbuf),
			 "bin/builddb.pl -f -n %d %s", me.now, currboard);
		system(genbuf);
		vmsg("資料更新完成");
	    }
	    me.page = 9999;
	    break;

	case 'B':
	    if( !HasUserPerm(SYSOP) && !is_BM_cache(currbid) )
		vmsg("只有板主才可以用唷!");
	    else
		BlogMain(me.now);
	    me.page = 9999;

	    break;
#endif
	}

	if (me.level >= MANAGER) {
	    int             page0 = me.page;

	    switch (ch) {
	    case 'n':
		a_newitem(&me, ADDITEM);
		me.page = 9999;
		break;
	    case 'g':
		a_newitem(&me, ADDGROUP);
		me.page = 9999;
		break;
	    case 'p':
		a_pasteitem(&me, 1);
		me.page = 9999;
		break;
	    case 'f':
		a_editsign(&me);
		me.page = 9999;
		break;
	    case Ctrl('P'):
		a_pastetagpost(&me, -1);
		returnvalue = DIRCHANGED;
		me.page = 9999;
		break;
	    case Ctrl('A'):
		a_pastetagpost(&me, 1);
		returnvalue = DIRCHANGED;
		me.page = 9999;
		break;
	    case 'a':
		a_appenditem(&me, 1);
		me.page = 9999;
		break;
	    default:
		me.page = page0;
		break;
	    }

	    if (me.num)
		switch (ch) {
		case 'm':
		    a_moveitem(&me);
		    me.page = 9999;
		    break;

		case 'D':
		    /* Ptt me.page = -1; */
		    a_delrange(&me);
		    me.page = 9999;
		    break;
		case 'd':
		    a_delete(&me);
		    me.page = 9999;
		    break;
		case 'H':
		    a_hideitem(&me);
		    me.page = 9999;
		    break;
		case 'T':
		    a_newtitle(&me);
		    me.page = 9999;
		    break;
		}
	}
	if (me.level == SYSOP) {
	    switch (ch) {
	    case 'l':
		a_newitem(&me, ADDLINK);
		me.page = 9999;
		break;
	    case 'N':
		a_showname(&me);
		me.page = 9999;
		break;
	    }
	}
    }
    free(me.header);
    return returnvalue;
}

int
Announce(void)
{
    setutmpmode(ANNOUNCE);
    a_menu(BBSNAME "佈告欄", "man",
	   ((HasUserPerm(PERM_SYSOP) ) ? SYSOP : NOBODY), 
	   NULL);
    return 0;
}

#ifdef BLOG
#include <mysql/mysql.h>
void BlogMain(int num)
{
    int     oldmode = currutmp->mode;
    char    genbuf[128], exit = 0;

    //setutmpmode(BLOGGING); /* will crash someone using old program  */
    sprintf(genbuf, "%s的部落格", currboard);
    showtitle("部落格", genbuf);
    while( !exit ){
	move(1, 0);
	outs("請選擇您要執行的重作:\n"
	       "0.回到上一層\n"
	       "1.製作部落格樣板格式\n"
	       "  使用新的 config 目錄下樣板資料\n"
	       "  通常在第一次使用部落格或樣板更新的時候使用\n"
	       "\n"
	       "2.重新製作部落格\n"
	       "  只在部落格資料整個亂掉的時候才使用\n"
	       "\n"
	       "3.將本文加入部落格\n"
	       "  將游標所在位置的文章加入部落格\n"
	       "\n"
	       "4.刪除迴響\n"
	       "\n"
	       "5.刪除一篇部落格\n"
	       "\n"
	       "C.建立部落格目錄 (您只有第一次時需要)\n"
	       );
	switch( getans("請選擇(0-5,C)？[0]") ){
	case '1':
	    snprintf(genbuf, sizeof(genbuf),
		     "bin/builddb.pl -c %s", currboard);
	    system(genbuf);
	    break;
	case '2':
	    snprintf(genbuf, sizeof(genbuf),
		     "bin/builddb.pl -a %s", currboard);
	    system(genbuf);
	    break;
	case '3':
	    snprintf(genbuf, sizeof(genbuf),
		     "bin/builddb.pl -f -n %d %s", num, currboard);
	    system(genbuf);
	    break;
	case '4':{
	    char    hash[33];
	    int     i;
	    getdata(16, 0, "請輸入該篇的雜湊值: ",
		    hash, sizeof(hash), DOECHO);
	    for( i = 0 ; hash[i] != 0 ; ++i ) /* 前面用 getdata() 保證有 \0 */
		if( !islower(hash[i]) && !isdigit(hash[i]) )
		    break;
	    if( i != 32 ){
		vmsg("輸入錯誤");
		break;
	    }
	    if( hash[0] != 0 && 
		getans("請確定刪除(Y/N)?[N] ") == 'y' ){
		MYSQL   mysql;
		char    cmd[256];
		
		snprintf(cmd, sizeof(cmd), "delete from comment where "
			"hash='%s'&&brdname='%s'", hash, currboard);
#ifdef DEBUG
		vmsg(cmd);
#endif
		if( !(!mysql_init(&mysql) ||
		      !mysql_real_connect(&mysql, BLOGDB_HOST, BLOGDB_USER,
					  BLOGDB_PASSWD, BLOGDB_DB, 
					  BLOGDB_PORT, BLOGDB_SOCK, 0) ||
		      mysql_query(&mysql, cmd)) )
		    vmsg("資料刪除完成");
		else
		    vmsg(
#ifdef DEBUG
			 mysql_error(&mysql)
#else
			 "database internal error"
#endif
			 );
		mysql_close(&mysql);
	    }
	}
	    break;
	    
	case '5': {
	    char    date[9];
	    int     i;
	    getdata(16, 0, "請輸入該篇的日期(yyyymmdd): ",
		    date, sizeof(date), DOECHO);
	    for( i = 0 ; i < 9 ; ++i )
		if( !isdigit(date[i]) )
		    break;
	    if( i != 8 ){
		vmsg("輸入錯誤");
		break;
	    }
	    snprintf(genbuf, sizeof(genbuf),
		     "bin/builddb.pl -D %s %s", date, currboard);
	    system(genbuf);
	}
	    break;

	case 'C': case 'c': {
	    fileheader_t item;
	    char    fpath[PATHLEN], adir[PATHLEN], buf[256];
	    setapath(fpath, currboard);
	    stampdir(fpath, &item);
	    strlcpy(item.title, "◆ Blog", sizeof(item.title));
	    strlcpy(item.owner, cuser.userid, sizeof(item.owner));

	    setapath(adir, currboard);
	    strcat(adir, "/.DIR");
	    append_record(adir, &item, FHSZ);

	    snprintf(buf, sizeof(buf),
		     "cp -R etc/Blog.Default/.DIR etc/Blog.Default/* %s/",
		     fpath);
	    system(buf);

	    more("etc/README.BLOG", YEA);
	}
	    break;

	default:
	    exit = 1;
	    break;
	}
	if( !exit )
	    vmsg("部落格完成");
    }
    currutmp->mode = oldmode;
    pressanykey();
}
#endif
