/* $Id: bbs.c,v 1.93 2003/05/14 08:31:29 in2 Exp $ */
#include "bbs.h"

static int recommend(int ent, fileheader_t * fhdr, char *direct);

static void
mail_by_link(char *owner, char *title, char *path)
{
    char            genbuf[200];
    fileheader_t    mymail;

    snprintf(genbuf, sizeof(genbuf),
	     BBSHOME "/home/%c/%s", cuser.userid[0], cuser.userid);
    stampfile(genbuf, &mymail);
    strlcpy(mymail.owner, owner, sizeof(mymail.owner));
    snprintf(mymail.title, sizeof(mymail.title), title);
    unlink(genbuf);
    Link(path, genbuf);
    snprintf(genbuf, sizeof(genbuf),
	     BBSHOME "/home/%c/%s/.DIR", cuser.userid[0], cuser.userid);

    append_record(genbuf, &mymail, sizeof(mymail));
}


void
anticrosspost()
{
    char            buf[200];

    snprintf(buf, sizeof(buf),
	    "\033[1;33;46m%s \033[37;45mcross post 文章 \033[37m %s\033[m",
	    cuser.userid, ctime(&now));
    log_file("etc/illegal_money", buf);

    post_violatelaw(cuser.userid, "Ptt系統警察", "Cross-post", "罰單處份");
    cuser.userlevel |= PERM_VIOLATELAW;
    cuser.vl_count++;
    mail_by_link("Ptt警察部隊", "Cross-Post罰單",
		 BBSHOME "/etc/crosspost.txt");
    passwd_update(usernum, &cuser);
    exit(0);
}

/* Heat CharlieL */
int
save_violatelaw()
{
    char            buf[128], ok[3];

    setutmpmode(VIOLATELAW);
    clear();
    stand_title("繳罰單中心");

    if (!(cuser.userlevel & PERM_VIOLATELAW)) {
	mprints(22, 0, "\033[1;31m你無聊啊? 你又沒有被開罰單~~\033[m");
	pressanykey();
	return 0;
    }
    reload_money();
    if (cuser.money < (int)cuser.vl_count * 1000) {
	snprintf(buf, sizeof(buf), "\033[1;31m這是你第 %d 次違反本站法規"
		 "必須繳出 %d $Ptt ,你只有 %d 元, 錢不夠啦!!\033[m",
		 (int)cuser.vl_count, (int)cuser.vl_count * 1000, cuser.money);
	mprints(22, 0, buf);
	pressanykey();
	return 0;
    }
    move(5, 0);
    prints("\033[1;37m你知道嗎? 因為你的違法 "
	   "已經造成很多人的不便\033[m\n");
    prints("\033[1;37m你是否確定以後不會再犯了？\033[m\n");

    if (!getdata(10, 0, "確定嗎？[y/n]:", ok, sizeof(ok), LCECHO) ||
	ok[0] == 'n' || ok[0] == 'N') {
	mprints(22, 0, "\033[1;31m等你想通了再來吧!! "
		"我相信你不會知錯不改的~~~\033[m");
	pressanykey();
	return 0;
    }
    snprintf(buf, sizeof(buf), "這是你第 %d 次違法 必須繳出 %d $Ptt",
	     cuser.vl_count, cuser.vl_count * 1000);
    mprints(11, 0, buf);

    if (!getdata(10, 0, "要付錢[y/n]:", ok, sizeof(ok), LCECHO) ||
	ok[0] == 'N' || ok[0] == 'n') {

	mprints(22, 0, "\033[1;31m 嗯 存夠錢 再來吧!!!\033[m");
	pressanykey();
	return 0;
    }
    demoney(-1000 * cuser.vl_count);
    cuser.userlevel &= (~PERM_VIOLATELAW);
    passwd_update(usernum, &cuser);
    return 0;
}

/*
 * void make_blist() { CreateNameList(); apply_boards(g_board_names); }
 */

static time_t   board_note_time;
static char    *brd_title;

void
set_board()
{
    boardheader_t  *bp;

    bp = getbcache(currbid);
    if( !Ben_Perm(bp) ){
	vmsg("access control violation, exit");
	u_exit("access control violation!");
    }
    board_note_time = bp->bupdate;
    brd_title = bp->BM;
    if (brd_title[0] <= ' ')
	brd_title = "徵求中";
    snprintf(currBM, sizeof(currBM), "板主：%s", brd_title);
    brd_title = ((bp->bvote != 2 && bp->bvote) ? "本看板進行投票中" :
		 bp->title + 7);
    currmode = (currmode & (MODE_DIRTY | MODE_MENU)) | MODE_STARTED;

    if (HAS_PERM(PERM_ALLBOARD) || is_BM(bp->BM))
	currmode = currmode | MODE_BOARD | MODE_POST;
    else if (haspostperm(currboard))
	currmode |= MODE_POST;
}

static void
readtitle()
{
    showtitle(currBM, brd_title);
    outs("[←]離開 [→]閱\讀 [^P]發表文章 [b]備忘錄 [d]刪除 [z]精華區 "
      "[TAB]文摘 [h]elp\n\033[7m  編號   日 期  作  者       文  章  標  題"
	 "                                   \033[m");
}

static void
readdoent(int num, fileheader_t * ent)
{
    int             type, uid;
    char           *mark, *title, color, special = 0, isonline = 0;
    userinfo_t     *uentp;
    if (ent->recommend > 9 || ent->recommend < 0)
	ent->recommend = 0;
//Ptt:暫時
	type = brc_unread(ent->filename, brc_num, brc_list) ? '+' : ' ';

    if ((currmode & MODE_BOARD) && (ent->filemode & FILE_DIGEST))
	type = (type == ' ') ? '*' : '#';
    else if (currmode & MODE_BOARD || HAS_PERM(PERM_LOGINOK)) {
	if (ent->filemode & FILE_MARKED)
	    type = (type == ' ') ? 'm' : 'M';

	else if (TagNum && !Tagger(atoi(ent->filename + 2), 0, TAG_NIN))
	    type = 'D';

	else if (ent->filemode & FILE_SOLVED)
	    type = 's';
    }
    title = subject(mark = ent->title);
    if (title == mark)
	color = '1', mark = "□";
    else
	color = '3', mark = "R:";

    if (title[47])
	strlcpy(title + 44, " …", sizeof(title) - 44);	/* 把多餘的 string 砍掉 */

    if (!strncmp(title, "[公告]", 6))
	special = 1;
    if (!strchr(ent->owner, '.') && (uid = searchuser(ent->owner)) &&
	!SHM->GV2.e.noonlineuser &&
	(uentp = search_ulist(uid)) && isvisible(currutmp, uentp))
	isonline = 1;

    if (strncmp(currtitle, title, TTLEN))
	prints("%6d %c\033[1;32m%c\033[m%-6s\033[%dm%-13.12s\033[m%s "
	       "\033[1m%.*s\033[m%s\n", num, type,
	       ent->recommend ? ent->recommend + '0' : ' ',
	       ent->date,
	       isonline,
	       ent->owner, mark,
	       special ? 6 : 0, title, special ? title + 6 : title);
    else
	prints("%6d %c\033[1;32m%c\033[m%-6s\033[%dm%-13.12s\033[1;3%cm%s "
	       "%s\033[m\n", num, type,
	       ent->recommend ? ent->recommend + '0' : ' ',
	       ent->date,
	       isonline,
	       ent->owner, color, mark,
	       title);
}

int
cmpfilename(fileheader_t * fhdr)
{
    return (!strcmp(fhdr->filename, currfile));
}

int
cmpfmode(fileheader_t * fhdr)
{
    return (fhdr->filemode & currfmode);
}

int
cmpfowner(fileheader_t * fhdr)
{
    return !strcasecmp(fhdr->owner, currowner);
}

int
whereami(int ent, fileheader_t * fhdr, char *direct)
{
    boardheader_t  *bh, *p[32], *root;
    int             i, j;

    if (!currutmp->brc_id)
	return 0;

    move(1, 0);
    clrtobot();
    bh = getbcache(currutmp->brc_id);
    root = getbcache(1);
    p[0] = bh;
    for (i = 0; i < 31 && p[i]->parent != root && p[i]->parent; i++)
	p[i + 1] = p[i]->parent;
    j = i;
    prints("我在哪?\n%-40.40s %.13s\n", p[j]->title + 7, p[j]->BM);
    for (j--; j >= 0; j--)
	prints("%*s %-13.13s %-37.37s %.13s\n", (i - j) * 2, "",
	       p[j]->brdname, p[j]->title,
	       p[j]->BM);

    pressanykey();
    return FULLUPDATE;
}

static int
substitute_check(fileheader_t * fhdr)
{
    fileheader_t    hdr;
    char            genbuf[100];
    int             num = 0;

    /* rocker.011018: 串接模式用reference增進效率 */
    if ((currmode & MODE_SELECT) && (fhdr->money & FHR_REFERENCE)) {
	num = fhdr->money & ~FHR_REFERENCE;
	setbdir(genbuf, currboard);
	get_record(genbuf, &hdr, sizeof(hdr), num);

	/* 再這裡要check一下原來的dir裡面是不是有被人動過... */
	if (strcmp(hdr.filename, fhdr->filename))
	    num = getindex(genbuf, fhdr->filename, sizeof(fileheader_t));

	substitute_record(genbuf, fhdr, sizeof(*fhdr), num);
    }
    return num;
}
static int
do_select(int ent, fileheader_t * fhdr, char *direct)
{
    char            bname[20];
    char            bpath[60];
    boardheader_t  *bh;
    struct stat     st;
    int             i;

    setutmpmode(SELECT);
    move(0, 0);
    clrtoeol();
    generalnamecomplete(MSG_SELECT_BOARD, bname, sizeof(bname),
			SHM->Bnumber,
			completeboard_compar,
			completeboard_permission,
			completeboard_getname);
    if (bname[0] == '\0' || !(i = getbnum(bname)))
	return FULLUPDATE;
    bh = getbcache(i);
    if (!Ben_Perm(bh))
	return FULLUPDATE;
    strlcpy(bname, bh->brdname, sizeof(bname));
    currbid = i;

    setbpath(bpath, bname);
    if ((*bname == '\0') || (stat(bpath, &st) == -1)) {
	move(2, 0);
	clrtoeol();
	outs(err_bid);
	return FULLUPDATE;
    }
    setutmpbid(currbid);

    brc_initial(bname);
    set_board();
    setbdir(direct, currboard);

    move(1, 0);
    clrtoeol();
    return NEWDIRECT;
}

/* ----------------------------------------------------- */
/* 改良 innbbsd 轉出信件、連線砍信之處理程序             */
/* ----------------------------------------------------- */
void
outgo_post(fileheader_t * fh, char *board)
{
    FILE           *foo;

    if ((foo = fopen("innd/out.bntp", "a"))) {
	fprintf(foo, "%s\t%s\t%s\t%s\t%s\n", board,
		fh->filename, cuser.userid, cuser.username, fh->title);
	fclose(foo);
    }
}

static void
cancelpost(fileheader_t * fh, int by_BM)
{
    FILE           *fin, *fout;
    char           *ptr, *brd;
    fileheader_t    postfile;
    char            genbuf[200];
    char            nick[STRLEN], fn1[STRLEN], fn2[STRLEN];

    setbfile(fn1, currboard, fh->filename);
    if ((fin = fopen(fn1, "r"))) {
	brd = by_BM ? "deleted" : "junk";

	setbpath(fn2, brd);
	stampfile(fn2, &postfile);
	memcpy(postfile.owner, fh->owner, IDLEN + TTLEN + 10);

	nick[0] = '\0';
	while (fgets(genbuf, sizeof(genbuf), fin)) {
	    if (!strncmp(genbuf, str_author1, LEN_AUTHOR1) ||
		!strncmp(genbuf, str_author2, LEN_AUTHOR2)) {
		if ((ptr = strrchr(genbuf, ')')))
		    *ptr = '\0';
		if ((ptr = (char *)strchr(genbuf, '(')))
		    strlcpy(nick, ptr + 1, sizeof(nick));
		break;
	    }
	}

	if ((fout = fopen("innd/cancel.bntp", "a"))) {
	    fprintf(fout, "%s\t%s\t%s\t%s\t%s\n", currboard, fh->filename,
		    cuser.userid, nick, fh->title);
	    fclose(fout);
	}
	fclose(fin);
	Rename(fn1, fn2);
	setbdir(genbuf, brd);
	setbtotal(getbnum(brd));
	append_record(genbuf, &postfile, sizeof(postfile));
    }
}

/* ----------------------------------------------------- */
/* 發表、回應、編輯、轉錄文章                            */
/* ----------------------------------------------------- */
void
do_reply_title(int row, char *title)
{
    char            genbuf[200];
    char            genbuf2[4];

    if (strncasecmp(title, str_reply, 4))
	snprintf(save_title, sizeof(save_title), "Re: %s", title);
    else
	strlcpy(save_title, title, sizeof(save_title));
    save_title[TTLEN - 1] = '\0';
    snprintf(genbuf, sizeof(genbuf), "採用原標題《%.60s》嗎?[Y] ", save_title);
    getdata(row, 0, genbuf, genbuf2, 4, LCECHO);
    if (genbuf2[0] == 'n' || genbuf2[0] == 'N')
	getdata(++row, 0, "標題：", save_title, TTLEN, DOECHO);
}

static void
do_unanonymous_post(char *fpath)
{
    fileheader_t    mhdr;
    char            title[128];
    char            genbuf[200];

    setbpath(genbuf, "UnAnonymous");
    if (dashd(genbuf)) {
	stampfile(genbuf, &mhdr);
	unlink(genbuf);
	Link(fpath, genbuf);
	strlcpy(mhdr.owner, cuser.userid, sizeof(mhdr.owner));
	strlcpy(mhdr.title, save_title, sizeof(mhdr.title));
	mhdr.filemode = 0;
	setbdir(title, "UnAnonymous");
	append_record(title, &mhdr, sizeof(mhdr));
    }
}

#ifdef NO_WATER_POST
static time_t   last_post_time = 0;
static time_t   water_counts = 0;
#endif

void
do_allpost(fileheader_t *postfile, const char *fpath, const char *owner)
{
    char            genbuf[200];

    setbpath(genbuf, ALLPOST);
    stampfile(genbuf, postfile);
    unlink(genbuf);

    /* jochang: boards may spread across many disk */
    /*
     * link doesn't work across device, Link doesn't work if we have
     * same-time-across-device posts, we try symlink now
     */
    {
	/* we need absolute path for symlink */
	char            abspath[256] = BBSHOME "/";
	strcat(abspath, fpath);
	symlink(abspath, genbuf);
    }
    strlcpy(postfile->owner, owner, sizeof(postfile->owner));
    strlcpy(postfile->title, save_title, sizeof(postfile->title));
    postfile->filemode = FILE_LOCAL;
    setbdir(genbuf, ALLPOST);
    if (append_record(genbuf, postfile, sizeof(fileheader_t)) != -1) {
	setbtotal(getbnum(ALLPOST));
    }
}

static int
do_general()
{
    fileheader_t    postfile;
    char            fpath[80], buf[80];
    int             aborted, defanony, ifuseanony;
    char            genbuf[200], *owner, *ctype[] = {"問題", "建議", "討論", "心得", "閒聊", "公告", "情報"};
    boardheader_t  *bp;
    int             islocal;

    ifuseanony = 0;
    bp = getbcache(currbid);

    clear();
    if (!(currmode & MODE_POST)) {
	move(5, 10);
	outs("對不起，您目前無法在此發表文章！");
	pressanykey();
	return FULLUPDATE;
    }
#ifdef NO_WATER_POST
    /* 三分鐘內最多發表五篇文章 */
    if (currutmp->lastact - last_post_time < 60 * 3) {
	if (water_counts >= 5) {
	    move(5, 10);
	    outs("對不起，您的文章太水囉，待會再post吧！小秘訣:可用'X'推薦文章");
	    pressanykey();
	    return FULLUPDATE;
	}
    } else {
	last_post_time = currutmp->lastact;
	water_counts = 0;
    }
#endif

    setbfile(genbuf, currboard, FN_POST_NOTE);

    if (more(genbuf, NA) == -1)
	more("etc/" FN_POST_NOTE, NA);

    move(19, 0);
    prints("發表文章於【\033[33m %s\033[m 】 \033[32m%s\033[m 看板\n\n",
	   currboard, bp->title + 7);

    if (quote_file[0])
	do_reply_title(20, currtitle);
    else {
	getdata(21, 0,
	"種類：1.問題 2.建議 3.討論 4.心得 5.閒聊 6.公告 7.情報 (1-7或不選)",
		save_title, 3, LCECHO);
	local_article = save_title[0] - '1';
	if (local_article >= 0 && local_article <= 6)
	    snprintf(save_title, sizeof(save_title),
		     "[%s] ", ctype[local_article]);
	else
	    save_title[0] = '\0';
	getdata_buf(22, 0, "標題：", save_title, TTLEN, DOECHO);
	strip_ansi(save_title, save_title, 0);
    }
    if (save_title[0] == '\0')
	return FULLUPDATE;

    curredit &= ~EDIT_MAIL;
    curredit &= ~EDIT_ITEM;
    setutmpmode(POSTING);

    /* 未具備 Internet 權限者，只能在站內發表文章 */
    if (HAS_PERM(PERM_INTERNET))
	local_article = 0;
    else
	local_article = 1;

    /* build filename */
    setbpath(fpath, currboard);
    stampfile(fpath, &postfile);

    aborted = vedit(fpath, YEA, &islocal);
    if (aborted == -1) {
	unlink(fpath);
	pressanykey();
	return FULLUPDATE;
    }
    water_counts++;		/* po成功 */

    /* set owner to Anonymous , for Anonymous board */

#ifdef HAVE_ANONYMOUS
    /* Ptt and Jaky */
    defanony = currbrdattr & BRD_DEFAULTANONYMOUS;
    if ((currbrdattr & BRD_ANONYMOUS) &&
	((strcmp(real_name, "r") && defanony) || (real_name[0] && !defanony))
	) {
	strcat(real_name, ".");
	owner = real_name;
	ifuseanony = 1;
    } else
	owner = cuser.userid;
#else
    owner = cuser.userid;
#endif
    /* 錢 */
    aborted = (aborted > MAX_POST_MONEY * 2) ? MAX_POST_MONEY : aborted / 2;
    postfile.money = aborted;
    strlcpy(postfile.owner, owner, sizeof(postfile.owner));
    strlcpy(postfile.title, save_title, sizeof(postfile.title));
    if (islocal)		/* local save */
	postfile.filemode = FILE_LOCAL;

    setbdir(buf, currboard);
    if (append_record(buf, &postfile, sizeof(postfile)) != -1) {
	setbtotal(currbid);

	if (currmode & MODE_SELECT)
	    append_record(currdirect, &postfile, sizeof(postfile));
	if (!islocal && !(bp->brdattr & BRD_NOTRAN))
	    outgo_post(&postfile, currboard);
	brc_addlist(postfile.filename);

	if (!(currbrdattr & BRD_HIDE) &&
	    (!bp->level || (currbrdattr & BRD_POSTMASK))) {
	    do_allpost(&postfile, fpath, owner);
	}
	outs("順利貼出佈告，");

#ifdef MAX_POST_MONEY
	aborted = (aborted > MAX_POST_MONEY) ? MAX_POST_MONEY : aborted;
#endif
	if (strcmp(currboard, "Test") && !ifuseanony) {
	    prints("這是您的第 %d 篇文章。 稿酬 %d 銀。",
		   ++cuser.numposts, aborted);
	    demoney(aborted);
	    passwd_update(usernum, &cuser);	/* post 數 */
	} else
	    outs("測試信件不列入紀錄，敬請包涵。");

	/* 回應到原作者信箱 */

	if (curredit & EDIT_BOTH) {
	    char           *str, *msg = "回應至作者信箱";

	    if ((str = strchr(quote_user, '.'))) {
		if (
#ifndef USE_BSMTP
		    bbs_sendmail(fpath, save_title, str + 1)
#else
		    bsmtp(fpath, save_title, str + 1, 0)
#endif
		    < 0)
		    msg = "作者無法收信";
	    } else {
		sethomepath(genbuf, quote_user);
		stampfile(genbuf, &postfile);
		unlink(genbuf);
		Link(fpath, genbuf);

		strlcpy(postfile.owner, cuser.userid, sizeof(postfile.owner));
		strlcpy(postfile.title, save_title, sizeof(postfile.title));
		postfile.filemode = FILE_BOTH;	/* both-reply flag */
		sethomedir(genbuf, quote_user);
		if (append_record(genbuf, &postfile, sizeof(postfile)) == -1)
		    msg = err_uid;
	    }
	    outs(msg);
	    curredit ^= EDIT_BOTH;
	}
	if (currbrdattr & BRD_ANONYMOUS)
	    do_unanonymous_post(fpath);
    }
    pressanykey();
    return FULLUPDATE;
}

int
do_post()
{
    boardheader_t  *bp;
    bp = getbcache(currbid);
    if (bp->brdattr & BRD_VOTEBOARD)
	return do_voteboard();
    else if (!(bp->brdattr & BRD_GROUPBOARD))
	return do_general();
    touchdircache(currbid);
    return 0;
}

static void
do_generalboardreply(fileheader_t * fhdr)
{
    char            genbuf[200];
    getdata(b_lines - 1, 0,
	    "▲ 回應至 (F)看板 (M)作者信箱 (B)二者皆是 (Q)取消？[F] ",
	    genbuf, 3, LCECHO);
    switch (genbuf[0]) {
    case 'm':
	mail_reply(0, fhdr, 0);
    case 'q':
	break;

    case 'b':
	curredit = EDIT_BOTH;
    default:
	strlcpy(currtitle, fhdr->title, sizeof(currtitle));
	strlcpy(quote_user, fhdr->owner, sizeof(quote_user));
	do_post();
    }
    *quote_file = 0;
}

int
getindex(char *fpath, char *fname, int size)
{
    int             fd, now = 0;
    fileheader_t    fhdr;

    if ((fd = open(fpath, O_RDONLY, 0)) != -1) {
	while ((read(fd, &fhdr, size) == size)) {
	    now++;
	    if (!strcmp(fhdr.filename, fname)) {
		close(fd);
		return now;
	    }
	}
	close(fd);
    }
    return 0;
}

int
invalid_brdname(char *brd)
{
    register char   ch;

    ch = *brd++;
    if (not_alnum(ch))
	return 1;
    while ((ch = *brd++)) {
	if (not_alnum(ch) && ch != '_' && ch != '-' && ch != '.')
	    return 1;
    }
    return 0;
}

static int
b_call_in(int ent, fileheader_t * fhdr, char *direct)
{
    userinfo_t     *u = search_ulist(searchuser(fhdr->owner));
    if (u) {
	int             fri_stat;
	fri_stat = friend_stat(currutmp, u);
	if (isvisible_stat(currutmp, u, fri_stat) && call_in(u, fri_stat))
	    return FULLUPDATE;
    }
    return DONOTHING;
}

static void
do_reply(fileheader_t * fhdr)
{
    boardheader_t  *bp;
    bp = getbcache(currbid);
    if (bp->brdattr & BRD_VOTEBOARD)
	do_voteboardreply(fhdr);
    else
	do_generalboardreply(fhdr);
}

static int
reply_post(int ent, fileheader_t * fhdr, char *direct)
{
    if (!(currmode & MODE_POST))
	return DONOTHING;

    setdirpath(quote_file, direct, fhdr->filename);
    do_reply(fhdr);
    *quote_file = 0;
    return FULLUPDATE;
}

static int
edit_post(int ent, fileheader_t * fhdr, char *direct)
{
    char            fpath[80], fpath0[80];
    char            genbuf[200];
    fileheader_t    postfile;
    boardheader_t  *bp;
    bp = getbcache(currbid);
    if (strcmp(bp->brdname, "Security") == 0)
	return DONOTHING;

    if (!HAS_PERM(PERM_SYSOP) && (bp->brdattr & BRD_VOTEBOARD))
	return DONOTHING;

    if ((!HAS_PERM(PERM_SYSOP)) &&
	strcmp(fhdr->owner, cuser.userid))
	return DONOTHING;

    if( currmode & MODE_SELECT )
	return DONOTHING;

    setutmpmode(REEDIT);
    setdirpath(genbuf, direct, fhdr->filename);
    local_article = fhdr->filemode & FILE_LOCAL;
    strlcpy(save_title, fhdr->title, sizeof(save_title));

    /* rocker.011018: 這裡是不是該檢查一下修改文章後的money和原有的比較? */
    if (vedit(genbuf, 0, NULL) != -1) {
	lock_substitute_record(direct, fhdr, sizeof(*fhdr), ent, LOCK_EX);
	setbpath(fpath, currboard);
	stampfile(fpath, &postfile);
	unlink(fpath);
	setbfile(fpath0, currboard, fhdr->filename);

	Rename(fpath0, fpath);

	/* rocker.011018: fix 串接模式改文章後文章就不見的bug */
	if ((currmode & MODE_SELECT) && (fhdr->money & FHR_REFERENCE)) {
	    fileheader_t    hdr;
	    int             num;

	    num = fhdr->money & ~FHR_REFERENCE;
	    setbdir(fpath0, currboard);
	    get_record(fpath0, &hdr, sizeof(hdr), num);

	    /* 再這裡要check一下原來的dir裡面是不是有被人動過... */
	    if (!strcmp(hdr.filename, fhdr->filename)) {
		strlcpy(hdr.filename, postfile.filename, sizeof(hdr.filename));
		strlcpy(hdr.title, save_title, sizeof(hdr.title));
		substitute_record(fpath0, &hdr, sizeof(hdr), num);
	    }
	}
	strlcpy(fhdr->filename, postfile.filename, sizeof(fhdr->filename));
	strlcpy(fhdr->title, save_title, sizeof(fhdr->title));
	brc_addlist(postfile.filename);
	lock_substitute_record(direct, fhdr, sizeof(*fhdr), ent, LOCK_UN);
	/* rocker.011018: 順便更新一下cache */
	touchdircache(currbid);
    }

    if (!(currbrdattr & BRD_HIDE) && (!bp->level || (currbrdattr & BRD_POSTMASK)))
	do_allpost(&postfile, fpath, cuser.userid);
    return FULLUPDATE;
}

#define UPDATE_USEREC   (currmode |= MODE_DIRTY)

static int
cross_post(int ent, fileheader_t * fhdr, char *direct)
{
    char            xboard[20], fname[80], xfpath[80], xtitle[80], inputbuf[10];
    fileheader_t    xfile;
    FILE           *xptr;
    int             author = 0;
    char            genbuf[200];
    char            genbuf2[4];
    boardheader_t  *bp;
    move(2, 0);
    clrtoeol();
    move(3, 0);
    clrtoeol();
    move(1, 0);
    bp = getbcache(currbid);
    if (bp && (bp->brdattr & BRD_VOTEBOARD))
	return FULLUPDATE;
    generalnamecomplete("轉錄本文章於看板：", xboard, sizeof(xboard),
			SHM->Bnumber,
			completeboard_compar,
			completeboard_permission,
			completeboard_getname);
    if (*xboard == '\0' || !haspostperm(xboard))
	return FULLUPDATE;

    if ((ent = str_checksum(fhdr->title)) != 0 &&
	ent == postrecord.checksum[0]) {
	/* 檢查 cross post 次數 */
	if (postrecord.times++ > MAX_CROSSNUM)
	    anticrosspost();
    } else {
	postrecord.times = 0;
	postrecord.checksum[0] = ent;
    }

    ent = 1;
    if (HAS_PERM(PERM_SYSOP) || !strcmp(fhdr->owner, cuser.userid)) {
	getdata(2, 0, "(1)原文轉載 (2)舊轉錄格式？[1] ",
		genbuf, 3, DOECHO);
	if (genbuf[0] != '2') {
	    ent = 0;
	    getdata(2, 0, "保留原作者名稱嗎?[Y] ", inputbuf, 3, DOECHO);
	    if (inputbuf[0] != 'n' && inputbuf[0] != 'N')
		author = 1;
	}
    }
    if (ent)
	snprintf(xtitle, sizeof(xtitle), "[轉錄]%.66s", fhdr->title);
    else
	strlcpy(xtitle, fhdr->title, sizeof(xtitle));

    snprintf(genbuf, sizeof(genbuf), "採用原標題《%.60s》嗎?[Y] ", xtitle);
    getdata(2, 0, genbuf, genbuf2, 4, LCECHO);
    if (genbuf2[0] == 'n' || genbuf2[0] == 'N') {
	if (getdata_str(2, 0, "標題：", genbuf, TTLEN, DOECHO, xtitle))
	    strlcpy(xtitle, genbuf, sizeof(xtitle));
    }
    getdata(2, 0, "(S)存檔 (L)站內 (Q)取消？[Q] ", genbuf, 3, LCECHO);
    if (genbuf[0] == 'l' || genbuf[0] == 's') {
	int             currmode0 = currmode;

	currmode = 0;
	setbpath(xfpath, xboard);
	stampfile(xfpath, &xfile);
	if (author)
	    strlcpy(xfile.owner, fhdr->owner, sizeof(xfile.owner));
	else
	    strlcpy(xfile.owner, cuser.userid, sizeof(xfile.owner));
	strlcpy(xfile.title, xtitle, sizeof(xfile.title));
	if (genbuf[0] == 'l') {
	    xfile.filemode = FILE_LOCAL;
	}
	setbfile(fname, currboard, fhdr->filename);
	xptr = fopen(xfpath, "w");

	strlcpy(save_title, xfile.title, sizeof(save_title));
	strlcpy(xfpath, currboard, sizeof(xfpath));
	strlcpy(currboard, xboard, sizeof(currboard));
	write_header(xptr);
	strlcpy(currboard, xfpath, sizeof(currboard));

	fprintf(xptr, "※ [本文轉錄自 %s 看板]\n\n", currboard);

	b_suckinfile(xptr, fname);
	addsignature(xptr, 0);
	fclose(xptr);
	/*
	 * Cross fs有問題 } else { unlink(xfpath); link(fname, xfpath); }
	 */
	setbdir(fname, xboard);
	append_record(fname, &xfile, sizeof(xfile));
	bp = getbcache(getbnum(xboard));
	if (!xfile.filemode && !(bp->brdattr && BRD_NOTRAN))
	    outgo_post(&xfile, xboard);
	setbtotal(getbnum(xboard));
	cuser.numposts++;
	UPDATE_USEREC;
	outs("文章轉錄完成");
	pressanykey();
	currmode = currmode0;
    }
    return FULLUPDATE;
}
static int
read_post(int ent, fileheader_t * fhdr, char *direct)
{
    char            genbuf[200];
    int             more_result;

    if (fhdr->owner[0] == '-')
	return DONOTHING;

    setdirpath(genbuf, direct, fhdr->filename);

    if ((more_result = more(genbuf, YEA)) == -1)
	return DONOTHING;

    brc_addlist(fhdr->filename);
    strncpy(currtitle, subject(fhdr->title), TTLEN);
    strncpy(currowner, subject(fhdr->owner), IDLEN + 2);

    switch (more_result) {
    case 1:
	return READ_PREV;
    case 2:
	return RELATE_PREV;
    case 3:
	return READ_NEXT;
    case 4:
	return RELATE_NEXT;
    case 5:
	return RELATE_FIRST;
    case 6:
	return FULLUPDATE;
    case 7:
    case 8:
	if ((currmode & MODE_POST)) {
	    strlcpy(quote_file, genbuf, sizeof(quote_file));
	    do_reply(fhdr);
	    *quote_file = 0;
	}
	return FULLUPDATE;
    case 9:
	return 'A';
    case 10:
	return 'a';
    case 11:
	return '/';
    case 12:
	return '?';
    }


    outmsg("\033[34;46m  閱\讀文章  \033[31;47m  (R/Y)\033[30m回信 \033[31m"
	 "(=[]<>)\033[30m相關主題 \033[31m(↑↓)\033[30m上下封 \033[31m(←)"
	   "\033[30m離開  \033[m");

    switch (egetch()) {
    case 'q':
    case 'Q':
    case KEY_LEFT:
	break;

    case ' ':
    case KEY_RIGHT:
    case KEY_DOWN:
    case KEY_PGDN:
    case 'n':
    case Ctrl('N'):
	return READ_NEXT;

    case KEY_UP:
    case 'p':
    case Ctrl('P'):
    case KEY_PGUP:
	return READ_PREV;

    case '=':
	return RELATE_FIRST;

    case ']':
    case 't':
	return RELATE_NEXT;

    case '[':
	return RELATE_PREV;

    case '.':
    case '>':
	return THREAD_NEXT;

    case ',':
    case '<':
	return THREAD_PREV;

    case Ctrl('C'):
	cal();
	return FULLUPDATE;

    case Ctrl('I'):
	t_idle();
	return FULLUPDATE;
	
    case 'X':
	recommend(ent, fhdr, direct);
	return FULLUPDATE;

    case 'y':
    case 'r':
    case 'R':
    case 'Y':
	if ((currmode & MODE_POST)) {
	    strlcpy(quote_file, genbuf, sizeof(quote_file));
	    do_reply(fhdr);
	    *quote_file = 0;
	}
    }
    return FULLUPDATE;
}

/* ----------------------------------------------------- */
/* 採集精華區                                            */
/* ----------------------------------------------------- */
static int
b_man()
{
    char            buf[64];

    setapath(buf, currboard);
    if ((currmode & MODE_BOARD) || HAS_PERM(PERM_SYSOP)) {
	char            genbuf[128];
	int             fd;
	snprintf(genbuf, sizeof(genbuf), "%s/.rebuild", buf);
	if ((fd = open(genbuf, O_CREAT, 0640)) > 0)
	    close(fd);
    }
    return a_menu(currboard, buf, HAS_PERM(PERM_ALLBOARD) ? 2 :
		  (currmode & MODE_BOARD ? 1 : 0));
}

#ifndef NO_GAMBLE
void
stop_gamble()
{
    boardheader_t  *bp = getbcache(currbid);
    char            fn_ticket[128], fn_ticket_end[128];
    if (!bp->endgamble || bp->endgamble > now)
	return;

    setbfile(fn_ticket, currboard, FN_TICKET);
    setbfile(fn_ticket_end, currboard, FN_TICKET_END);

    rename(fn_ticket, fn_ticket_end);
    if (bp->endgamble) {
	bp->endgamble = 0;
	substitute_record(fn_board, bp, sizeof(boardheader_t), currbid);
    }
}
static int
join_gamble(int ent, fileheader_t * fhdr, char *direct)
{
    if (!HAS_PERM(PERM_LOGINOK))
	return DONOTHING;
    stop_gamble();
    ticket(currbid);
    return FULLUPDATE;
}
static int
hold_gamble(int ent, fileheader_t * fhdr, char *direct)
{
    char            fn_ticket[128], fn_ticket_end[128], genbuf[128], msg[256] = "",
                    yn[10] = "";
    boardheader_t  *bp = getbcache(currbid);
    int             i;
    FILE           *fp = NULL;

    if (!(currmode & MODE_BOARD))
	return 0;
    setbfile(fn_ticket, currboard, FN_TICKET);
    setbfile(fn_ticket_end, currboard, FN_TICKET_END);
    setbfile(genbuf, currboard, FN_TICKET_LOCK);
    if (dashf(fn_ticket)) {
	getdata(b_lines - 1, 0, "已經有舉辦賭盤, "
		"是否要 [停止下注]?(N/y)：", yn, 3, LCECHO);
	if (yn[0] != 'y')
	    return FULLUPDATE;
	rename(fn_ticket, fn_ticket_end);
	if (bp->endgamble) {
	    bp->endgamble = 0;
	    substitute_record(fn_board, bp, sizeof(boardheader_t), currbid);

	}
	return FULLUPDATE;
    }
    if (dashf(fn_ticket_end)) {
	getdata(b_lines - 1, 0, "已經有舉辦賭盤, "
		"是否要開獎 [否/是]?(N/y)：", yn, 3, LCECHO);
	if (yn[0] != 'y')
	    return FULLUPDATE;
	openticket(currbid);
	return FULLUPDATE;
    } else if (dashf(genbuf)) {
	move(b_lines - 1, 0);
	prints(" 目前系統正在處理開獎事宜, 請結果出爐後再舉辦.......");
	pressanykey();
	return FULLUPDATE;
    }
    getdata(b_lines - 2, 0, "要舉辦賭盤 (N/y):", yn, 3, LCECHO);
    if (yn[0] != 'y')
	return FULLUPDATE;
    getdata(b_lines - 1, 0, "賭什麼? 請輸入主題 (輸入後編輯內容):",
	    msg, 20, DOECHO);
    if (msg[0] == 0 ||
	vedit(fn_ticket_end, NA, NULL) < 0)
	return FULLUPDATE;

    clear();
    showtitle("舉辦賭盤", BBSNAME);
    setbfile(genbuf, currboard, FN_TICKET_ITEMS);

    //sprintf(genbuf, "%s/" FN_TICKET_ITEMS, direct);

    if (!(fp = fopen(genbuf, "w")))
	return FULLUPDATE;
    do {
	getdata(2, 0, "輸入彩票價格 (價格:10-10000):", yn, 6, LCECHO);
	i = atoi(yn);
    } while (i < 10 || i > 10000);
    fprintf(fp, "%d\n", i);
    if (!getdata(3, 0, "設定自動封盤時間?(Y/n)", yn, 3, LCECHO) || yn[0] != 'n') {
	bp->endgamble = gettime(4, now);
	substitute_record(fn_board, bp, sizeof(boardheader_t), currbid);
    }
    move(6, 0);
    snprintf(genbuf, sizeof(genbuf),
	     "請到 %s 板 按'f'參與賭博!\n\n一張 %d Ptt幣, 這是%s的賭博\n%s%s",
	     currboard,
	     i, i < 100 ? "小賭式" : i < 500 ? "平民級" :
	     i < 1000 ? "貴族級" : i < 5000 ? "富豪級" : "傾家蕩產",
	     bp->endgamble ? "賭盤結束時間: " : "",
	     bp->endgamble ? Cdate(&bp->endgamble) : ""
	     );
    strcat(msg, genbuf);
    prints("請依次輸入彩票名稱, 需提供2~8項. (未滿八項, 輸入直接按enter)\n");
    for (i = 0; i < 8; i++) {
	snprintf(yn, sizeof(yn), " %d)", i + 1);
	getdata(7 + i, 0, yn, genbuf, 9, DOECHO);
	if (!genbuf[0] && i > 1)
	    break;
	fprintf(fp, "%s\n", genbuf);
    }
    fclose(fp);
    move(8 + i, 0);
    prints("賭盤設定完成");
    snprintf(genbuf, sizeof(genbuf), "[公告] %s 板 開始賭博!", currboard);
    post_msg(currboard, genbuf, msg, cuser.userid);
    post_msg("Record", genbuf + 7, msg, "[馬路探子]");
    /* Tim 控制CS, 以免正在玩的user把資料已經寫進來 */
    rename(fn_ticket_end, fn_ticket);
    /* 設定完才把檔名改過來 */

    return FULLUPDATE;
}
#endif

static int
cite_post(int ent, fileheader_t * fhdr, char *direct)
{
    char            fpath[256];
    char            title[TTLEN + 1];

    setbfile(fpath, currboard, fhdr->filename);
    strlcpy(title, "◇ ", sizeof(title));
    strlcpy(title + 3, fhdr->title, TTLEN - 3);
    title[TTLEN] = '\0';
    a_copyitem(fpath, title, 0, 1);
    b_man();
    return FULLUPDATE;
}

int
edit_title(int ent, fileheader_t * fhdr, char *direct)
{
    char            genbuf[200];
    fileheader_t    tmpfhdr = *fhdr;
    int             dirty = 0;

    if (currmode & MODE_BOARD || !strcmp(cuser.userid, fhdr->owner)) {
	if (getdata(b_lines - 1, 0, "標題：", genbuf, TTLEN, DOECHO)) {
	    strlcpy(tmpfhdr.title, genbuf, sizeof(tmpfhdr.title));
	    dirty++;
	}
    }
    if (HAS_PERM(PERM_SYSOP)) {
	if (getdata(b_lines - 1, 0, "作者：", genbuf, IDLEN + 2, DOECHO)) {
	    strlcpy(tmpfhdr.owner, genbuf, sizeof(tmpfhdr.owner));
	    dirty++;
	}
	if (getdata(b_lines - 1, 0, "日期：", genbuf, 6, DOECHO)) {
	    snprintf(tmpfhdr.date, sizeof(tmpfhdr.date), "%.5s", genbuf);
	    dirty++;
	}
    }
    if (currmode & MODE_BOARD || !strcmp(cuser.userid, fhdr->owner)) {
	getdata(b_lines - 1, 0, "確定(Y/N)?[n] ", genbuf, 3, DOECHO);
	if ((genbuf[0] == 'y' || genbuf[0] == 'Y') && dirty) {
	    *fhdr = tmpfhdr;
	    substitute_record(direct, fhdr, sizeof(*fhdr), ent);
	    /* rocker.011018: 這裡應該改成用reference的方式取得原來的檔案 */
	    substitute_check(fhdr);
	    touchdircache(currbid);
	}
	return FULLUPDATE;
    }
    return DONOTHING;
}

static int
solve_post(int ent, fileheader_t * fhdr, char *direct)
{
    if (HAS_PERM(PERM_SYSOP)) {
	fhdr->filemode ^= FILE_SOLVED;
	substitute_record(direct, fhdr, sizeof(*fhdr), ent);
	touchdircache(currbid);
	return PART_REDRAW;
    }
    return DONOTHING;
}

static int
recommend_cancel(int ent, fileheader_t * fhdr, char *direct)
{
    char            yn[5];
    if (!(currmode & MODE_BOARD))
	return DONOTHING;
    getdata(b_lines - 1, 0, "確定要推薦歸零(Y/N)?[n] ", yn, 5, LCECHO);
    if (yn[0] != 'y')
	return FULLUPDATE;
    fhdr->recommend = 0;

    substitute_record(direct, fhdr, sizeof(*fhdr), ent);
    substitute_check(fhdr);
    touchdircache(currbid);
    return FULLUPDATE;
}

static int
recommend(int ent, fileheader_t * fhdr, char *direct)
{
    struct tm      *ptime = localtime(&now);
    char            buf[200], path[200], yn[5];
    boardheader_t  *bp;
    static time_t   lastrecommend = 0;

    bp = getbcache(currbid);
    if( bp->brdattr & BRD_NORECOMMEND ){
	vmsg("抱歉, 本板禁止推薦");
	return FULLUPDATE;
    }
    if (!(currmode & MODE_POST) || bp->brdattr & BRD_VOTEBOARD) {
	vmsg("您因權限不足無法推薦!");
	return FULLUPDATE;
    }

    setdirpath(path, direct, fhdr->filename);
    if (fhdr->recommend > 9 || fhdr->recommend < 0)
	/* 暫時性的 code 原來舊有值取消 */
	fhdr->recommend = 0;

    if (fhdr->recommend == 0 && strcmp(cuser.userid, fhdr->owner) == 0){
	vmsg("警告! 本人不能推薦第一次!");
	return FULLUPDATE;
    }
#ifndef DEBUG
    if (!(currmode & MODE_BOARD) && getuser(cuser.userid) &&
	now - lastrecommend < 40) {
	move(b_lines - 1, 0);
	prints("離上次推薦時間太近囉, 請多花點時間仔細閱\讀文章!");
	pressanykey();
	return FULLUPDATE;
    }
#endif

    if (!getdata(b_lines - 2, 0, "推薦語:", path, 40, DOECHO) ||
	    path == NULL ||
	!getdata(b_lines - 1, 0, "確定要推薦, 請仔細考慮(Y/N)?[n] ",
		 yn, 5, LCECHO)
	|| yn[0] != 'y')
	return FULLUPDATE;

    snprintf(buf, sizeof(buf),
	     "\033[1;31m→ \033[33m%s\033[m\033[33m:%s\033[m%*s推%15s %02d/%02d\n",
	     cuser.userid, path,
	     51 - strlen(cuser.userid) - strlen(path), " ", fromhost,
	     ptime->tm_mon + 1, ptime->tm_mday);
    lock_substitute_record(direct, fhdr, sizeof(*fhdr), ent, LOCK_EX);
    setdirpath(path, direct, fhdr->filename);
    log_file(path, buf);
    if (!(fhdr->recommend < 9))
	lock_substitute_record(direct, fhdr, sizeof(*fhdr), ent, LOCK_UN);
    else{
	fhdr->recommend++;
	lastrecommend = now;
	passwd_update(usernum, &cuser);
	lock_substitute_record(direct, fhdr, sizeof(*fhdr), ent, LOCK_UN);
	substitute_check(fhdr);
	touchdircache(currbid);
    }
    return FULLUPDATE;
}

static int
mark_post(int ent, fileheader_t * fhdr, char *direct)
{

    if (!(currmode & MODE_BOARD))
	return DONOTHING;

    fhdr->filemode ^= FILE_MARKED;
    substitute_record(direct, fhdr, sizeof(*fhdr), ent);
    substitute_check(fhdr);
    touchdircache(currbid);
    return PART_REDRAW;
}

int
del_range(int ent, fileheader_t * fhdr, char *direct)
{
    char            num1[8], num2[8];
    int             inum1, inum2;
    boardheader_t  *bp;
    bp = getbcache(currbid);

    /* rocker.011018: 串接模式下還是不允許刪除比較好 */
    if (currmode & MODE_SELECT) {
	outmsg("請先回到正常模式後再進行刪除...");
	refresh();
	/* safe_sleep(1); */
	return FULLUPDATE;
    }
    if (strcmp(bp->brdname, "Security") == 0)
	return DONOTHING;
    if ((currstat != READING) || (currmode & MODE_BOARD)) {
	getdata(1, 0, "[設定刪除範圍] 起點：", num1, 5, DOECHO);
	inum1 = atoi(num1);
	if (inum1 <= 0) {
	    outmsg("起點有誤");
	    refresh();
	    /* safe_sleep(1); */
	    return FULLUPDATE;
	}
	getdata(1, 28, "終點：", num2, 5, DOECHO);
	inum2 = atoi(num2);
	if (inum2 < inum1) {
	    outmsg("終點有誤");
	    refresh();
	    /* safe_sleep(1); */
	    return FULLUPDATE;
	}
	getdata(1, 48, msg_sure_ny, num1, 3, LCECHO);
	if (*num1 == 'y') {
	    outmsg("處理中,請稍後...");
	    refresh();
	    if (currmode & MODE_SELECT) {
		int             fd, size = sizeof(fileheader_t);
		char            genbuf[100];
		fileheader_t    rsfh;
		int             i = inum1, now;
		if (currstat == RMAIL)
		    sethomedir(genbuf, cuser.userid);
		else
		    setbdir(genbuf, currboard);
		if ((fd = (open(direct, O_RDONLY, 0))) != -1) {
		    if (lseek(fd, (off_t) (size * (inum1 - 1)), SEEK_SET) !=
			-1) {
			while (read(fd, &rsfh, size) == size) {
			    if (i > inum2)
				break;
			    now = getindex(genbuf, rsfh.filename, size);
			    strlcpy(currfile, rsfh.filename, sizeof(currfile));
			    delete_file(genbuf, sizeof(fileheader_t), now,
					cmpfilename);
			    i++;
			}
		    }
		    close(fd);
		}
	    }
	    delete_range(direct, inum1, inum2);
	    fixkeep(direct, inum1);

	    if (currmode & MODE_BOARD)
		setbtotal(currbid);

	    return DIRCHANGED;
	}
	return FULLUPDATE;
    }
    return DONOTHING;
}

static int
del_post(int ent, fileheader_t * fhdr, char *direct)
{
    char            genbuf[100];
    int             not_owned;
    boardheader_t  *bp;

    bp = getbcache(currbid);
    if (strcmp(bp->brdname, "Security") == 0)
	return DONOTHING;
    if ((fhdr->filemode & FILE_MARKED) || (fhdr->filemode & FILE_DIGEST) ||
	(fhdr->owner[0] == '-'))
	return DONOTHING;

    not_owned = strcmp(fhdr->owner, cuser.userid);
    if ((!(currmode & MODE_BOARD) && not_owned) ||
	((bp->brdattr & BRD_VOTEBOARD) && !HAS_PERM(PERM_SYSOP)) ||
	!strcmp(cuser.userid, STR_GUEST))
	return DONOTHING;

    getdata(1, 0, msg_del_ny, genbuf, 3, LCECHO);
    if (genbuf[0] == 'y' || genbuf[0] == 'Y') {
	strlcpy(currfile, fhdr->filename, sizeof(currfile));
	if (!delete_file(direct, sizeof(fileheader_t), ent, cmpfilename)) {
	    if (currmode & MODE_SELECT) {
		/* rocker.011018: 利用reference減低loading */
		fileheader_t    hdr;
		int             num;

		num = fhdr->money & ~FHR_REFERENCE;
		setbdir(genbuf, currboard);
		get_record(genbuf, &hdr, sizeof(hdr), num);

		/* 再這裡要check一下原來的dir裡面是不是有被人動過... */
		if (strcmp(hdr.filename, fhdr->filename)) {
		    num = getindex(genbuf, fhdr->filename, sizeof(fileheader_t));
		    get_record(genbuf, &hdr, sizeof(hdr), num);
		}
		/* rocker.011018: 這裡要還原被破壞的money */
		fhdr->money = hdr.money;
		delete_file(genbuf, sizeof(fileheader_t), num, cmpfilename);
	    }
	    cancelpost(fhdr, not_owned);

	    setbtotal(currbid);
	    if (fhdr->money < 0)
		fhdr->money = 0;
	    if (not_owned && strcmp(currboard, "Test")) {
		deumoney(searchuser(fhdr->owner), -fhdr->money);
	    }
	    if (!not_owned && strcmp(currboard, "Test")) {
		if (cuser.numposts)
		    cuser.numposts--;
		move(b_lines - 1, 0);
		clrtoeol();
		demoney(-fhdr->money);
		passwd_update(usernum, &cuser);	/* post 數 */
		prints("%s，您的文章減為 %d 篇，支付清潔費 %d 銀", msg_del_ok,
		       cuser.numposts, fhdr->money);
		refresh();
		pressanykey();
	    }
	    return DIRCHANGED;
	}
    }
    return FULLUPDATE;
}

static int
view_postmoney(int ent, fileheader_t * fhdr, char *direct)
{
    move(b_lines - 1, 0);
    clrtoeol();
    prints("這一篇文章值 %d 銀", fhdr->money);
    refresh();
    pressanykey();
    return FULLUPDATE;
}

#ifdef OUTJOBSPOOL
/* 看板備份 */
static int
tar_addqueue(int ent, fileheader_t * fhdr, char *direct)
{
    char            email[60], qfn[80], ans[2];
    FILE           *fp;
    char            bakboard, bakman;
    clear();
    showtitle("看板備份", BBSNAME);
    move(2, 0);
    if (!((currmode & MODE_BOARD) || HAS_PERM(PERM_SYSOP))) {
	move(5, 10);
	outs("妳要是板主或是站長才能醬醬啊 -.-\"\"");
	pressanykey();
	return FULLUPDATE;
    }
    snprintf(qfn, sizeof(qfn), BBSHOME "/jobspool/tarqueue.%s", currboard);
    if (access(qfn, 0) == 0) {
	outs("已經排定行程, 稍後會進行備份");
	pressanykey();
	return FULLUPDATE;
    }
    if (!getdata(4, 0, "請輸入目的信箱：", email, sizeof(email), DOECHO))
	return FULLUPDATE;

    /* check email -.-"" */
    if (strstr(email, "@") == NULL || strstr(email, ".bbs@") != NULL) {
	move(6, 0);
	outs("您指定的信箱不正確! ");
	pressanykey();
	return FULLUPDATE;
    }
    getdata(6, 0, "要備份看板內容嗎(Y/N)?[Y]", ans, sizeof(ans), LCECHO);
    bakboard = (ans[0] == 'n' || ans[0] == 'N') ? 0 : 1;
    getdata(7, 0, "要備份精華區內容嗎(Y/N)?[N]", ans, sizeof(ans), LCECHO);
    bakman = (ans[0] == 'y' || ans[0] == 'Y') ? 1 : 0;
    if (!bakboard && !bakman) {
	move(8, 0);
	outs("可是我們只能備份看板或精華區的耶 ^^\"\"\"");
	pressanykey();
	return FULLUPDATE;
    }
    fp = fopen(qfn, "w");
    fprintf(fp, "%s\n", cuser.userid);
    fprintf(fp, "%s\n", email);
    fprintf(fp, "%d,%d\n", bakboard, bakman);
    fclose(fp);

    move(10, 0);
    outs("系統已經將您的備份排入行程, \n");
    outs("稍後將會在系統負荷較低的時候將資料寄給您~ :) ");
    pressanykey();
    return FULLUPDATE;
}
#endif

static int      sequent_ent;
static int      continue_flag;

/* ----------------------------------------------------- */
/* 依序讀新文章                                          */
/* ----------------------------------------------------- */
static int
sequent_messages(fileheader_t * fptr)
{
    static int      idc;
    char            genbuf[200];

    if (fptr == NULL)
	return (idc = 0);

    if (++idc < sequent_ent)
	return 0;

    if (!brc_unread(fptr->filename, brc_num, brc_list))
	return 0;

    if (continue_flag)
	genbuf[0] = 'y';
    else {
	prints("讀取文章於：[%s] 作者：[%s]\n標題：[%s]",
	       currboard, fptr->owner, fptr->title);
	getdata(3, 0, "(Y/N/Quit) [Y]: ", genbuf, 3, LCECHO);
    }

    if (genbuf[0] != 'y' && genbuf[0]) {
	clear();
	return (genbuf[0] == 'q' ? QUIT : 0);
    }
    setbfile(genbuf, currboard, fptr->filename);
    brc_addlist(fptr->filename);

    if (more(genbuf, YEA) == 0)
	outmsg("\033[31;47m  \033[31m(R)\033[30m回信  \033[31m(↓,n)"
	       "\033[30m下一封  \033[31m(←,q)\033[30m離開  \033[m");
    continue_flag = 0;

    switch (egetch()) {
    case KEY_LEFT:
    case 'e':
    case 'q':
    case 'Q':
	break;

    case 'y':
    case 'r':
    case 'Y':
    case 'R':
	if (currmode & MODE_POST) {
	    strlcpy(quote_file, genbuf, sizeof(quote_file));
	    do_reply(fptr);
	    *quote_file = 0;
	}
	break;

    case ' ':
    case KEY_DOWN:
    case '\n':
    case 'n':
	continue_flag = 1;
    }

    clear();
    return 0;
}

static int
sequential_read(int ent, fileheader_t * fhdr, char *direct)
{
    char            buf[40];

    clear();
    sequent_messages((fileheader_t *) NULL);
    sequent_ent = ent;
    continue_flag = 0;
    setbdir(buf, currboard);
    apply_record(buf, sequent_messages, sizeof(fileheader_t));
    return FULLUPDATE;
}

/* ----------------------------------------------------- */
/* 看板備忘錄、文摘、精華區                              */
/* ----------------------------------------------------- */
int
b_note_edit_bname(int bid)
{
    char            buf[64];
    int             aborted;
    boardheader_t  *fh = getbcache(bid);
    setbfile(buf, fh->brdname, fn_notes);
    aborted = vedit(buf, NA, NULL);
    if (aborted == -1) {
	clear();
	outs(msg_cancel);
	pressanykey();
    } else {
	if (!getdata(2, 0, "設定有效期限天？(n/Y)", buf, 3, LCECHO)
	    || buf[0] != 'n')
	    fh->bupdate = gettime(3, fh->bupdate ? fh->bupdate : now);
	else
	    fh->bupdate = 0;
	substitute_record(fn_board, fh, sizeof(boardheader_t), bid);
    }
    return 0;
}

static int
b_notes_edit()
{
    if (currmode & MODE_BOARD) {
	b_note_edit_bname(currbid);
	return FULLUPDATE;
    }
    return 0;
}

static int
b_water_edit()
{
    if (currmode & MODE_BOARD) {
	friend_edit(BOARD_WATER);
	return FULLUPDATE;
    }
    return 0;
}

static int
visable_list_edit()
{
    if (currmode & MODE_BOARD) {
	friend_edit(BOARD_VISABLE);
	hbflreload(currbid);
	return FULLUPDATE;
    }
    return 0;
}

static int
b_post_note()
{
    char            buf[200], yn[3];

    if (currmode & MODE_BOARD) {
	setbfile(buf, currboard, FN_POST_NOTE);
	if (more(buf, NA) == -1)
	    more("etc/" FN_POST_NOTE, NA);
	getdata(b_lines - 2, 0, "是否要用自訂post注意事項?", yn, sizeof(yn), LCECHO);
	if (yn[0] == 'y')
	    vedit(buf, NA, NULL);
	else
	    unlink(buf);
	return FULLUPDATE;
    }
    return 0;
}

static int
b_application()
{
    char            buf[200];

    if (currmode & MODE_BOARD) {
	setbfile(buf, currboard, FN_APPLICATION);
	vedit(buf, NA, NULL);
	return FULLUPDATE;
    }
    return 0;
}

static int
can_vote_edit()
{
    if (currmode & MODE_BOARD) {
	friend_edit(FRIEND_CANVOTE);
	return FULLUPDATE;
    }
    return 0;
}

static int
bh_title_edit()
{
    boardheader_t  *bp;

    if (currmode & MODE_BOARD) {
	char            genbuf[BTLEN];

	bp = getbcache(currbid);
	move(1, 0);
	clrtoeol();
	getdata_str(1, 0, "請輸入看板新中文敘述:", genbuf,
		    BTLEN - 16, DOECHO, bp->title + 7);

	if (!genbuf[0])
	    return 0;
	strip_ansi(genbuf, genbuf, 0);
	strlcpy(bp->title + 7, genbuf, sizeof(bp->title) - 7);
	substitute_record(fn_board, bp, sizeof(boardheader_t), currbid);
	log_usies("SetBoard", currboard);
	return FULLUPDATE;
    }
    return 0;
}

static int
b_notes()
{
    char            buf[64];

    setbfile(buf, currboard, fn_notes);
    if (more(buf, NA) == -1) {
	clear();
	move(4, 20);
	outs("本看板尚無「備忘錄」。");
    }
    pressanykey();
    return FULLUPDATE;
}

int
board_select()
{
    char            fpath[80];
    char            genbuf[100];

    currmode &= ~MODE_SELECT;
    snprintf(fpath, sizeof(fpath), "SR.%s", cuser.userid);
    setbfile(genbuf, currboard, fpath);
    unlink(genbuf);
    if (currstat == RMAIL)
	sethomedir(currdirect, cuser.userid);
    else
	setbdir(currdirect, currboard);
    return NEWDIRECT;
}

int
board_digest()
{
    if (currmode & MODE_SELECT)
	board_select();
    currmode ^= MODE_DIGEST;
    if (currmode & MODE_DIGEST)
	currmode &= ~MODE_POST;
    else if (haspostperm(currboard))
	currmode |= MODE_POST;

    setbdir(currdirect, currboard);
    return NEWDIRECT;
}

int
board_etc()
{
    if (!HAS_PERM(PERM_SYSOP))
	return DONOTHING;
    currmode ^= MODE_ETC;
    if (currmode & MODE_ETC)
	currmode &= ~MODE_POST;
    else if (haspostperm(currboard))
	currmode |= MODE_POST;

    setbdir(currdirect, currboard);
    return NEWDIRECT;
}

static int
good_post(int ent, fileheader_t * fhdr, char *direct)
{
    char            genbuf[200];
    char            genbuf2[200];
    int             delta = 0;

    if ((currmode & MODE_DIGEST) || !(currmode & MODE_BOARD))
	return DONOTHING;

    if (fhdr->filemode & FILE_DIGEST) {
	fhdr->filemode = (fhdr->filemode & ~FILE_DIGEST);
	if (!strcmp(currboard, "Note") || !strcmp(currboard, "PttBug") ||
	    !strcmp(currboard, "Artdsn") || !strcmp(currboard, "PttLaw")) {
	    deumoney(searchuser(fhdr->owner), -1000);
	    if (!(currmode & MODE_SELECT))
		fhdr->money -= 1000;
	    else
		delta = -1000;
	}
    } else {
	fileheader_t    digest;
	char           *ptr, buf[64];

	memcpy(&digest, fhdr, sizeof(digest));
	digest.filename[0] = 'G';
	strlcpy(buf, direct, sizeof(buf));
	ptr = strrchr(buf, '/') + 1;
	ptr[0] = '\0';
	snprintf(genbuf, sizeof(genbuf), "%s%s", buf, digest.filename);

	if (dashf(genbuf))
	    unlink(genbuf);

	digest.filemode = 0;
	snprintf(genbuf2, sizeof(genbuf2), "%s%s", buf, fhdr->filename);
	Link(genbuf2, genbuf);
	strcpy(ptr, fn_mandex);
	append_record(buf, &digest, sizeof(digest));

	fhdr->filemode = (fhdr->filemode & ~FILE_MARKED) | FILE_DIGEST;
	if (!strcmp(currboard, "Note") || !strcmp(currboard, "PttBug") ||
	    !strcmp(currboard, "Artdsn") || !strcmp(currboard, "PttLaw")) {
	    deumoney(searchuser(fhdr->owner), 1000);
	    if (!(currmode & MODE_SELECT))
		fhdr->money += 1000;
	    else
		delta = 1000;
	}
    }
    substitute_record(direct, fhdr, sizeof(*fhdr), ent);
    touchdircache(currbid);
    /* rocker.011018: 串接模式用reference增進效率 */
    if ((currmode & MODE_SELECT) && (fhdr->money & FHR_REFERENCE)) {
	fileheader_t    hdr;
	char            genbuf[100];
	int             num;

	num = fhdr->money & ~FHR_REFERENCE;
	setbdir(genbuf, currboard);
	get_record(genbuf, &hdr, sizeof(hdr), num);

	/* 再這裡要check一下原來的dir裡面是不是有被人動過... */
	if (strcmp(hdr.filename, fhdr->filename)) {
	    num = getindex(genbuf, fhdr->filename, sizeof(fileheader_t));
	    get_record(genbuf, &hdr, sizeof(hdr), num);
	}
	fhdr->money = hdr.money + delta;

	substitute_record(genbuf, fhdr, sizeof(*fhdr), num);
    }
    return PART_REDRAW;
}

/* help for board reading */
static char    *board_help[] = {
    "\0全功\能看板操作說明",
    "\01基本命令",
    "(p)(↑)   上移一篇文章         (^P)     發表文章",
    "(n)(↓)   下移一篇文章         (d)      刪除文章",
    "(P)(PgUp) 上移一頁             (S)      串連相關文章",
    "(N)(PgDn) 下移一頁             (##)     跳到 ## 號文章",
    "(r)(→)   閱\讀此篇文章         ($)      跳到最後一篇文章",
    "\01進階命令",
    "(tab)/z   文摘模式/精華區      (a/A)(^Q)找尋作者/作者資料",
    "(b/f)     展讀備忘錄/參與賭盤  (?)(/)   找尋標題",
    "(V/R)     投票/查詢投票結果    (^W)(X)  我在哪裡/推薦文章",
    "(x)(w)    轉錄文章/丟水球      (=)/([]<>-+) 找尋首篇文章/主題式閱\讀",
#ifdef INTERNET_EMAIL
    "(F)       文章寄回Internet郵箱 (U)      將文章 uuencode 後寄回郵箱",
#endif
    "(E)       重編文章             (^H)     列出所有的 New Post(s)",
    "\01板主命令",
    "(M/o)     舉行投票/編私投票名單 (m/c/g) 保留文章/選錄精華/文摘",
    "(D)       刪除一段範圍的文章    (T/B)   重編文章標題/重編看板標題",
    "(I)       開放/禁止看版推薦     (t/^D)  標記文章/砍除標記的文章",
    "(O)       編輯Post注意事項      (H)/(Y) 看板隱藏/現身 取消推薦文章",
#ifdef NO_GAMBLE
    "(W/K/v)   編進板畫面/水桶名單/可看見名單",
#else
    "(^G)      舉辦賭盤/停止下注/開獎(W/K/v) 編進板畫面/水桶名單/可看見名單",
#endif
    NULL
};

static int
b_help()
{
    show_help(board_help);
    return FULLUPDATE;
}

static int
b_changerecommend(int ent, fileheader_t * fhdr, char *direct)
{
    boardheader_t   bh;
    int             bid;
    if (!((currmode & MODE_BOARD) || HAS_PERM(PERM_SYSOP)) ||
	currboard[0] == 0 ||
	(bid = getbnum(currboard)) < 0 ||
	get_record(fn_board, &bh, sizeof(bh), bid) == -1)
	return DONOTHING;
    if( bh.brdattr & BRD_NORECOMMEND )
	bh.brdattr -= BRD_NORECOMMEND;
    else
	bh.brdattr += BRD_NORECOMMEND;
    setup_man(&bh);
    substitute_record(fn_board, &bh, sizeof(bh), bid);
    reset_board(bid);
    vmsg("本板現在 %s 推薦",
	 (bh.brdattr & BRD_NORECOMMEND) ? "禁止" : "開放");
    return FULLUPDATE;
}

/* ----------------------------------------------------- */
/* 板主設定隱形/ 解隱形                                  */
/* ----------------------------------------------------- */
char            board_hidden_status;
#ifdef  BMCHS
static int
change_hidden(int ent, fileheader_t * fhdr, char *direct)
{
    boardheader_t   bh;
    int             bid;
    char            ans[4];

    if (!((currmode & MODE_BOARD) || HAS_PERM(PERM_SYSOP)) ||
	currboard[0] == 0 ||
	(bid = getbnum(currboard)) < 0 ||
	get_record(fn_board, &bh, sizeof(bh), bid) == -1)
	return DONOTHING;

    if (((bh.brdattr & BRD_HIDE) && (bh.brdattr & BRD_POSTMASK))) {
	getdata(1, 0, "目前板在隱形狀態, 要解隱形嘛(Y/N)?[N]",
		ans, sizeof(ans), LCECHO);
	if (ans[0] != 'y' && ans[0] != 'Y')
	    return FULLUPDATE;
	getdata(2, 0, "再確認一次, 真的要把板板公開嘛 @____@(Y/N)?[N]",
		ans, sizeof(ans), LCECHO);
	if (ans[0] != 'y' && ans[0] != 'Y')
	    return FULLUPDATE;
	if (bh.brdattr & BRD_HIDE)
	    bh.brdattr -= BRD_HIDE;
	if (bh.brdattr & BRD_POSTMASK)
	    bh.brdattr -= BRD_POSTMASK;
	log_usies("OpenBoard", bh.brdname);
	outs("君心今傳眾人，無處不聞弦歌。\n");
	board_hidden_status = 0;
	hbflreload(bid);
    } else {
	getdata(1, 0, "目前板在現形狀態, 要隱形嘛(Y/N)?[N]",
		ans, sizeof(ans), LCECHO);
	if (ans[0] != 'y' && ans[0] != 'Y')
	    return FULLUPDATE;
	bh.brdattr |= BRD_HIDE;
	bh.brdattr |= BRD_POSTMASK;
	log_usies("CloseBoard", bh.brdname);
	outs("君心今已掩抑，惟盼善自珍重。\n");
	board_hidden_status = 1;
    }
    setup_man(&bh);
    substitute_record(fn_board, &bh, sizeof(bh), bid);
    reset_board(bid);
    log_usies("SetBoard", bh.brdname);
    pressanykey();
    return FULLUPDATE;
}
#endif

/* ----------------------------------------------------- */
/* 看板功能表                                            */
/* ----------------------------------------------------- */
struct onekey_t read_comms[] = {
    {KEY_TAB, board_digest},
    {'B', bh_title_edit},
    {'b', b_notes},
    {'C', board_etc},
    {'c', cite_post},
    {'D', del_range},
    {'d', del_post},
    {'E', edit_post},
#ifndef NO_GAMBLE
    {'f', join_gamble},
    {Ctrl('G'), hold_gamble},
#endif
    {'g', good_post},
#ifdef BMCHS
    {'H', change_hidden},
#endif
    {'h', b_help},
    {'I', b_changerecommend},
    {'i', b_application},
    {'K', b_water_edit},
    {'L', solve_post},
    {'M', b_vote_maintain},
    {'m', mark_post},
    {'O', b_post_note},
    {'o', can_vote_edit},
    {'Q', view_postmoney},
    {'R', b_results},
    {'r', read_post},
    {'S', sequential_read},
    {'s', do_select},
    {'T', edit_title},
#ifdef OUTJOBSPOOL
    {'u', tar_addqueue},
#endif
    {'V', b_vote},
    {'v', visable_list_edit},
    {'W', b_notes_edit},
    {'w', b_call_in},
    {'X', recommend},
    {'x', cross_post},
    {'Y', recommend_cancel},
    {'y', reply_post},
    {'z', b_man},
    {Ctrl('P'), do_post},
    {Ctrl('W'), whereami},
    {'\0', NULL}
};

int
Read()
{
    int             mode0 = currutmp->mode;
    int             stat0 = currstat, tmpbid = currutmp->brc_id;
    char            buf[40];
#ifdef LOG_BOARD
    time_t          usetime = now;
#endif

    setutmpmode(READING);
    set_board();

    if (board_visit_time < board_note_time) {
	setbfile(buf, currboard, fn_notes);
	more(buf, NA);
	pressanykey();
    }
    setutmpbid(currbid);
    setbdir(buf, currboard);
    curredit &= ~EDIT_MAIL;
    i_read(READING, buf, readtitle, readdoent, read_comms,
	   currbid);
#ifdef LOG_BOARD
    log_board(currboard, now - usetime);
#endif
    brc_update();
    setutmpbid(tmpbid);
    currutmp->mode = mode0;
    currstat = stat0;
    return 0;
}

void
ReadSelect()
{
    int             mode0 = currutmp->mode;
    int             stat0 = currstat;
    char            genbuf[200];

    currstat = SELECT;
    if (do_select(0, 0, genbuf) == NEWDIRECT)
	Read();
    setutmpbid(0);
    currutmp->mode = mode0;
    currstat = stat0;
}

#ifdef LOG_BOARD
static void
log_board(char *mode, time_t usetime)
{
    char            buf[256];

    if (usetime > 30) {
	snprintf(buf, sizeof(buf), "USE %-20.20s Stay: %5ld (%s) %s",
		 mode, usetime, cuser.userid, ctime(&now));
	log_file(FN_USEBOARD, buf);
    }
}
#endif

int
Select()
{
    char            genbuf[200];

    do_select(0, NULL, genbuf);
    return 0;
}
#ifdef HAVEMOBILE
void
mobile_message(char *mobile, char *message)
{



    bsmtp(char *fpath, char *title, char *rcpt, int method);
}

#endif
