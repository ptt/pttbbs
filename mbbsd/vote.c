#include "bbs.h"

// XXX TODO bvote 跟實際的 vote control file 有時會不同步...
// XXX TODO 開票還是給獨立的 utility 或 daemon 去跑比較好

#define MAX_VOTE_NR	(20)
#define MAX_VOTE_PAGE	(5)
#define ITEM_PER_PAGE	(30)

static const char * const STR_bv_control = "control";	/* 投票日期 選項 */
static const char * const STR_bv_desc    = "desc";	/* 投票目的 */
static const char * const STR_bv_ballots = "ballots";	/* 投的票 (per byte) */
static const char * const STR_bv_flags   = "flags";
static const char * const STR_bv_comments= "comments";	/* 投票者的建議 */
static const char * const STR_bv_limited = "limited";	/* 私人投票 */
static const char * const STR_bv_limits  = "limits";	/* 投票資格限制 */
static const char * const STR_bv_title   = "vtitle";
static const char * const STR_bv_lock    = "vlock";

static const char * const STR_bv_results = "results";

// XXX TODO use macros to sync the length of strings above?
typedef struct {
    char control [sizeof("controlXX\0") ];
    char desc    [sizeof("descXX\0")    ];
    char ballots [sizeof("ballotsXX\0") ];
    char flags   [sizeof("flagsXX\0")   ];
    char comments[sizeof("commentsXX\0")];
    char limited [sizeof("limitedXX\0") ];
    char limits  [sizeof("limitsXX\0")  ];
    char title   [sizeof("vtitleXX\0")  ];
    char lock    [sizeof("vlockXX\0")   ];
} vote_buffer_t;

static void
votebuf_init(vote_buffer_t *vbuf, int n)
{
    assert(vbuf);
    snprintf(vbuf->ballots, sizeof(vbuf->ballots), "%s%d", STR_bv_ballots, n);
    snprintf(vbuf->control, sizeof(vbuf->control), "%s%d", STR_bv_control, n);
    snprintf(vbuf->desc,    sizeof(vbuf->desc),    "%s%d", STR_bv_desc,    n);
    snprintf(vbuf->flags,   sizeof(vbuf->flags),   "%s%d", STR_bv_flags,   n);
    snprintf(vbuf->comments,sizeof(vbuf->comments),"%s%d", STR_bv_comments,n);
    snprintf(vbuf->limited, sizeof(vbuf->limited), "%s%d", STR_bv_limited, n);
    snprintf(vbuf->limits,  sizeof(vbuf->limits),  "%s%d", STR_bv_limits,  n);
    snprintf(vbuf->title,   sizeof(vbuf->title),   "%s%d", STR_bv_title,   n);
    snprintf(vbuf->lock,    sizeof(vbuf->lock),    "%s%d", STR_bv_lock,    n);
}

static int
vote_stampfile(char *filepath, const char *boardname)
{
    fileheader_t    fh;
    char	    buf[PATHLEN];

    setbpath(buf, boardname);
    if(stampfile(buf, &fh) < 0)
        return -1;
    setbfile(filepath, boardname, fh.filename);
    return 0;
}

void
b_suckinfile(FILE * fp, const char *fname)
{
    FILE *sfp;
    char inbuf[256];

    if ((sfp = fopen(fname, "r"))) {

	while (fgets(inbuf, sizeof(inbuf), sfp))
	    fputs(inbuf, fp);
	fclose(sfp);
    }
}

void
b_suckinfile_invis(FILE * fp, const char *fname, const char *boardname)
{
    FILE           *sfp;

    if ((sfp = fopen(fname, "r"))) {
	char            inbuf[256];
	if(fgets(inbuf, sizeof(inbuf), sfp))
	{
	    /* first time, try if boardname revealed. */
	    char *post = strstr(inbuf, str_post1);
	    if(!post) post = strstr(inbuf, str_post2);
	    if(post)
		post = strstr(post, boardname);
	    if(post) {
		/* found releaved stuff. */
		/*
		// mosaic method 1
		while(*boardname++)
		    *post++ = '?';
		    */
		// mosaic method 2
		strcpy(post, "(某隱形看板)\n");
	    }
	    fputs(inbuf, fp);
	    while (fgets(inbuf, sizeof(inbuf), sfp))
		fputs(inbuf, fp);
	}
	fclose(sfp);
    }
}

static void
b_count(const char *buf, int counts[], short item_num, int *total)
{
    short	    choice;
    int             fd;

    memset(counts, 0, item_num * sizeof(counts[0]));
    *total = 0;
    if ((fd = open(buf, O_RDONLY)) != -1) {
	flock(fd, LOCK_EX);	/* Thor: 防止多人同時算 */
	while (read(fd, &choice, sizeof(short)) == sizeof(short)) {
	    if (choice >= item_num)
		continue;
	    counts[choice]++;
	    (*total)++;
	}
	flock(fd, LOCK_UN);
	close(fd);
    }
}

static int
b_nonzeroNum(const char *buf)
{
    int             i = 0;
    char            inchar;
    int             fd;

    if ((fd = open(buf, O_RDONLY)) != -1) {
	while (read(fd, &inchar, 1) == 1)
	    if (inchar)
		i++;
	close(fd);
    }
    return i;
}

static void
vote_report(const char *bname, const char *post_bname, const char *fname)
{
    int bid;
    char buf[PATHLEN];
    fileheader_t header;

    setbpath(buf, post_bname);
    stampfile(buf, &header);
    strlcpy(header.owner, "[馬路探子]", sizeof(header.owner));
    snprintf(header.title, sizeof(header.title), "[%s] 看板 選情報導", bname);

    Copy(fname, buf);

    /* append record to .DIR */
    setbfile(buf, post_bname, FN_DIR);
    if (append_record(buf, &header, sizeof(header)) >= 0)
	if ((bid = getbnum(post_bname)) > 0)
	    touchbtotal(bid);
}

static void
b_result_one(const vote_buffer_t *vbuf, boardheader_t * fh, int *total)
{
    FILE           *cfp, *tfp, *xfp;
    int             lockfd;
    char           *bname;
    char            inbuf[ANSILINELEN];
    int            *counts;
    int             people_num;
    short	    item_num, junk;
    char            b_control   [PATHLEN];
    char            b_report    [PATHLEN];
    char            buf		[PATHLEN];
    time4_t         closetime;

    bname = fh->brdname;

    setbfile(buf, bname, vbuf->lock);
    if((lockfd = OpenCreate(buf, O_EXCL)) < 0)
	return;
    close(lockfd);

    // XXX TODO backup vote files?

    if (fh->bvote > 0)
	fh->bvote--;

    // Read in the control file
    setbfile(b_control, bname, vbuf->control);
    cfp = fopen(b_control, "r");
    assert(cfp);
    unlink(b_control);
    fgets(inbuf, sizeof(inbuf), cfp);
    sscanf(inbuf, "%hd,%hd", &item_num, &junk);
    fgets(inbuf, sizeof(inbuf), cfp);
    sscanf(inbuf, "%d", &closetime);

    // prevent death caused by a bug, it should be remove later.
    if (item_num <= 0)
	return;

    counts = (int *)malloc(item_num * sizeof(int));

    // Flags file is used to track who had voted
    setbfile(buf, bname, vbuf->flags);
    people_num = b_nonzeroNum(buf);
    unlink(buf);

    // Ballots file is used to collect all votes
    setbfile(buf, bname, vbuf->ballots);
    b_count(buf, counts, item_num, total);
    unlink(buf);

    // Start of the report
    // Create a temp file to hold the vote report
    if (vote_stampfile(b_report, bname) < 0)
	return;
    if ((tfp = fopen(b_report, "w")) == NULL)
	return;

    // Report: title part
    setbfile(buf, bname, vbuf->title);
    if ((xfp = fopen(buf, "r"))) {
	fgets(inbuf, sizeof(inbuf), xfp);
	fprintf(tfp, "%s\n◆ 投票名稱: %s\n\n", msg_separator, inbuf);
	fclose(xfp);
    }
    fprintf(tfp, "%s\n◆ 投票中止於: %s\n\n\n◆ 票選題目描述:\n\n",
	    msg_separator, Cdate(&closetime));
    fh->vtime = now;

    // Report: description part
    setbfile(buf, bname, vbuf->desc);
    b_suckinfile(tfp, buf);
    unlink(buf);

    // Report: result part
    fprintf(tfp, "\n◆投票結果:(共有 %d 人投票,每人最多可投 %hd 票)\n",
	    people_num, junk);
    fprintf(tfp, "    選    項                                   總票數  得票率  得票分布\n");
    for (junk = 0; junk < item_num; junk++) {
	fgets(inbuf, sizeof(inbuf), cfp);
	chomp(inbuf);
	fprintf(tfp, "    %-42s %3d 票 %6.2f%%  %6.2f%%\n", inbuf + 3, counts[junk],
		(float)(counts[junk] * 100) / (float)(people_num),
		(float)(counts[junk] * 100) / (float)(*total));
    }
    fclose(cfp);

    free(counts);

    // Report: comments part
    fprintf(tfp, "%s\n◆ 使用者建議：\n\n", msg_separator);
    setbfile(buf, bname, vbuf->comments);
    b_suckinfile(tfp, buf);
    unlink(buf);

    fprintf(tfp, "%s\n◆ 總票數 = %d 票\n\n", msg_separator, *total);

    // End of the report
    fclose(tfp);

    // Post to boards
    vote_report(bname, bname, b_report);
    if (!(fh->brdattr & (BRD_NOCOUNT|BRD_HIDE))) { // from ptt2 local modification
	vote_report(bname, BN_RECORD, b_report);
    }

    // Reuse the report file, and append old results after it.
    tfp = fopen(b_report, "a");
    setbfile(buf, bname, STR_bv_results);
    b_suckinfile(tfp, buf);
    fclose(tfp);
    Rename(b_report, buf);

    // Remove the lock file
    setbfile(buf, bname, vbuf->lock);
    unlink(buf);
}

static void
b_result(boardheader_t * fh)
{
    FILE           *cfp;
    time4_t         closetime;
    int             i, total;
    char            buf[STRLEN];
    char            temp[STRLEN];
    vote_buffer_t   vbuf;

    for (i = 0; i < MAX_VOTE_NR; i++) {
	votebuf_init(&vbuf, i);
	setbfile(buf, fh->brdname, vbuf.control);
	cfp = fopen(buf, "r");
	if (!cfp)
	    continue;
	fgets(temp, sizeof(temp), cfp);
	fscanf(cfp, "%d\n", &closetime);
	fclose(cfp);
	if (closetime < now)
	    b_result_one(&vbuf, fh, &total);
    }
}

static int
b_close(boardheader_t * fh)
{
    b_result(fh);
    return 1;
}

static int
b_closepolls(void)
{
    boardheader_t  *fhp;
    int             pos;
    int             total;

#ifndef BARRIER_HAS_BEEN_IN_SHM
    char    *fn_vote_polling = ".polling";
    unsigned long  last;
    FILE           *cfp;
    /* XXX necessary to lock ? */
    if ((cfp = fopen(fn_vote_polling, "r"))) {
	char timebuf[100];
	fgets(timebuf, sizeof(timebuf), cfp);
	sscanf(timebuf, "%lu", &last);
	fclose(cfp);
	if (last + 3600 >= (unsigned long)now)
	    return 0;
    }
    if ((cfp = fopen(fn_vote_polling, "w")) == NULL)
	return 0;
    fprintf(cfp, "%d\n%s\n\n", now, Cdate(&now));
    fclose(cfp);
#endif

    for (fhp = bcache, pos = 1, total = num_boards(); pos <= total; fhp++, pos++) {
	if (fhp->bvote && b_close(fhp)) {
	    if (substitute_record(fn_board, fhp, sizeof(*fhp), pos) == -1)
		outs(err_board_update);
	    else
		reset_board(pos);
	}
    }

    return 0;
}

void
auto_close_polls(void)
{
    /* 最多一天開票一次 */
    if (now - SHM->close_vote_time > DAY_SECONDS) {
	b_closepolls();
	SHM->close_vote_time = now;
    }
}

static int
vote_view(const vote_buffer_t *vbuf, const char *bname)
{
    boardheader_t  *fhp;
    FILE           *fp;
    char            buf[STRLEN], genbuf[STRLEN], inbuf[STRLEN];
    short	    item_num, i;
    int             num = 0, pos, *counts, total;
    time4_t         closetime;

    setbfile(buf, bname, vbuf->ballots);
    if ((num = dashs(buf)) < 0) /* file size */
	num = 0;

    setbfile(buf, bname, vbuf->title);
    move(0, 0);
    clrtobot();

    if ((fp = fopen(buf, "r"))) {
	fgets(inbuf, sizeof(inbuf), fp);
	prints("\n投票名稱: %s", inbuf);
	fclose(fp);
    }
    setbfile(buf, bname, vbuf->control);
    fp = fopen(buf, "r");

    assert(fp);
    fscanf(fp, "%hd,%hd\n%d\n", &item_num, &i, &closetime);
    counts = (int *)malloc(item_num * sizeof(int));

    prints("\n◆ 預知投票紀事: 目前共有 %d 票,\n"
	   "本次投票將結束於 %s\n", (int)(num / sizeof(short)),
	   Cdate(&closetime));

    /* Thor: 開放 票數 預知 */
    setbfile(buf, bname, vbuf->flags);
    prints("共有 %d 人投票\n", b_nonzeroNum(buf));

    setbfile(buf, bname, vbuf->ballots);
    b_count(buf, counts, item_num, &total);

    total = 0;

    for (i = num = 0; i < item_num; i++, num++) {
	fgets(inbuf, sizeof(inbuf), fp);
	chomp(inbuf);
	inbuf[30] = '\0';	/* truncate */
#ifdef RESTRICT_PREVIEW_VOTE_RESULTS
        if (HasUserPerm(PERM_ALLBOARD | PERM_SYSSUPERSUBOP))
#endif
        {
            move(num % 15 + 6, num / 15 * 40);
            prints("  %-32s%3d 票", inbuf, counts[i]);
        }
	total += counts[i];
	if (num == 29) {
	    num = -1;
	    pressanykey();
	    move(6, 0);
	    clrtobot();
	}
    }
    fclose(fp);
    free(counts);
    pos = getbnum(bname);
    assert(0<=pos-1 && pos-1<MAX_BOARD);
    fhp = bcache + pos - 1;
    move(t_lines - 3, 0);
    prints("◆ 目前總票數 = %d 票", total);
    getdata(b_lines - 1, 0, "(A)取消投票 (B)提早開票 (C)繼續？[C] ", genbuf,
	    4, LCECHO);
    if (genbuf[0] == 'a') {
	getdata(b_lines - 1, 0, "請再次確認取消投票 (Y/N) [N] ", genbuf,
		4, LCECHO);
	if (genbuf[0] != 'y')
	    return FULLUPDATE;

	setbfile(buf, bname, vbuf->control);
	unlink(buf);
	setbfile(buf, bname, vbuf->flags);
	unlink(buf);
	setbfile(buf, bname, vbuf->ballots);
	unlink(buf);
	setbfile(buf, bname, vbuf->desc);
	unlink(buf);
	setbfile(buf, bname, vbuf->limited);
	unlink(buf);
	setbfile(buf, bname, vbuf->limits);
	unlink(buf);
	setbfile(buf, bname, vbuf->title);
	unlink(buf);

	if (fhp->bvote > 0)
	    fhp->bvote--;

	if (substitute_record(fn_board, fhp, sizeof(*fhp), pos) == -1)
	    outs(err_board_update);
	reset_board(pos);
    } else if (genbuf[0] == 'b') {
	b_result_one(vbuf, fhp, &total);
	if (substitute_record(fn_board, fhp, sizeof(*fhp), pos) == -1)
	    outs(err_board_update);

	reset_board(pos);
    }
    return FULLUPDATE;
}

static int
vote_view_all(const char *bname)
{
    int             i;
    int             x = -1;
    FILE           *fp, *xfp;
    char            buf[STRLEN], genbuf[STRLEN];
    char            inbuf[80];
    vote_buffer_t   vbuf;

    move(0, 0);
    for (i = 0; i < MAX_VOTE_NR; i++) {
	votebuf_init(&vbuf, i);
	setbfile(buf, bname, vbuf.control);
	if ((fp = fopen(buf, "r"))) {
	    prints("(%d) ", i);
	    x = i;
	    fclose(fp);

	    setbfile(buf, bname, vbuf.title);
	    if ((xfp = fopen(buf, "r"))) {
		fgets(inbuf, sizeof(inbuf), xfp);
		fclose(xfp);
	    } else
		strlcpy(inbuf, "無標題", sizeof(inbuf));
	    prints("%s\n", inbuf);
	}
    }

    if (x < 0)
	return FULLUPDATE;

    snprintf(buf, sizeof(buf), "要看幾號投票 [%d] ", x);
    getdata(b_lines - 1, 0, buf, genbuf, 4, LCECHO);


    if (atoi(genbuf) < 0 || atoi(genbuf) > MAX_VOTE_NR)
	snprintf(genbuf, sizeof(genbuf), "%d", x);

    votebuf_init(&vbuf, atoi(genbuf));
    setbfile(buf, bname, vbuf.control);

    if (dashf(buf)) {
	return vote_view(&vbuf, bname);
    } else
	return FULLUPDATE;
}

static int
vote_maintain(const char *bname)
{
    FILE           *fp = NULL;
    char            inbuf[STRLEN], buf[STRLEN];
    int             num = 0, aborted, pos, x, i;
    time4_t         closetime;
    boardheader_t  *fhp;
    char            genbuf[4];
    vote_buffer_t   vbuf;

    if (!(currmode & MODE_BOARD))
	return 0;
    if ((pos = getbnum(bname)) <= 0)
	return 0;

    assert(0<=pos-1 && pos-1<MAX_BOARD);
    fhp = bcache + pos - 1;

    if (fhp->bvote != 0) {

	getdata(b_lines - 1, 0,
		"(V)觀察目前投票 (M)舉辦新投票 (A)取消所有投票 (Q)繼續 [Q]",
		genbuf, 4, LCECHO);
	if (genbuf[0] == 'v')
	    return vote_view_all(bname);
	else if (genbuf[0] == 'a') {
            getdata(b_lines - 1, 0, "請再次確認取消所有投票 (Y/N) [N] ", genbuf,
                    4, LCECHO);
            if (genbuf[0] != 'y')
                return FULLUPDATE;

	    fhp->bvote = 0;

	    for (i = 0; i < MAX_VOTE_NR; i++) {
		int j;
		char buf2[64];
		const char *filename[] = {
		    STR_bv_ballots, STR_bv_control, STR_bv_desc, STR_bv_desc,
		    STR_bv_flags, STR_bv_comments, STR_bv_limited, STR_bv_limits,
		    STR_bv_title, NULL
		};
		for (j = 0; filename[j] != NULL; j++) {
		    snprintf(buf2, sizeof(buf2), "%s%d", filename[j], i);
		    setbfile(buf, bname, buf2);
		    unlink(buf);
		}
	    }
	    if (substitute_record(fn_board, fhp, sizeof(*fhp), pos) == -1)
		outs(err_board_update);

	    return FULLUPDATE;
	} else if (genbuf[0] != 'm') {
	    if (fhp->bvote >= MAX_VOTE_NR)
		vmsg("不得舉辦過多投票");
	    return FULLUPDATE;
	}
    }

    x = 1;
    do {
	votebuf_init(&vbuf, x);
	setbfile(buf, bname, vbuf.control);
	x++;
    } while (dashf(buf) && x <= MAX_VOTE_NR);

    --x;
    if (x >= MAX_VOTE_NR)
	return FULLUPDATE;

    getdata(b_lines - 1, 0,
	    "確定要舉辦投票嗎？ [y/N]: ",
	    inbuf, 4, LCECHO);
    if (inbuf[0] != 'y')
	return FULLUPDATE;

    vs_hdr("舉辦投票");
    votebuf_init(&vbuf, x);
    setbfile(buf, bname, vbuf.lock);
    unlink(buf);

    clear();
    move(0, 0);
    prints("第 %d 號投票\n", x);
    setbfile(buf, bname, vbuf.title);
    getdata(4, 0, "請輸入投票名稱:", inbuf, 50, DOECHO);
    if (inbuf[0] == '\0')
	strlcpy(inbuf, "不知名的", sizeof(inbuf));
    fp = fopen(buf, "w");
    assert(fp);
    fputs(inbuf, fp);
    fclose(fp);

    vmsg("按任何鍵開始編輯此次 [投票宗旨]");
    setbfile(buf, bname, vbuf.desc);
    aborted = veditfile(buf);
    if (aborted == EDIT_ABORTED) {
	vmsg("取消此次投票");
	return FULLUPDATE;
    }
    aborted = 0;
    setbfile(buf, bname, vbuf.flags);
    unlink(buf);

    getdata(4, 0,
	    "是否限定投票者名單：(y)編輯可投票人員名單[n]任何人皆可投票:[N]",
	    inbuf, 2, LCECHO);
    setbfile(buf, bname, vbuf.limited);
    if (inbuf[0] == 'y') {
	fp = fopen(buf, "w");
	assert(fp);
	//fprintf(fp, "此次投票設限");
	fclose(fp);
	friend_edit(FRIEND_CANVOTE);
    } else {
	if (dashf(buf))
	    unlink(buf);
    }
    getdata(5, 0,
	    "是否限定投票資格：(y)限制投票資格[n]不設限:[N]",
	    inbuf, 2, LCECHO);
    setbfile(buf, bname, vbuf.limits);
    if (inbuf[0] == 'y') {
	fp = fopen(buf, "w");
        // TODO regtime 跟 LOGINDAYS 其實可以合併
	assert(fp);
        // 註冊時間 (以月為單位，deprecated)
	fprintf(fp, "%d\n", now - (MONTH_SECONDS * 0));
	closetime = 0;
	do {
	    getdata(6, 0, STR_LOGINDAYS "下限", inbuf, 6, DOECHO);
	    closetime = atoi(inbuf);	// borrow variable
	} while (closetime < 0);
	fprintf(fp, "%d\n", closetime);
	fprintf(fp, "%d\n", 0); // legacy record: numpost limit.
	fclose(fp);
    } else {
	if (dashf(buf))
	    unlink(buf);
    }
    clear();
    getdata(0, 0, "此次投票進行幾天 (1~30天)？", inbuf, 4, DOECHO);

    closetime = atoi(inbuf);
    if (closetime <= 0)
	closetime = 1;
    else if (closetime > 30)
	closetime = 30;

    closetime = closetime * DAY_SECONDS + now;
    setbfile(buf, bname, vbuf.control);
    fp = fopen(buf, "w");
    assert(fp);
    fprintf(fp, "000,000\n%d\n", closetime);

    outs("\n請依序輸入選項, 按 ENTER 完成設定");
    num = 0;
    x = 0;	/* x is the page number */
    while (!aborted) {
	if( num % 15 == 0 ){
	    for( i = num ; i < num + 15 ; ++i ){
		move((i % 15) + 2, (i / 15) * 40);
		prints(ANSI_COLOR(1;30) "%c)" ANSI_RESET " ", i + 'A');
	    }
	}
	snprintf(buf, sizeof(buf), "%c) ", num + 'A');
	getdata((num % 15) + 2, (num / 15) * 40, buf,
		inbuf, 37, DOECHO);
	if (*inbuf) {
	    fprintf(fp, "%1c) %s\n", (num + 'A'), inbuf);
	    num++;
	}
	if ((*inbuf == '\0' && num >= 1) || x == MAX_VOTE_PAGE)
	    aborted = 1;
	if (num == ITEM_PER_PAGE) {
	    x++;
	    num = 0;
	}
    }
    snprintf(buf, sizeof(buf), "請問每人最多可投幾票？([1]～%d): ", x * ITEM_PER_PAGE + num);

    getdata(t_lines - 3, 0, buf, inbuf, 3, DOECHO);

    if (atoi(inbuf) <= 0 || atoi(inbuf) > (x * ITEM_PER_PAGE + num)) {
	inbuf[0] = '1';
	inbuf[1] = 0;
    }

    rewind(fp);
    fprintf(fp, "%3d,%3d\n", x * ITEM_PER_PAGE + num, MAX(1, atoi(inbuf)));
    fclose(fp);

    fhp->bvote++;

    if (substitute_record(fn_board, fhp, sizeof(*fhp), pos) == -1)
	outs(err_board_update);
    reset_board(pos);
    outs("開始投票了！");

    return FULLUPDATE;
}

static int
vote_flag(const vote_buffer_t *vbuf, const char *bname, char val)
{
    char            buf[256], flag;
    int             fd, num, size;

    num = usernum - 1;
    setbfile(buf, bname, vbuf->flags);
    if ((fd = OpenCreate(buf, O_RDWR)) == -1)
	return -1;
    size = lseek(fd, 0, SEEK_END);
    memset(buf, 0, sizeof(buf));
    while (size <= num) {
	write(fd, buf, sizeof(buf));
	size += sizeof(buf);
    }
    lseek(fd, num, SEEK_SET);
    read(fd, &flag, 1);
    if (flag == 0 && val != 0) {
	lseek(fd, num, SEEK_SET);
	write(fd, &val, 1);
    }
    close(fd);
    return flag;
}

static int
user_vote_one(const vote_buffer_t *vbuf, const char *bname)
{
    FILE           *cfp;
    char            buf[STRLEN], redo;
    short	    count, tickets;
    short	    curr_page, item_num, max_page;
    char            inbuf[80], choices[ITEM_PER_PAGE+1], vote[4], *chosen;
    time4_t         closetime;

    // bid = boaord id, must be at least one int.
    // fd should be int.
    int		    bid = 0, i = 0;
    int		    fd = 0;

    // initialize board info
    if ((bid = getbnum(bname)) <= 0)
	return 0;
    assert(0<=bid-1 && bid-1<MAX_BOARD);

    setbfile(buf, bname, vbuf->limited);	/* Ptt */
    if (dashf(buf)) {
	setbfile(buf, bname, FN_CANVOTE);
	if (!file_exist_record(buf, cuser.userid)) {
	    vmsg("對不起! 這是私人投票..你並沒有受邀唷!");
	    return FULLUPDATE;
	} else {
	    vmsg("恭喜你受邀此次私人投票.   <檢視此次受邀名單>");
	    more(buf, YEA);
	}
    }
    setbfile(buf, bname, vbuf->limits);
    if (dashf(buf)) {
	int limits_logins, limits_posts;
	FILE * lfp = fopen(buf, "r");
        const char *reason = NULL;
	assert(lfp);
	fscanf(lfp, "%d%d%d", &closetime, &limits_logins, &limits_posts);
	fclose(lfp);
	// XXX if this is a private vote (limited), I think we don't need to check limits?
	if (cuser.firstlogin > closetime)
            reason = "註冊時間";
        else if (cuser.numlogindays < (uint32_t)limits_logins)
            reason = STR_LOGINDAYS;

        if (reason)
        {
	    vmsgf("%s未達投票資格限制", reason);
	    return FULLUPDATE;
	}
    }
    if (vote_flag(vbuf, bname, '\0')) {
	vmsg("此次投票，你已投過了！");
	return FULLUPDATE;
    }
    setutmpmode(VOTING);
    setbfile(buf, bname, vbuf->desc);
    more(buf, YEA);

#ifdef USE_VOTE_CAPTCHA
    const char *captcha_msg = remote_captcha();
    if (captcha_msg != NULL) {
	vmsg(captcha_msg);
	return FULLUPDATE;
    }
#endif

    vs_hdr("投票箱");

    setbfile(buf, bname, vbuf->control);
    cfp = fopen(buf, "r");
    if (!cfp)
    {
	vmsg("抱歉，此投票已取消或無效。");
	return FULLUPDATE;
    }

    assert(cfp);
    fscanf(cfp, "%hd,%hd\n%d\n", &item_num, &tickets, &closetime);
    chosen = (char *)malloc(item_num+100); // XXX dirty fix 板主增加選項的問題
    memset(chosen, 0, item_num+100);
    memset(choices, 0, sizeof(choices));
    max_page = (item_num - 1)/ ITEM_PER_PAGE + 1;

    outs("投票方式：確定好您的選擇後，輸入其代碼(A, B, C...)即可。\n");
    prints("此次投票你可以投 %1hd 票。按 0 取消投票, 1 完成投票, "
	    "> 下一頁, < 上一頁\n此次投票將結束於：%s \n\n",
	   tickets, Cdate(&closetime));

#define REDO_DRAW	1
#define REDO_SCAN	2
    redo = REDO_DRAW;
    curr_page = 0;
    while (1) {

	if (redo) {
	    int i, j;
	    move(5, 0);
	    clrtobot();

	    /* 想不到好方法 因為不想整個讀進 memory
	     * 而且大部分的投票不會超過一頁 所以再從檔案 scan */
	    /* XXX 投到一半板主增加選項則 chosen 太小 */
	    if (redo & REDO_SCAN) {
		for (i = 0; i < curr_page; i++)
		    for (j = 0; j < ITEM_PER_PAGE; j++)
			fgets(inbuf, sizeof(inbuf), cfp);
	    }

	    count = 0;
	    for (i = 0; i < ITEM_PER_PAGE && fgets(inbuf, sizeof(inbuf), cfp); i++) {
		chomp(inbuf);
		move((count % 15) + 5, (count / 15) * 40);
		prints("%c%s", chosen[curr_page * ITEM_PER_PAGE + i] ? '*' : ' ',
			inbuf);
		choices[count % ITEM_PER_PAGE] = inbuf[0];
		count++;
	    }
	    redo = 0;
	}

	vote[0] = vote[1] = '\0';
	move(t_lines - 2, 0);
	prints("你還可以投 %2d 票   [ 目前所在頁數 %2d / 共 %2d 頁 (可輸入 '<' '>' 換頁) ]", tickets - i, curr_page + 1, max_page);
	getdata(t_lines - 4, 0, "輸入您的選擇: ", vote, sizeof(vote), DOECHO);
	*vote = toupper(*vote);

#define CURRENT_CHOICE \
    chosen[curr_page * ITEM_PER_PAGE + vote[0] - 'A']
	if (vote[0] == '0' || (!vote[0] && !i)) {
	    outs("記得再來投喔!!");
	    break;
	} else if (vote[0] == '1' && i);
	else if (!vote[0])
	    continue;
	else if (vote[0] == '<' && max_page > 1) {
	    curr_page = (curr_page + max_page - 1) % max_page;
    	    rewind(cfp);
	    fgets(inbuf, sizeof(inbuf), cfp);
    	    fgets(inbuf, sizeof(inbuf), cfp);
	    redo = REDO_DRAW | REDO_SCAN;
	    continue;
	}
	else if (vote[0] == '>' && max_page > 1) {
	    curr_page = (curr_page + 1) % max_page;
	    if (curr_page == 0) {
		rewind(cfp);
		fgets(inbuf, sizeof(inbuf), cfp);
		fgets(inbuf, sizeof(inbuf), cfp);
	    }
	    redo = REDO_DRAW;
	    continue;
	}
	else if (index(choices, vote[0]) == NULL)	/* 無效 */
	    continue;
	else if ( CURRENT_CHOICE ) { /* 已選 */
	    move(((vote[0] - 'A') % 15) + 5, (((vote[0] - 'A')) / 15) * 40);
	    outc(' ');
	    CURRENT_CHOICE = 0;
	    i--;
	    continue;
	} else {
	    if (i == tickets)
		continue;
	    move(((vote[0] - 'A') % 15) + 5, (((vote[0] - 'A')) / 15) * 40);
	    outc('*');
	    CURRENT_CHOICE = 1;
	    i++;
	    continue;
	}

	if (vote_flag(vbuf, bname, vote[0]) != 0)
	    outs("重複投票! 不予計票。");
	else {
	    setbfile(buf, bname, vbuf->ballots);
	    if ((fd = OpenCreate(buf, O_WRONLY | O_APPEND)) == 0)
		outs("無法投入票匭\n");
	    else {
		struct stat     statb;
		char            buf[3], mycomments[3][74], b_comments[80];

		for (i = 0; i < 3; i++)
		    strlcpy(mycomments[i], "\n", sizeof(mycomments[i]));

		flock(fd, LOCK_EX);
		for (count = 0; count < item_num; count++) {
		    if (chosen[count])
			write(fd, &count, sizeof(short));
		}
		flock(fd, LOCK_UN);
		fstat(fd, &statb);
		close(fd);
		getdata(b_lines - 2, 0,
			"您對這次投票有什麼寶貴的意見嗎？(y/n)[N]",
			buf, 3, DOECHO);
		if (buf[0] == 'Y' || buf[0] == 'y') {
		    do {
			move(5, 0);
			clrtobot();
			outs("請問您對這次投票有什麼寶貴的意見？"
			     "最多三行，按[Enter]結束");
			for (i = 0; (i < 3) &&
			     getdata(7 + i, 0, "：",
				     mycomments[i], sizeof(mycomments[i]),
				     DOECHO); i++);
			getdata(b_lines - 2, 0, "(S)儲存 (E)重新來過 "
				"(Q)取消？[S]", buf, 3, LCECHO);
		    } while (buf[0] == 'e');
		    if (buf[0] == 'q')
			break;
		    setbfile(b_comments, bname, vbuf->comments);
		    if (mycomments[0][0])
		    {
			FILE *fcm = fopen(b_comments, "a");
			if (fcm) {
			    fprintf(fcm,
				    ANSI_COLOR(36) "○使用者" ANSI_COLOR(1;36) " %s "
				    ANSI_COLOR(;36) "的建議：" ANSI_RESET "\n",
				    cuser.userid);
			    for (i = 0; i < 3; i++)
				fprintf(fcm, "    %s\n", mycomments[i]);
			    fprintf(fcm, "\n");
			    fclose(fcm);
			}
		    }
		}
		move(b_lines - 1, 0);
		outs("已完成投票！\n");
	    }
	}
	break;
    }
    free(chosen);
    fclose(cfp);
    pressanykey();
    return FULLUPDATE;
}

static const char *
voteperm_msg(const char *bname)
{
    const char *msg;

    if (!HasBasicUserPerm(PERM_LOGINOK))
	return "對不起! 您未完成註冊程序, 還沒有投票權喔!";

    if (HasUserPerm(PERM_VIOLATELAW))
        return "罰單未繳清，無法投票。";

    if ((msg = banned_msg(bname)) != NULL)
        return msg;

    return NULL;
}

static int
user_vote(const char *bname)
{
    int             bid;
    boardheader_t  *bh;
    char            buf[STRLEN];
    FILE           *fp;
    int             i, x = -1;
    char            genbuf[STRLEN];
    char            inbuf[80];
    vote_buffer_t   vbuf;
    const char *msg;

    if ((bid = getbnum(bname)) <= 0)
	return 0;

    assert(0<=bid-1 && bid-1<MAX_BOARD);
    bh = getbcache(bid);

    clear();

    if (bh->bvote == 0) {
	vmsg("目前並沒有任何投票舉行。");
	return FULLUPDATE;
    }
    if ((msg = voteperm_msg(bname)) != NULL) {
        vmsg(msg);
        return FULLUPDATE;
    }

    // XXX I think such loop is ineffective...
    // According to the creation code, the vote is ranged as [1..MAX_VOTE_NR]
    for (i = 1; i <= MAX_VOTE_NR; i++) {
	votebuf_init(&vbuf, i);
	setbfile(buf, bname, vbuf.control);
	if (!dashf(buf))
	    continue;

	x = i;
	prints("(%d) ", x);

	setbfile(buf, bname, vbuf.title);
	fp = fopen(buf, "r");
	if (fp) {
	    fgets(inbuf, sizeof(inbuf), fp);
	    fclose(fp);
	} else {
	    strlcpy(inbuf, "無標題", sizeof(inbuf));
	}
	prints("%s\n", inbuf);
    }

    if (x < 0)
	return FULLUPDATE;

    snprintf(buf, sizeof(buf), "要投幾號投票 [%d] ", x);
    getdata(b_lines - 1, 0, buf, genbuf, 4, LCECHO);
    i = atoi(genbuf);

    // x: default (max), i: user selection
    if (i < 1 || i > MAX_VOTE_NR)
	i = x;

    votebuf_init(&vbuf, i);
    return user_vote_one(&vbuf, bname);
}

static int
vote_results(const char *bname)
{
    char            buf[STRLEN];

    setbfile(buf, bname, STR_bv_results);
    if (more(buf, YEA) == -1)
	vmsg("目前沒有任何投票的結果。");
    return FULLUPDATE;
}

int
b_vote_maintain(void)
{
    return vote_maintain(currboard);
}

int
b_vote(void)
{
    return user_vote(currboard);
}

int
b_results(void)
{
    return vote_results(currboard);
}
