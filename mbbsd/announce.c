/* $Id$ */
#include "bbs.h"

// XXX piaip 2007/12/29
// 最近發現很多 code 都死在 announce
// 因為進來要看 lastlevel 而非 currbid
// user 可能一進 BBS 直殺郵件->mail_cite->進精華區
// 於是就爆炸 
// 同理 currboard 也不該用
// 請改用 me.bid (注意 me.bid 可能為 0, 表示進來的非看板。)

// for max file size limitation here, see edit.c
#define MAX_FILE_SIZE (32768*1024)

// used to force a page refresh. 
// TODO change this to INT_MAX in future, when we understand what is the magic 10000 value.
#define A_INVALID_PAGE (9999)

// zindex code is in the format "z-1-2-3-4" or "1. 2. 3. 4"
// for safety reason, we expect num to be < 10 digits.
// 9 + 1 ('-') + 3 ('...') = 13
// however to make the prompt bar smaller, we'd reduce few more bytes.
#define SAFE_ZINDEX_CODELEN (STRLEN-15)

#ifndef _DIM
#define _DIM(x) (sizeof(x)/sizeof(x[0]))
#endif

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
	vmsg("[注意] 提醒您複製/標記後要貼上(p)或附加(a)後才能刪除原文!");
	flFirstAlert = 0;
    }
}

#define FHSZ            sizeof(fileheader_t)

static int
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

    // if len < 0, the directory is not valid anymore.
    if (len < 0)
	return 0;

    if (len < pm->header_size)
	bzero(&pm->header[len], FHSZ * (pm->header_size - len));
    return 1;
}

static void
a_timestamp(char *buf, const time4_t *time)
{
    struct tm pt;

    localtime4_r(time, &pt);
    sprintf(buf, "%02d/%02d/%02d", pt.tm_mon + 1, pt.tm_mday, (pt.tm_year + 1900) % 100);
}

static int
a_showmenu(menu_t * pm)
{
    char           *title, *editor;
    int             n;
    fileheader_t   *item;
    time4_t         dtime;

    showtitle("精華文章", pm->mtitle);
    prints("   " ANSI_COLOR(1;36) "編號    標      題%56s" ANSI_COLOR(0),
	   "編    選      日    期");

    if (!pm->num)
    {
	outs("\n  《精華區》尚在吸取天地間的日月精華中... :)");
    }
    else
    {
	char buf[PATHLEN];

	// determine if path is valid.
	if (!a_loadname(pm))
	    return 0;

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
    }

    move(b_lines, 0);
    if(copyqueue_querysize() > 0)
    {		// something in queue
	char buf[STRLEN];
	snprintf(buf, sizeof(buf),  "【已標記(複製) %d 項】", copyqueue_querysize());
	vs_footer(buf, pm->level == 0 ?
		" (c)標記/複製 - 無管理權限，無法貼上 " :
		" (c)標記/複製 (p)貼上/取消/重設標記 (a)附加至文章後\t(q/←)離開 (h)說明");
    } 
    else if(pm->level)
    {		// BM
	vs_footer(" 【板  主】 ",
		" (n)新增文章 (g)新增目錄 (e)編輯檔案\t(q/←)離開 (h)說明");
    }
    else
    {		// normal user
	vs_footer(" 【功\能鍵】 ",
		" (k↑j↓)移動游標 (enter/→)讀取資料\t(q/←)離開 (h)說明");
    }
    return 1;
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

	    if (!a_loadname(pm))
		return pm->now;
	}
	if (DBCS_strcasestr(pm->header[pos - pm->page].title, search_str))
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
	 "[^W]            我在哪裡\n"
	 "[F][U]          將文章寄回 Internet 郵箱/"
	 "將文章 uuencode 後寄回郵箱\n");
    if (level >= MANAGER) {
	outs("\n" ANSI_COLOR(36) "【 板主專用鍵 】" ANSI_RESET "\n"
	     "[H]             切換為 公開/可見會員名單/板主 才能閱\讀\n"
	     "[n/g]           收錄精華文章/開闢目錄\n"
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

static void
a_newitem(menu_t * pm, int mode)
{
    char    *mesg[3] = {
	"[新增文章] 請輸入標題：",	/* ADDITEM */
	"[新增目錄] 請輸入標題：",	/* ADDGROUP */
    };

    char            fpath[PATHLEN];
    fileheader_t    item;

    strlcpy(fpath, pm->path, sizeof(fpath));
    if (strlen(pm->path) + FNLEN*2 >= PATHLEN)
	return;

    switch (mode) {
    case ADDITEM:
	stampfile(fpath, &item);
	strlcpy(item.title, "◇ ", sizeof(item.title));	/* A1BA */
	break;

    case ADDGROUP:
	if (stampadir(fpath, &item, 0) == -1)
	{
	    vmsg("抱歉，無法在本層建立新目錄。");
	    return;
	}
	strlcpy(item.title, "◆ ", sizeof(item.title));	/* A1BB */
	break;
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
	{
	    int edflags = 0;
# ifdef BN_BBSMOVIE
	    if (pm && pm->bid && 
		strcmp(getbcache(pm->bid)->brdname, 
			BN_BBSMOVIE) == 0)
	    {
		edflags |= EDITFLAG_UPLOAD;
		edflags |= EDITFLAG_ALLOWLARGE;
	    }
# endif // BN_BBSMOVIE
	    if (vedit2(fpath, 0, NULL, NULL, edflags) == -1) {
		unlink(fpath);
		pressanykey();
		return;
	    }
	}
	break;
    case ADDGROUP:
	// do nothing
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
		    Mkdir(pm->path);
		memset(&item, 0, sizeof(fileheader_t));
		strlcpy(item.filename, fname + 1, sizeof(item.filename));
		memcpy(cq->copytitle, "◎", 2);
		Copy(cq->copyfile, newpath);
	    } else if (dashf(cq->copyfile)) {
		stampfile(newpath, &item);
		memcpy(cq->copytitle, "◇", 2);
                Copy(cq->copyfile, newpath);
	    } else if (dashd(cq->copyfile) &&
		       stampadir(newpath, &item, 0) != -1) {
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
	off_t sz;

	if (!dashf(cq->copyfile)) {
	    vmsg("目錄不得附加於檔案後！");
	    return;
	}

	snprintf(fname, sizeof(fname), "%s/%s", pm->path,
		pm->header[pm->now - pm->page].filename);

	// if same file, abort.
	if (!dashf(fname) || strcmp(fname, cq->copyfile) == 0) {
	    vmsg("檔案不得附加於此！");
	    return;
	}

	// fname = destination
	sz = dashs(fname);
	if (sz >= MAX_FILE_SIZE)
	{
	    vmsg("檔案已超過最大限制，無法再附加");
	    return;
	}

	if (isask) {
	    snprintf(buf, sizeof(buf),
		    "確定要將[%s]附加於此嗎(Y/N)？[N] ", cq->copytitle);
	    getdata(b_lines - 2, 1, buf, ans, sizeof(ans), LCECHO);
	}

	if (ans[0] != 'y' || !(fp = fopen(fname, "a+")))
	    return;

	if (!(fin = fopen(cq->copyfile, "r"))) {
	    fclose(fp);
	    return;
	}
	// cq->copyfile = input
	sz = dashs(cq->copyfile);

	memset(buf, '-', 74);
	buf[74] = '\0';
	fprintf(fp, "\n> %s <\n\n", buf);
	if (isask)
	    getdata(b_lines - 1, 1,
		    "是否收錄簽名檔部份(Y/N)？[Y] ",
		    ans, sizeof(ans), LCECHO);

	// XXX reported by Kinra, appending same file may cause endless loop here.
	// we check file name at prior steps and do file size check here.
	while (sz > 0 && fgets(buf, sizeof(buf), fin)) {
	    sz -= strlen(buf);
	    if ((ans[0] == 'n') &&
		    !strcmp(buf, "--\n"))
		break;
	    fputs(buf, fp);
	}
	fclose(fin);
	fclose(fp);
	cq->copyfile[0] = '\0';
    }
}

static int
a_pastetagpost(menu_t * pm, int mode)
{
    fileheader_t    fhdr;
    boardheader_t  *bh = NULL;
    int             ans = 0, ent = 0, tagnum;
    char            title[TTLEN + 1] = "◇  ";
    char            dirname[PATHLEN], buf[PATHLEN];

    if (TagBoard == 0){
	sethomedir(dirname, cuser.userid);
    }
    else{
	bh = getbcache(TagBoard);
	setbdir(dirname, bh->brdname);
    }
    tagnum = TagNum;

    // prevent if anything wrong
    if (tagnum > MAXTAGS || tagnum < 0)
    {
	vmsg("內部錯誤。請把你剛剛進行的完整步驟貼到 "
		BN_BUGREPORT " 板。");
	return ans;
    }

    if (tagnum < 1)
	return ans;

    /* since we use different tag features,
     * copyqueue is not required/used. */
    copyqueue_reset();

    while (tagnum-- > 0) {
	memset(&fhdr, 0, sizeof(fhdr));
	EnumTagFhdr(&fhdr, dirname, ent++);

	// XXX many process crashed here as fhdr.filename[0] == 0
	// let's workaround it? or trace?
	// if (!fhdr->filename[0])
        //   continue;
	
	if (!fhdr.filename[0])
	{
	    grayout(0, b_lines-2, GRAYOUT_DARK);
	    move(b_lines-1, 0); clrtobot();
	    prints("第 %d 項處理發生錯誤。 請把你剛剛進行的完整步驟貼到 "
		    BN_BUGREPORT " 板。\n", ent);
	    vmsg("忽略錯誤並繼續進行。");
	    continue;
	}

	if (TagBoard == 0) 
	    sethomefile(buf, cuser.userid, fhdr.filename);
	else
	    setbfile(buf, bh->brdname, fhdr.filename);

	if (dashf(buf)) {
	    strlcpy(title + 3, fhdr.title, sizeof(title) - 3);
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
    num = (newnum[0] == '$') ? A_INVALID_PAGE : atoi(newnum) - 1;
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

    snprintf(fname, sizeof(fname), "%s/" FN_DIR, pm->path);
    del_range(0, NULL, fname);
    pm->num = get_num_records(fname, FHSZ);
}

static void
a_delete(menu_t * pm)
{
    char            fpath[PATHLEN], buf[PATHLEN], cmd[PATHLEN];
    char            ans[4];
    fileheader_t    backup, *fhdr = &(pm->header[pm->now - pm->page]);

    snprintf(fpath, sizeof(fpath),
	     "%s/%s", pm->path, fhdr->filename);
    setadir(buf, pm->path);

    if (fhdr->filename[0] == 'H' && fhdr->filename[1] == '.') {
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

	// XXX we also check PERM_MAILLIMIT here because RMAIL
	// may be not trusted...
	const char *save_bn = ( HasUserPerm(PERM_MAILLIMIT) && (currstat & RMAIL) ) ?
		BN_JUNK : BN_DELETED;

	getdata(b_lines - 1, 1, "您確定要刪除此檔案嗎(Y/N)？[N] ", ans,
		sizeof(ans), LCECHO);
	if (ans[0] != 'y')
	    return;
	if (delete_record(buf, FHSZ, pm->now + 1) == -1)
	    return;


	setbpath(buf, save_bn);
	stampfile(buf, &backup);
	strlcpy(backup.owner, cuser.userid, sizeof(backup.owner));
	strlcpy(backup.title, fhdr->title + 2, sizeof(backup.title));

	snprintf(cmd, sizeof(cmd),
		"mv -f %s %s", fpath, buf);
	system(cmd);
	setbdir(buf, save_bn);
	append_record(buf, &backup, sizeof(backup));
	setbtotal(getbnum(save_bn));

    } else if (dashd(fpath)) {

	// XXX we also check PERM_MAILLIMIT here because RMAIL
	// may be not trusted...
	const char *save_bn = ( HasUserPerm(PERM_MAILLIMIT) && (currstat & RMAIL) ) ?
		BN_JUNK : BN_DELETED;

	getdata(b_lines - 1, 1, "您確定要刪除整個目錄嗎(Y/N)？[N] ", ans,
		sizeof(ans), LCECHO);
	if (ans[0] != 'y')
	    return;
	if (delete_record(buf, FHSZ, pm->now + 1) == -1)
	    return;

	setapath(buf, save_bn);
	// XXX because this directory will hold folders from entire site,
	// let's allow it to use a large set of file names.
	if (stampadir(buf, &backup, 1) != 0)
	{
	    vmsg("抱歉，系統目前無法刪除資料，請通知站務人員");
	    return;
	}

	snprintf(cmd, sizeof(cmd),
		"rm -rf %s;/bin/mv -f %s %s", buf, fpath, buf);
	system(cmd);

	strlcpy(backup.owner, cuser.userid, sizeof(backup.owner));
	strcpy(backup.title, "◆");
	strlcpy(backup.title + 2, fhdr->title + 2, sizeof(backup.title) - 3);

        // restrict access if source is hidden
        if ((fhdr->filemode & FILE_BM) || (fhdr->filemode & FILE_HIDE))
            backup.filemode |= FILE_BM;

	/* merge setapath(buf, save_bn); setadir(buf, buf); */
	snprintf(buf, sizeof(buf), "man/boards/%c/%s/" FN_DIR,
		*save_bn, save_bn);
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
    if (getdata_buf(b_lines - 1, 0, "   新標題: ", buf, 60, DOECHO)) {
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
    if (getdata_buf(b_lines - 1, 1, "符號", buf, 3, DOECHO)) {
	item.title[0] = buf[0] ? buf[0] : ' ';
	item.title[1] = buf[1] ? buf[1] : ' ';
	item.title[2] = ' ';
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
#ifdef CHESSCOUNTRY
static void
a_setchesslist(const menu_t * me)
{
    char buf[4];
    char buf_list[PATHLEN];
    char buf_photo[PATHLEN];
    char buf_this[PATHLEN];
    char buf_real[PATHLEN];
    int list_exist, photo_exist;
    fileheader_t* fhdr = me->header + me->now - me->page;
    int n;

    snprintf(buf_this,  sizeof(buf_this),  "%s/%s", me->path, fhdr->filename);
    if((n = readlink(buf_this, buf_real, sizeof(buf_real) - 1)) == -1)
	strcpy(buf_real, fhdr->filename);
    else
	// readlink doesn't garentee zero-ended
	buf_real[n] = 0;

    if (strcmp(buf_real, "chess_list") == 0
	    || strcmp(buf_real, "chess_photo") == 0) {
	vmsg("不需重設！");
	return;
    }

    snprintf(buf_list,  sizeof(buf_list),  "%s/chess_list",  me->path);
    snprintf(buf_photo, sizeof(buf_photo), "%s/chess_photo", me->path);

    list_exist = dashf(buf_list);
    photo_exist = dashd(buf_photo);

    if (!list_exist && !photo_exist) {
	vmsg("此看板非棋國！");
	return;
    }

    getdata(b_lines, 0, "將此項目設定為 (1) 棋國名單 (2) 棋國照片檔目錄：",
	    buf, sizeof(buf), 1);
    if (buf[0] == '1') {
	if (list_exist)
	    getdata(b_lines, 0, "原有之棋國名單將被取代，請確認 (y/N)",
		    buf, sizeof(buf), 1);
	else
	    buf[0] = 'y';

	if (buf[0] == 'y' || buf[0] == 'Y') {
	    Rename(buf_this, buf_list);
	    symlink("chess_list", buf_this);
	}
    } else if (buf[0] == '2') {
	if (photo_exist)
	    getdata(b_lines, 0, "原有之棋國照片將被取代，請確認 (y/N)",
		    buf, sizeof(buf), 1);
	else
	    buf[0] = 'y';

	if (buf[0] == 'y' || buf[0] == 'Y') {
	    if(strncmp(buf_photo, "man/boards/", 11) == 0 && // guarding
		    buf_photo[11] && buf_photo[12] == '/' && // guarding
		    snprintf(buf_list, sizeof(buf_list), "rm -rf %s", buf_photo)
		    == strlen(buf_photo) + 7)
		system(buf_list);
	    Rename(buf_this, buf_photo);
	    symlink("chess_photo", buf_this);
	}
    }
}
#endif /* defined(CHESSCOUNTRY) */

static int
isvisible_man(const menu_t * me)
{
    fileheader_t   *fhdr = &me->header[me->now - me->page];
    /* board friend only effact when in board reading mode */
    if (me->level >= MANAGER)
	return 1;
    if (fhdr->filemode & FILE_BM)
	return 0;
    if (fhdr->filemode & FILE_HIDE)
    {
	if (currstat == ANNOUNCE ||
	    !is_hidden_board_friend(me->bid, currutmp->uid))
	    return 0;
    }
    return 1;
}

typedef struct {
    char bReturnToRoot;		// 用來跳出 recursion
    int  z_indexes [STRLEN/2];	// each index code takes minimal 2 characters
}   a_menu_session_t;

// look up current location
#define A_WHEREAMI_PREFIX_STR	"我在哪？ "
static int 
a_where_am_i(const menu_t *root, int current_idx, const char *current_title)
{
    const menu_t *p;
    int lvl, num;
    const int max_lvl = t_lines -3; // 3: vs_hdr() + pause() + where_am_i
    char zidx_buf[STRLEN-sizeof(A_WHEREAMI_PREFIX_STR)-1] = "z";
    int  zidx_len;
    char abuf[12] = "";  // INT_MAX + sign + NUL
    int  last_idx_len = 0;
    char *zidx = zidx_buf + strlen(zidx_buf);
    char bskipping;

    move(1, 0); clrtobot(); outs(A_WHEREAMI_PREFIX_STR ANSI_COLOR(1));

    // decide length of last index
    snprintf(abuf, sizeof(abuf), "-%d", current_idx);
    last_idx_len = strlen(abuf)+1;
    // calculate remaining length
    zidx_len = sizeof(zidx_buf) - strlen(zidx) - last_idx_len;	

    bskipping = 0;
    // first round, quick render zidx
    for (p = root; p != NULL; p = p->next)
    {
	snprintf(abuf, sizeof(abuf), "-%d", p->now+1);
	if (p->next == NULL)
	{
	    // tail. always print it.
	    strlcpy(zidx, bskipping ? abuf+1 : abuf, last_idx_len+1);
	} else if (bskipping) {
	    // skip iteration
	    continue;
	} else {
	    // determine if there's still space to print it
	    if (strlen(abuf) > zidx_len)
	    {
		// re-print from previous entry
		char *s = strrchr(zidx_buf, '-');
		assert(s);
		zidx_len += strlen(s);
		strlcpy(s, "...", zidx_len+1);  // '-%d-' enough to hold '...'
		zidx_len -= strlen(s);
		bskipping = 1;
	    } else {
		// simply print it
		strlcpy(zidx, abuf, zidx_len);
		zidx_len -= strlen(zidx);
		zidx     += strlen(zidx);
	    }
	}
    }

    outs(zidx_buf); outs(ANSI_RESET);
    move(2, 0); 
	
    bskipping = 0;
    // second round, render text output
    for (p = root, lvl = 0, num = 0; lvl < max_lvl; p = p->next)
    {
	int onum = num;
	if (p)
	    num = p->now +1;

	if (bskipping && p)
	    continue;

	move(lvl+2, 0);
	prints("%*s", lvl, "");

	//  decide if we need to skip.
	if (++lvl == max_lvl-1 && p && p->next)
	{
	    prints("... (中間已省略) ...");
	    bskipping = 1;
	    continue;
	}

	// in the first round, print topic and no number...
	if (onum)
	    prints("%2d. ", onum);

	// for last round, p == NULL -> use current title
	prints("%s\n", p ? p->mtitle : current_title);

	// break at last round (p == NULL)
	if (!p)
	    break;
    }
    return 0;
}

int a_parse_zindexes(const char *s, a_menu_session_t *sess)
{
    int i = 0;

    memset(sess->z_indexes, 0, sizeof(sess->z_indexes));

    // skip leading whitespaces
    while (*s && isascii(*s) && isspace(*s)) s++;

    // determine if leading character was 'z', which means 'reset root'
    if (*s == 'z' || *s == 'Z')
	sess->z_indexes[i++] = -1;

    if (strpbrk(s, "0123456789") == NULL)
	return (sess->z_indexes[i] == 0);

    while (NULL != (s = strpbrk(s, "0123456789")) &&
	    i+1 < _DIM(sess->z_indexes) )
    {
	sess->z_indexes[i] = atoi(s);
	// for overflow, ignore all remaining.
	if (sess->z_indexes[i] < 0)
	{
	    sess->z_indexes[i] = 0;
	    break;
	}
	// only increase index
	if (sess->z_indexes[i])
	    i++;
	while(isascii(*s) && isdigit(*s)) s++;
    }

    return 1;
}

#define MULTI_SEARCH_PROMPT "新位置 (可輸入多層數字): "
static int
a_multi_search_num(char init, a_menu_session_t *sess)
{
    char buf[STRLEN - sizeof(MULTI_SEARCH_PROMPT) -1] = "";

    buf[0] = init;
    move(b_lines, 0);
    clrtoeol();
    outs(MULTI_SEARCH_PROMPT);
    sess->z_indexes[0] = sess->z_indexes[1] = 0;
    if (vgetstr(buf, sizeof(buf), VGET_DEFAULT, buf) < 1)
	return 0;

    a_parse_zindexes(buf, sess);
    if (!sess->z_indexes[1])
	return sess->z_indexes[0];
    return 0;
}

int
a_menu_rec(const char *maintitle, const char *path, 
	int lastlevel, int lastbid,
	char *trans_buffer,
	a_menu_session_t *sess,
	const int *preselect,
	// we don't change root's value (but may change root pointer)
	// we may   change parent's value (but never change parent pointer)
	const menu_t *root, menu_t* const parent)
{
    menu_t          me = {0};
    char            fname[PATHLEN];
    int             ch, returnvalue = FULLUPDATE;

    assert(sess);

    // prevent deep resursive directories
    if (strlen(path) + FNLEN >= PATHLEN)
    {
	// it is not save to enter such directory.
	return returnvalue;
    }

    if(trans_buffer)
	trans_buffer[0] = '\0';

    if (parent)
    {
	parent->next = &me;
    } else {
	assert(root == NULL);
	root = &me;
    }

    me.header_size = p_lines;
    me.header = (fileheader_t *) calloc(me.header_size, FHSZ);
    me.path = path;
    strlcpy(me.mtitle, maintitle, sizeof(me.mtitle));
    setadir(fname, me.path);
    me.num = get_num_records(fname, FHSZ);
    me.bid = lastbid;

    /* 精華區-tree 中部份結構屬於 cuser ==> BM */

    if (!(me.level = lastlevel)) {
	char           *ptr;

	// warning: this is only valid for me.level.
	// is_uBM should not do anything except returning test result:
	// for ex, setting user BM permission automatically.
	// such extra behavior will result in any sub-op to have PERM_BM
	// ability, which leads to entering BM board without authority.
	// Thanks to mtdas@ptt for reporting this exploit.
	if (HasUserPerm(PERM_BASIC) && (ptr = strrchr(me.mtitle, '[')))
	    me.level = is_uBM(ptr + 1, cuser.userid);
    }
    me.page = A_INVALID_PAGE;

    if (preselect && !*preselect)
	preselect = NULL;

    me.now = preselect ? (*preselect -1) : 0;

    for (;;) {
	if (me.now >= me.num)
	    me.now = me.num - 1;
	if (me.now < 0)
	    me.now = 0;

	if (me.now < me.page || me.now >= me.page + me.header_size) {
	    me.page = me.now - ((me.page == 10000 && me.now > p_lines / 2) ?
				(p_lines / 2) : (me.now % p_lines));
	    if (!a_showmenu(&me))
	    {
		// some directories are invalid, restart!
		sess->bReturnToRoot = 1;
		break;
	    }
	}

	if (preselect && *preselect && preselect[1])
	{
	    // if this is not the last preselect entry, enter it
	    ch = KEY_ENTER;
	} else {
	    ch = cursor_key(2 + me.now - me.page, 0);
	}

	if (ch == 'q' || ch == 'Q' || ch == KEY_LEFT)
	    break;

	// TODO maybe we should let 1-9=simple search and z=tree-search
	// TODO or let 'z' prefix means 'back to root'
	if ((ch >= '1' && ch <= '9') || (ch == 'z' || ch == 'Z')) {
	    int n = a_multi_search_num(ch, sess);
	    me.page = A_INVALID_PAGE;
	    if (n > 0)
	    {
		// simple (single) selection
		me.now = n-1;
		me.page = 10000; // I don't know what's the magic value 10000... 
	    }
	    else if (n == 0 && sess->z_indexes[0] == 0)
	    {
		// empty/invalid input
	    }
	    else
	    {
		// n == 0 with multiple selects
		preselect = sess->z_indexes;
		if (*preselect < 0)
		{
		    // return to root first?
		    if (parent)
		    {
			sess->bReturnToRoot = 1;
			return DONOTHING;
		    }

		    // already in root
		    preselect ++;
		}

		// handle first preselect (maybe zero due to previous 'already in root')
		if (*preselect > 0)
		    me.now = *preselect - 1;
		else
		    preselect = NULL;
	    }
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

	case KEY_HOME:
	case '0':
	    me.now = 0;
	    break;
	case KEY_END:
	case '$':
	    me.now = me.num - 1;
	    break;

	case '?':
	case '/':
	    if(me.num) {
		me.now = a_searchtitle(&me, ch == '?');
		me.page = A_INVALID_PAGE;
	    }
	    break;
	case 'h':
	    a_showhelp(me.level);
	    me.page = A_INVALID_PAGE;
	    break;

	case Ctrl('I'):
	    t_idle();
	    me.page = A_INVALID_PAGE;
	    break;

	case Ctrl('W'):
	    a_where_am_i(root, me.now, me.header[me.now - me.page].title);
	    vmsg(NULL);
	    me.page = A_INVALID_PAGE;
	    break;

	case 'e':
	case 'E':
	    snprintf(fname, sizeof(fname),
		     "%s/%s", path, me.header[me.now - me.page].filename);
	    if (dashf(fname) && me.level >= MANAGER) {
		int edflags = 0;
		*quote_file = 0;

# ifdef BN_BBSMOVIE
		if (me.bid && strcmp(getbcache(me.bid)->brdname, 
			    BN_BBSMOVIE) == 0)
		{
		    edflags |= EDITFLAG_UPLOAD;
		    edflags |= EDITFLAG_ALLOWLARGE;
		}
# endif // BN_BBSMOVIE

		if (vedit2(fname, NA, NULL, NULL, edflags) != -1) {
		    char            fpath[PATHLEN];
		    fileheader_t    fhdr;
		    strlcpy(fpath, path, sizeof(fpath));
		    stampfile(fpath, &fhdr);
		    unlink(fpath);
		    strlcpy(fhdr.filename,
			    me.header[me.now - me.page].filename,
			    sizeof(fhdr.filename));
		    strlcpy(me.header[me.now - me.page].owner,
			    cuser.userid,
			    sizeof(me.header[me.now - me.page].owner));
		    setadir(fpath, path);
		    substitute_record(fpath, me.header + me.now - me.page,
				      sizeof(fhdr), me.now + 1);

		}
		me.page = A_INVALID_PAGE;
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
		    (me.bid==0 || !is_BM_cache(me.bid)) && dashd(fname) )
		    vmsg("只有板主才可以拷貝目錄唷!");
		else
		    a_copyitem(fname, me.header[me.now - me.page].title, 0, 1);
		me.page = A_INVALID_PAGE;
		/* move down */
		if (++me.now >= me.num)
		    me.now = 0;
		break;
	    }
	case KEY_ENTER:
	case KEY_RIGHT:
	case 'r':
	    if (me.now >= me.num || me.now < 0)
	    {
		preselect = NULL;
		continue;
	    } 
	    else
	    {
		fileheader_t   *fhdr = &me.header[me.now - me.page];
		const int *newselect = preselect ? preselect+1 : NULL;
		preselect = NULL;

		if (!isvisible_man(&me))
		    break;
#ifdef DEBUG
		vmsgf("%s/%s", &path[11], fhdr->filename);;
#endif
		snprintf(fname, sizeof(fname), "%s/%s", path, fhdr->filename);
		if (dashf(fname)) {
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
				    "要把範例加入到文章內嗎?[y/N]" :
				    "確定要點這首歌嗎?[y/N]",
				    ans, sizeof(ans), LCECHO);
			    if (ans[0] == 'y') {
				strlcpy(trans_buffer, fname, PATHLEN);
				sess->bReturnToRoot = 1;
				if (currstat == OSONG) {
				    log_filef(FN_USSONG, LOG_CREAT, "%s\n", fhdr->title);
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
		    returnvalue = a_menu_rec(me.header[me.now - me.page].title, fname, 
			    me.level, me.bid, trans_buffer, 
			    sess, newselect, root, &me);
		   
		    if (returnvalue == DONOTHING)
		    {
			// DONOTHING will only be caused by previous a_multi_search_num + preselect.
			assert(sess->bReturnToRoot);

			if (!parent)
			{
			    // we've reached root menu!
			    assert(sess->z_indexes[0] == -1);
			    sess->bReturnToRoot = 0;
			    returnvalue = FULLUPDATE;
			    preselect = sess->z_indexes+1;  // skip first 'return to root'
			    if (*preselect > 0)
				me.now = *preselect-1;
			}
		    } else  {
			returnvalue = FULLUPDATE;
		    }

		    me.next = NULL;
		    /* Ptt  強力跳出recursive */
		    if (sess->bReturnToRoot) {
			free(me.header);
			return returnvalue;
		    }
		}
		me.page = A_INVALID_PAGE;
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
		me.page = A_INVALID_PAGE;
	    }

	    break;

	}

	if (me.level >= MANAGER) {
	    switch (ch) {
	    case 'n':
		a_newitem(&me, ADDITEM);
		me.page = A_INVALID_PAGE;
		break;
	    case 'g':
		a_newitem(&me, ADDGROUP);
		me.page = A_INVALID_PAGE;
		break;
	    case 'p':
		a_pasteitem(&me, 1);
		me.page = A_INVALID_PAGE;
		break;
	    case 'f':
		a_editsign(&me);
		me.page = A_INVALID_PAGE;
		break;
	    case Ctrl('P'):
		a_pastetagpost(&me, -1);
		returnvalue = DIRCHANGED;
		me.page = A_INVALID_PAGE;
		break;
	    case Ctrl('A'):
		a_pastetagpost(&me, 1);
		returnvalue = DIRCHANGED;
		me.page = A_INVALID_PAGE;
		break;
	    case 'a':
		a_appenditem(&me, 1);
		me.page = A_INVALID_PAGE;
		break;
	    }

	    if (me.num)
		switch (ch) {
		case 'm':
		    a_moveitem(&me);
		    me.page = A_INVALID_PAGE;
		    break;

		case 'D':
		    /* Ptt me.page = -1; */
		    a_delrange(&me);
		    me.page = A_INVALID_PAGE;
		    break;
		case 'd':
		    a_delete(&me);
		    me.page = A_INVALID_PAGE;
		    break;
		case 'H':
		    a_hideitem(&me);
		    me.page = A_INVALID_PAGE;
		    break;
		case 'T':
		    a_newtitle(&me);
		    me.page = A_INVALID_PAGE;
		    break;
#ifdef CHESSCOUNTRY
		case 'L':
		    a_setchesslist(&me);
		    break;
#endif
		}
	}
	if (me.level >= SYSOP) {
	    switch (ch) {
	    case 'N':
		a_showname(&me);
		me.page = A_INVALID_PAGE;
		break;
	    }
	}
    }
    free(me.header);
    return returnvalue;
}

int
a_menu(const char *maintitle, const char *path, 
	int lastlevel, int lastbid,
	char *trans_buffer)
{
    a_menu_session_t sess = {0};
    return a_menu_rec(maintitle, path, 
	    lastlevel, lastbid, trans_buffer, 
	    &sess, NULL, NULL, NULL);
}

int
Announce(void)
{
    setutmpmode(ANNOUNCE);
    a_menu(BBSNAME "佈告欄", "man",
	   ((HasUserPerm(PERM_SYSOP) ) ? SYSOP : NOBODY), 
	   0,
	   NULL);
    return 0;
}

