/* $Id: bbs.c,v 1.63 2002/07/05 17:10:26 in2 Exp $ */
#include "bbs.h"

static void 
mail_by_link(char *owner, char *title, char *path)
{
    char            genbuf[200];
    fileheader_t    mymail;

    sprintf(genbuf, BBSHOME "/home/%c/%s", cuser.userid[0], cuser.userid);
    stampfile(genbuf, &mymail);
    strcpy(mymail.owner, owner);
    sprintf(mymail.title, title);
    unlink(genbuf);
    Link(path, genbuf);
    sprintf(genbuf, BBSHOME "/home/%c/%s/.DIR", cuser.userid[0], cuser.userid);

    append_record(genbuf, &mymail, sizeof(mymail));
}


void 
anticrosspost()
{
    char            buf[200];

    sprintf(buf,
	    "\033[1;33;46m%s \033[37;45mcross post ¤å³¹ \033[37m %s\033[m",
	    cuser.userid, ctime(&now));
    log_file("etc/illegal_money", buf);

    post_violatelaw(cuser.userid, "Ptt¨t²ÎÄµ¹î", "Cross-post", "»@³æ³B¥÷");
    cuser.userlevel |= PERM_VIOLATELAW;
    cuser.vl_count++;
    mail_by_link("PttÄµ¹î³¡¶¤", "Cross-Post»@³æ",
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
    stand_title("Ãº»@³æ¤¤¤ß");

    if (!(cuser.userlevel & PERM_VIOLATELAW)) {
	mprints(22, 0, "\033[1;31m§AµL²á°Ú? §A¤S¨S¦³³Q¶}»@³æ~~\033[m");
	pressanykey();
	return 0;
    }
    reload_money();
    if (cuser.money < (int)cuser.vl_count * 1000) {
	sprintf(buf, "\033[1;31m³o¬O§A²Ä %d ¦¸¹H¤Ï¥»¯¸ªk³W"
		"¥²¶·Ãº¥X %d $Ptt ,§A¥u¦³ %d ¤¸, ¿ú¤£°÷°Õ!!\033[m",
	      (int)cuser.vl_count, (int)cuser.vl_count * 1000, cuser.money);
	mprints(22, 0, buf);
	pressanykey();
	return 0;
    }
    move(5, 0);
    prints("\033[1;37m§Aª¾¹D¶Ü? ¦]¬°§Aªº¹Hªk "
	   "¤w¸g³y¦¨«Ü¦h¤Hªº¤£«K\033[m\n");
    prints("\033[1;37m§A¬O§_½T©w¥H«á¤£·|¦A¥Ç¤F¡H\033[m\n");

    if (!getdata(10, 0, "½T©w¶Ü¡H[y/n]:", ok, sizeof(ok), LCECHO) ||
	ok[0] == 'n' || ok[0] == 'N') {
	mprints(22, 0, "\033[1;31mµ¥§A·Q³q¤F¦A¨Ó§a!! "
		"§Ú¬Û«H§A¤£·|ª¾¿ù¤£§ïªº~~~\033[m");
	pressanykey();
	return 0;
    }
    sprintf(buf, "³o¬O§A²Ä %d ¦¸¹Hªk ¥²¶·Ãº¥X %d $Ptt",
	    cuser.vl_count, cuser.vl_count * 1000);
    mprints(11, 0, buf);

    if (!getdata(10, 0, "­n¥I¿ú[y/n]:", ok, sizeof(ok), LCECHO) ||
	ok[0] == 'N' || ok[0] == 'n') {

	mprints(22, 0, "\033[1;31m ¶â ¦s°÷¿ú ¦A¨Ó§a!!!\033[m");
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
    board_note_time = bp->bupdate;
    brd_title = bp->BM;
    if (brd_title[0] <= ' ')
	brd_title = "¼x¨D¤¤";
    sprintf(currBM, "ªO¥D¡G%s", brd_title);
    brd_title = ((bp->bvote != 2 && bp->bvote) ? "¥»¬ÝªO¶i¦æ§ë²¼¤¤" :
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
    outs("[¡ö]Â÷¶} [¡÷]¾\\Åª [^P]µoªí¤å³¹ [b]³Æ§Ñ¿ý [d]§R°£ [z]ºëµØ°Ï "
      "[TAB]¤åºK [h]elp\n\033[7m  ½s¸¹   ¤é ´Á  §@  ªÌ       ¤å  ³¹  ¼Ð  ÃD"
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
//Ptt:¼È®É
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
	color = '1', mark = "¡¼";
    else
	color = '3', mark = "R:";

    if (title[47])
	strcpy(title + 44, " ¡K");	/* §â¦h¾lªº string ¬å±¼ */

    if (!strncmp(title, "[¤½§i]", 6))
	special = 1;
    if (!strchr(ent->owner, '.') && (uid = searchuser(ent->owner)) &&
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
    prints("§Ú¦b­þ?\n%-40.40s %.13s\n", p[j]->title + 7, p[j]->BM);
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

    /* rocker.011018: ¦ê±µ¼Ò¦¡¥Îreference¼W¶i®Ä²v */
    if ((currmode & MODE_SELECT) && (fhdr->money & FHR_REFERENCE)) {
	num = fhdr->money & ~FHR_REFERENCE;
	setbdir(genbuf, currboard);
	get_record(genbuf, &hdr, sizeof(hdr), num);

	/* ¦A³o¸Ì­ncheck¤@¤U­ì¨Óªºdir¸Ì­±¬O¤£¬O¦³³Q¤H°Ê¹L... */
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
    strcpy(bname, bh->brdname);
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
/* §ï¨} innbbsd Âà¥X«H¥ó¡B³s½u¬å«H¤§³B²zµ{§Ç             */
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
		    strcpy(nick, ptr + 1);
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
/* µoªí¡B¦^À³¡B½s¿è¡BÂà¿ý¤å³¹                            */
/* ----------------------------------------------------- */
void 
do_reply_title(int row, char *title)
{
    char            genbuf[200];
    char            genbuf2[4];

    if (strncasecmp(title, str_reply, 4))
	sprintf(save_title, "Re: %s", title);
    else
	strcpy(save_title, title);
    save_title[TTLEN - 1] = '\0';
    sprintf(genbuf, "±Ä¥Î­ì¼ÐÃD¡m%.60s¡n¶Ü?[Y] ", save_title);
    getdata(row, 0, genbuf, genbuf2, 4, LCECHO);
    if (genbuf2[0] == 'n' || genbuf2[0] == 'N')
	getdata(++row, 0, "¼ÐÃD¡G", save_title, TTLEN, DOECHO);
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
	strcpy(mhdr.owner, cuser.userid);
	strcpy(mhdr.title, save_title);
	mhdr.filemode = 0;
	setbdir(title, "UnAnonymous");
	append_record(title, &mhdr, sizeof(mhdr));
    }
}

#ifdef NO_WATER_POST
static time_t   last_post_time = 0;
static time_t   water_counts = 0;
#endif

static int 
do_general()
{
    fileheader_t    postfile;
    char            fpath[80], buf[80];
    int             aborted, defanony, ifuseanony;
    char            genbuf[200], *owner, *ctype[] = {"°ÝÃD", "«ØÄ³", "°Q½×", "¤ß±o", "¶¢²á", "¤½§i", "±¡³ø"};
    boardheader_t  *bp;
    int             islocal;

    ifuseanony = 0;
    bp = getbcache(currbid);

    clear();
    if (!(currmode & MODE_POST)) {
	move(5, 10);
	outs("¹ï¤£°_¡A±z¥Ø«eµLªk¦b¦¹µoªí¤å³¹¡I");
	pressanykey();
	return FULLUPDATE;
    }
#ifdef NO_WATER_POST
    /* ¤T¤ÀÄÁ¤º³Ì¦hµoªí¤­½g¤å³¹ */
    if (currutmp->lastact - last_post_time < 60 * 3) {
	if (water_counts >= 5) {
	    move(5, 10);
	    outs("¹ï¤£°_¡A±zªº¤å³¹¤Ó¤ôÅo¡A«Ý·|¦Apost§a¡I¤p¯µ³Z:¥i¥Î'X'±ÀÂË¤å³¹");
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
    prints("µoªí¤å³¹©ó¡i\033[33m %s\033[m ¡j \033[32m%s\033[m ¬ÝªO\n\n",
	   currboard, bp->title + 7);

    if (quote_file[0])
	do_reply_title(20, currtitle);
    else {
	getdata(21, 0,
	"ºØÃþ¡G1.°ÝÃD 2.«ØÄ³ 3.°Q½× 4.¤ß±o 5.¶¢²á 6.¤½§i 7.±¡³ø (1-7©Î¤£¿ï)",
		save_title, 3, LCECHO);
	local_article = save_title[0] - '1';
	if (local_article >= 0 && local_article <= 6)
	    sprintf(save_title, "[%s] ", ctype[local_article]);
	else
	    save_title[0] = '\0';
	getdata_buf(22, 0, "¼ÐÃD¡G", save_title, TTLEN, DOECHO);
	strip_ansi(save_title, save_title, 0);
    }
    if (save_title[0] == '\0')
	return FULLUPDATE;

    curredit &= ~EDIT_MAIL;
    curredit &= ~EDIT_ITEM;
    setutmpmode(POSTING);

    /* ¥¼¨ã³Æ Internet Åv­­ªÌ¡A¥u¯à¦b¯¸¤ºµoªí¤å³¹ */
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
    water_counts++;		/* po¦¨¥\ */

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
    /* ¿ú */
    aborted = (aborted > MAX_POST_MONEY * 2) ? MAX_POST_MONEY : aborted / 2;
    postfile.money = aborted;
    strcpy(postfile.owner, owner);
    strcpy(postfile.title, save_title);
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
	    setbpath(genbuf, ALLPOST);
	    stampfile(genbuf, &postfile);
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
	    strcpy(postfile.owner, owner);
	    strcpy(postfile.title, save_title);
	    postfile.filemode = FILE_LOCAL;
	    setbdir(genbuf, ALLPOST);
	    if (append_record(genbuf, &postfile, sizeof(postfile)) != -1) {
		setbtotal(getbnum(ALLPOST));
	    }
	}
	outs("¶¶§Q¶K¥X§G§i¡A");

#ifdef MAX_POST_MONEY
	aborted = (aborted > MAX_POST_MONEY) ? MAX_POST_MONEY : aborted;
#endif
	if (strcmp(currboard, "Test") && !ifuseanony) {
	    prints("³o¬O±zªº²Ä %d ½g¤å³¹¡C ½Z¹S %d »È¡C",
		   ++cuser.numposts, aborted);
	    demoney(aborted);
	    passwd_update(usernum, &cuser);	/* post ¼Æ */
	} else
	    outs("´ú¸Õ«H¥ó¤£¦C¤J¬ö¿ý¡A·q½Ð¥]²[¡C");

	/* ¦^À³¨ì­ì§@ªÌ«H½c */

	if (curredit & EDIT_BOTH) {
	    char           *str, *msg = "¦^À³¦Ü§@ªÌ«H½c";

	    if ((str = strchr(quote_user, '.'))) {
		if (
#ifndef USE_BSMTP
		    bbs_sendmail(fpath, save_title, str + 1)
#else
		    bsmtp(fpath, save_title, str + 1, 0)
#endif
		    < 0)
		    msg = "§@ªÌµLªk¦¬«H";
	    } else {
		sethomepath(genbuf, quote_user);
		stampfile(genbuf, &postfile);
		unlink(genbuf);
		Link(fpath, genbuf);

		strcpy(postfile.owner, cuser.userid);
		strcpy(postfile.title, save_title);
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
	    "¡¶ ¦^À³¦Ü (F)¬ÝªO (M)§@ªÌ«H½c (B)¤GªÌ¬Ò¬O (Q)¨ú®ø¡H[F] ",
	    genbuf, 3, LCECHO);
    switch (genbuf[0]) {
    case 'm':
	mail_reply(0, fhdr, 0);
    case 'q':
	break;

    case 'b':
	curredit = EDIT_BOTH;
    default:
	strcpy(currtitle, fhdr->title);
	strcpy(quote_user, fhdr->owner);
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
    setutmpmode(REEDIT);
    setdirpath(genbuf, direct, fhdr->filename);
    local_article = fhdr->filemode & FILE_LOCAL;
    strcpy(save_title, fhdr->title);

    /* rocker.011018: ³o¸Ì¬O¤£¬O¸ÓÀË¬d¤@¤U­×§ï¤å³¹«áªºmoney©M­ì¦³ªº¤ñ¸û? */
    if (vedit(genbuf, 0, NULL) != -1) {
	setbpath(fpath, currboard);
	stampfile(fpath, &postfile);
	unlink(fpath);
	setbfile(fpath0, currboard, fhdr->filename);

	Rename(fpath0, fpath);

	/* rocker.011018: fix ¦ê±µ¼Ò¦¡§ï¤å³¹«á¤å³¹´N¤£¨£ªºbug */
	if ((currmode & MODE_SELECT) && (fhdr->money & FHR_REFERENCE)) {
	    fileheader_t    hdr;
	    int             num;

	    num = fhdr->money & ~FHR_REFERENCE;
	    setbdir(fpath0, currboard);
	    get_record(fpath0, &hdr, sizeof(hdr), num);

	    /* ¦A³o¸Ì­ncheck¤@¤U­ì¨Óªºdir¸Ì­±¬O¤£¬O¦³³Q¤H°Ê¹L... */
	    if (!strcmp(hdr.filename, fhdr->filename)) {
		strcpy(hdr.filename, postfile.filename);
		strcpy(hdr.title, save_title);
		substitute_record(fpath0, &hdr, sizeof(hdr), num);
	    }
	}
	strcpy(fhdr->filename, postfile.filename);
	strcpy(fhdr->title, save_title);
	brc_addlist(postfile.filename);
	substitute_record(direct, fhdr, sizeof(*fhdr), ent);
	/* rocker.011018: ¶¶«K§ó·s¤@¤Ucache */
	touchdircache(currbid);
    }
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
    generalnamecomplete("Âà¿ý¥»¤å³¹©ó¬ÝªO¡G", xboard, sizeof(xboard),
			SHM->Bnumber,
			completeboard_compar,
			completeboard_permission,
			completeboard_getname);
    if (*xboard == '\0' || !haspostperm(xboard))
	return FULLUPDATE;

    if ((ent = str_checksum(fhdr->title)) != 0 &&
	ent == postrecord.checksum[0]) {
	/* ÀË¬d cross post ¦¸¼Æ */
	if (postrecord.times++ > MAX_CROSSNUM)
	    anticrosspost();
    } else {
	postrecord.times = 0;
	postrecord.checksum[0] = ent;
    }

    ent = 1;
    if (HAS_PERM(PERM_SYSOP) || !strcmp(fhdr->owner, cuser.userid)) {
	getdata(2, 0, "(1)­ì¤åÂà¸ü (2)ÂÂÂà¿ý®æ¦¡¡H[1] ",
		genbuf, 3, DOECHO);
	if (genbuf[0] != '2') {
	    ent = 0;
	    getdata(2, 0, "«O¯d­ì§@ªÌ¦WºÙ¶Ü?[Y] ", inputbuf, 3, DOECHO);
	    if (inputbuf[0] != 'n' && inputbuf[0] != 'N')
		author = 1;
	}
    }
    if (ent)
	sprintf(xtitle, "[Âà¿ý]%.66s", fhdr->title);
    else
	strcpy(xtitle, fhdr->title);

    sprintf(genbuf, "±Ä¥Î­ì¼ÐÃD¡m%.60s¡n¶Ü?[Y] ", xtitle);
    getdata(2, 0, genbuf, genbuf2, 4, LCECHO);
    if (genbuf2[0] == 'n' || genbuf2[0] == 'N') {
	if (getdata_str(2, 0, "¼ÐÃD¡G", genbuf, TTLEN, DOECHO, xtitle))
	    strcpy(xtitle, genbuf);
    }
    getdata(2, 0, "(S)¦sÀÉ (L)¯¸¤º (Q)¨ú®ø¡H[Q] ", genbuf, 3, LCECHO);
    if (genbuf[0] == 'l' || genbuf[0] == 's') {
	int             currmode0 = currmode;

	currmode = 0;
	setbpath(xfpath, xboard);
	stampfile(xfpath, &xfile);
	if (author)
	    strcpy(xfile.owner, fhdr->owner);
	else
	    strcpy(xfile.owner, cuser.userid);
	strcpy(xfile.title, xtitle);
	if (genbuf[0] == 'l') {
	    xfile.filemode = FILE_LOCAL;
	}
	setbfile(fname, currboard, fhdr->filename);
	//if (ent) {
	    xptr = fopen(xfpath, "w");

	    strcpy(save_title, xfile.title);
	    strcpy(xfpath, currboard);
	    strcpy(currboard, xboard);
	    write_header(xptr);
	    strcpy(currboard, xfpath);

	    fprintf(xptr, "¡° [¥»¤åÂà¿ý¦Û %s ¬ÝªO]\n\n", currboard);

	    b_suckinfile(xptr, fname);
	    addsignature(xptr, 0);
	    fclose(xptr);
	    /*
	     * Cross fs¦³°ÝÃD } else { unlink(xfpath); link(fname, xfpath); }
	     */
	    setbdir(fname, xboard);
	    append_record(fname, &xfile, sizeof(xfile));
	    bp = getbcache(getbnum(xboard));
	    if (!xfile.filemode && !(bp->brdattr && BRD_NOTRAN))
		outgo_post(&xfile, xboard);
	    setbtotal(getbnum(xboard));
	    cuser.numposts++;
	    UPDATE_USEREC;
	    outs("¤å³¹Âà¿ý§¹¦¨");
	    pressanykey();
	    currmode = currmode0;
	}
	return FULLUPDATE;
    }
    static int      read_post(int ent, fileheader_t * fhdr, char *direct){
	char            genbuf[200];
	int             more_result;

	if              (fhdr->owner[0] == '-')
	                    return DONOTHING;

	                setdirpath(genbuf, direct, fhdr->filename);

	if              ((more_result = more(genbuf, YEA)) == -1)
	                    return DONOTHING;

	                brc_addlist(fhdr->filename);
	                strncpy(currtitle, subject(fhdr->title), TTLEN);
	                strncpy(currowner, subject(fhdr->owner), IDLEN + 2);

	switch          (more_result) {
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
		strcpy(quote_file, genbuf);
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


	outmsg("\033[34;46m  ¾\\Åª¤å³¹  \033[31;47m  (R/Y)\033[30m¦^«H \033[31m"
	 "(=[]<>)\033[30m¬ÛÃö¥DÃD \033[31m(¡ô¡õ)\033[30m¤W¤U«Ê \033[31m(¡ö)"
	       "\033[30mÂ÷¶}  \033[m");

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
	    break;

	case Ctrl('I'):
	    t_idle();
	    return FULLUPDATE;
	case 'y':
	case 'r':
	case 'R':
	case 'Y':
	    if ((currmode & MODE_POST)) {
		strcpy(quote_file, genbuf);
		do_reply(fhdr);
		*quote_file = 0;
	    }
	}
	return FULLUPDATE;
    }

    /* ----------------------------------------------------- */
    /* ±Ä¶°ºëµØ°Ï                                            */
    /* ----------------------------------------------------- */
    static int      b_man() {
	char            buf[64];

	                setapath(buf, currboard);
	if              ((currmode & MODE_BOARD) || HAS_PERM(PERM_SYSOP)) {
	    char            genbuf[128];
	    int             fd;
	                    sprintf(genbuf, "%s/.rebuild", buf);
	    if              ((fd = open(genbuf, O_CREAT, 0640)) > 0)
		                close(fd);
	}
	                return a_menu(currboard, buf, HAS_PERM(PERM_ALLBOARD) ? 2 :
			                   (currmode & MODE_BOARD ? 1 : 0));
    }

#ifndef NO_GAMBLE
    void            stop_gamble() {
	boardheader_t  *bp = getbcache(currbid);
	char            fn_ticket[128], fn_ticket_end[128];
	if              (!bp->endgamble || bp->endgamble > now)
	                    return;

	                setbfile(fn_ticket, currboard, FN_TICKET);
	                setbfile(fn_ticket_end, currboard, FN_TICKET_END);

	                rename(fn_ticket, fn_ticket_end);
	if              (bp->endgamble) {
	    bp->endgamble = 0;
	    substitute_record(fn_board, bp, sizeof(boardheader_t), currbid);
	}
    }
    static int      join_gamble(int ent, fileheader_t * fhdr, char *direct){
	if (!HAS_PERM(PERM_LOGINOK))
	    return DONOTHING;
	stop_gamble();
	ticket(currbid);
	return FULLUPDATE;
    }
    static int      hold_gamble(int ent, fileheader_t * fhdr, char *direct){
	char            fn_ticket[128], fn_ticket_end[128], genbuf[128], msg[256] = "",
	                yn[10] = "";
	boardheader_t  *bp = getbcache(currbid);
	int             i;
	FILE           *fp = NULL;

	if              (!(currmode & MODE_BOARD))
	                    return 0;
	                setbfile(fn_ticket, currboard, FN_TICKET);
	                setbfile(fn_ticket_end, currboard, FN_TICKET_END);
	                setbfile(genbuf, currboard, FN_TICKET_LOCK);
	if              (dashf(fn_ticket)) {
	    getdata(b_lines - 1, 0, "¤w¸g¦³Á|¿ì½ä½L, "
		    "¬O§_­n [°±¤î¤Uª`]?(N/y)¡G", yn, 3, LCECHO);
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
	    getdata(b_lines - 1, 0, "¤w¸g¦³Á|¿ì½ä½L, "
		    "¬O§_­n [¶}¼ú/¨ú®ø]?(N/y)¡G", yn, 3, LCECHO);
	    if (yn[0] != 'y')
		return FULLUPDATE;
	    openticket(currbid);
	    return FULLUPDATE;
	} else if (dashf(genbuf)) {
	    move(b_lines - 1, 0);
	    prints(" ¥Ø«e¨t²Î¥¿¦b³B²z¶}¼ú¨Æ©y, ½Ðµ²ªG¥XÄl«á¦AÁ|¿ì.......");
	    pressanykey();
	    return FULLUPDATE;
	}
	getdata(b_lines - 2, 0, "­nÁ|¿ì½ä½L (N/y):", yn, 3, LCECHO);
	if (yn[0] != 'y')
	    return FULLUPDATE;
	getdata(b_lines - 1, 0, "½ä¤°»ò? ½Ð¿é¤J¥DÃD (¿é¤J«á½s¿è¤º®e):",
		msg, 20, DOECHO);
	if (msg[0] == 0 ||
	    vedit(fn_ticket_end, NA, NULL) < 0)
	    return FULLUPDATE;

	clear();
	showtitle("Á|¿ì½ä½L", BBSNAME);
	setbfile(genbuf, currboard, FN_TICKET_ITEMS);

	//sprintf(genbuf, "%s/" FN_TICKET_ITEMS, direct);

	if (!(fp = fopen(genbuf, "w")))
	    return FULLUPDATE;
	do {
	    getdata(2, 0, "¿é¤J±m²¼»ù®æ (»ù®æ:10-10000):", yn, 6, LCECHO);
	    i = atoi(yn);
	} while (i < 10 || i > 10000);
	fprintf(fp, "%d\n", i);
	if (!getdata(3, 0, "³]©w¦Û°Ê«Ê½L®É¶¡?(Y/n)", yn, 3, LCECHO) || yn[0] != 'n') {
	    bp->endgamble = gettime(4, now);
	    substitute_record(fn_board, bp, sizeof(boardheader_t), currbid);
	}
	move(6, 0);
	sprintf(genbuf, "½Ð¨ì %s ªO «ö'f'°Ñ»P½ä³Õ!\n\n¤@±i %d Ptt¹ô, ³o¬O%sªº½ä³Õ\n%s%s",
		currboard,
		i, i < 100 ? "¤p½ä¦¡" : i < 500 ? "¥­¥Á¯Å" :
		i < 1000 ? "¶Q±Ú¯Å" : i < 5000 ? "´I»¨¯Å" : "¶É®a¿º²£",
		bp->endgamble ? "½ä½Lµ²§ô®É¶¡: " : "",
		bp->endgamble ? Cdate(&bp->endgamble) : ""
	    );
	strcat(msg, genbuf);
	prints("½Ð¨Ì¦¸¿é¤J±m²¼¦WºÙ, »Ý´£¨Ñ2~8¶µ. (¥¼º¡¤K¶µ, ¿é¤Jª½±µ«öenter)\n");
	for (i = 0; i < 8; i++) {
	    sprintf(yn, " %d)", i + 1);
	    getdata(7 + i, 0, yn, genbuf, 9, DOECHO);
	    if (!genbuf[0] && i > 1)
		break;
	    fprintf(fp, "%s\n", genbuf);
	}
	fclose(fp);
	move(8 + i, 0);
	prints("½ä½L³]©w§¹¦¨");
	sprintf(genbuf, "[¤½§i] %s ªO ¶}©l½ä³Õ!", currboard);
	post_msg(currboard, genbuf, msg, cuser.userid);
	post_msg("Record", genbuf + 7, msg, "[°¨¸ô±´¤l]");
	/* Tim ±±¨îCS, ¥H§K¥¿¦bª±ªºuser§â¸ê®Æ¤w¸g¼g¶i¨Ó */
	rename(fn_ticket_end, fn_ticket);
	//³] © w § ¹¤~§âÀÉ¦W § ï¹L ¨ Ó

	    return FULLUPDATE;
    }
#endif

    static int      cite_post(int ent, fileheader_t * fhdr, char *direct){
	char            fpath[256];
	char            title[TTLEN + 1];

	                setbfile(fpath, currboard, fhdr->filename);
	                strcpy(title, "¡º ");
	                strncpy(title + 3, fhdr->title, TTLEN - 3);
	                title[TTLEN] = '\0';
	                a_copyitem(fpath, title, 0, 1);
	                b_man();
	                return FULLUPDATE;
    }

    int             edit_title(int ent, fileheader_t * fhdr, char *direct){
	char            genbuf[200];
	fileheader_t    tmpfhdr = *fhdr;
	int             dirty = 0;

	if              (currmode & MODE_BOARD || !strcmp(cuser.userid, fhdr->owner)) {
	    if (getdata(b_lines - 1, 0, "¼ÐÃD¡G", genbuf, TTLEN, DOECHO)) {
		strcpy(tmpfhdr.title, genbuf);
		dirty++;
	    }
	}
	if              (HAS_PERM(PERM_SYSOP)) {
	    if (getdata(b_lines - 1, 0, "§@ªÌ¡G", genbuf, IDLEN + 2, DOECHO)) {
		strcpy(tmpfhdr.owner, genbuf);
		dirty++;
	    }
	    if (getdata(b_lines - 1, 0, "¤é´Á¡G", genbuf, 6, DOECHO)) {
		sprintf(tmpfhdr.date, "%.5s", genbuf);
		dirty++;
	    }
	}
	if (currmode & MODE_BOARD || !strcmp(cuser.userid, fhdr->owner)) {
	    getdata(b_lines - 1, 0, "½T©w(Y/N)?[n] ", genbuf, 3, DOECHO);
	    if ((genbuf[0] == 'y' || genbuf[0] == 'Y') && dirty) {
		*fhdr = tmpfhdr;
		substitute_record(direct, fhdr, sizeof(*fhdr), ent);
		/* rocker.011018: ³o¸ÌÀ³¸Ó§ï¦¨¥Îreferenceªº¤è¦¡¨ú±o­ì¨ÓªºÀÉ®× */
		substitute_check(fhdr);
		touchdircache(currbid);
	    }
	    return FULLUPDATE;
	}
	return DONOTHING;
    }

    static int      solve_post(int ent, fileheader_t * fhdr, char *direct){
	if (HAS_PERM(PERM_SYSOP)) {
	    fhdr->filemode ^= FILE_SOLVED;
	    substitute_record(direct, fhdr, sizeof(*fhdr), ent);
	    touchdircache(currbid);
	    return PART_REDRAW;
	}
	return DONOTHING;
    }

    static int      recommend_cancel(int ent, fileheader_t * fhdr, char *direct){
	char            yn[5];
	if              (!(currmode & MODE_BOARD))
	                    return DONOTHING;
	                getdata(b_lines - 1, 0, "½T©w­n±ÀÂËÂk¹s(Y/N)?[n] ", yn, 5, LCECHO);
	if              (yn[0] != 'y')
	                    return FULLUPDATE;
	                fhdr->recommend = 0;

	                substitute_record(direct, fhdr, sizeof(*fhdr), ent);
	                substitute_check(fhdr);
	                touchdircache(currbid);
	                return FULLUPDATE;
    }
    static int      recommend(int ent, fileheader_t * fhdr, char *direct){
	struct tm      *ptime = localtime(&now);
	char            buf[200], path[200], yn[5];
	boardheader_t  *bp;
	                bp = getbcache(currbid);

	if              (!(currmode & MODE_POST) || !strcmp(fhdr->owner, cuser.userid) ||
			                 bp->brdattr & BRD_VOTEBOARD) {
	    move(b_lines - 1, 0);
	    prints("±z¦]Åv­­¤£¨¬µLªk±ÀÂË ©Î ¤£¯à±ÀÂË¦Û¤vªº¤å³¹!");
	    pressanykey();
	    return FULLUPDATE;
	}
	if              (fhdr->recommend > 9 || fhdr->recommend < 0)
	                  //¼È®É©Êªºcode ­ ì¨ÓÂÂ¦³­È¨ú®ø
	                    fhdr->recommend = 0;

	if (!(currmode & MODE_BOARD) && getuser(cuser.userid) &&
	    now - xuser.recommend < 60) {
	    move(b_lines - 1, 0);
	    prints("Â÷¤W¦¸±ÀÂË®É¶¡¤ÓªñÅo, ½Ð¦hªáÂI®É¶¡¥J²Ó¾\\Åª¤å³¹!");
	    pressanykey();
	    return FULLUPDATE;
	}
	if (!getdata(b_lines - 2, 0, "±ÀÂË»y:", path, 40, DOECHO) ||
	    !getdata(b_lines - 1, 0, "½T©w­n±ÀÂË, ½Ð¥J²Ó¦Ò¼{(Y/N)?[n] ", yn, 5, LCECHO)
	    || yn[0] != 'y')
	    return FULLUPDATE;

	sprintf(buf,
		"\033[1;31m¡÷ \033[33m%s\033[m\033[33m:%s\033[m%*s±ÀÂË¦Û:%s %02d/%02d\n",
		cuser.userid, path,
		45 - strlen(cuser.userid) - strlen(path), " ", fromhost,
		ptime->tm_mon + 1, ptime->tm_mday);
	setdirpath(path, direct, fhdr->filename);
	log_file(path, buf);
	if (fhdr->recommend < 9) {
	    fhdr->recommend++;
	    cuser.recommend = now;
	    passwd_update(usernum, &cuser);
	    substitute_record(direct, fhdr, sizeof(*fhdr), ent);
	    substitute_check(fhdr);
	    touchdircache(currbid);
	}
	return FULLUPDATE;
    }
    static int      mark_post(int ent, fileheader_t * fhdr, char *direct){

	if (!(currmode & MODE_BOARD))
	    return DONOTHING;

	fhdr->filemode ^= FILE_MARKED;
	substitute_record(direct, fhdr, sizeof(*fhdr), ent);
	substitute_check(fhdr);
	touchdircache(currbid);
	return PART_REDRAW;
    }

    int             del_range(int ent, fileheader_t * fhdr, char *direct){
	char            num1[8], num2[8];
	int             inum1, inum2;
	boardheader_t  *bp;
	                bp = getbcache(currbid);

	/* rocker.011018: ¦ê±µ¼Ò¦¡¤UÁÙ¬O¤£¤¹³\§R°£¤ñ¸û¦n */
	if              (currmode & MODE_SELECT) {
	    outmsg("½Ð¥ý¦^¨ì¥¿±`¼Ò¦¡«á¦A¶i¦æ§R°£...");
	    refresh();
	    /* safe_sleep(1); */
	    return FULLUPDATE;
	}
	if              (strcmp(bp->brdname, "Security") == 0)
	                    return DONOTHING;
	if ((currstat != READING) || (currmode & MODE_BOARD)) {
	    getdata(1, 0, "[³]©w§R°£½d³ò] °_ÂI¡G", num1, 5, DOECHO);
	    inum1 = atoi(num1);
	    if (inum1 <= 0) {
		outmsg("°_ÂI¦³»~");
		refresh();
		/* safe_sleep(1); */
		return FULLUPDATE;
	    }
	    getdata(1, 28, "²×ÂI¡G", num2, 5, DOECHO);
	    inum2 = atoi(num2);
	    if (inum2 < inum1) {
		outmsg("²×ÂI¦³»~");
		refresh();
		/* safe_sleep(1); */
		return FULLUPDATE;
	    }
	    getdata(1, 48, msg_sure_ny, num1, 3, LCECHO);
	    if (*num1 == 'y') {
		outmsg("³B²z¤¤,½Ðµy«á...");
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
				strcpy(currfile, rsfh.filename);
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

    static int      del_post(int ent, fileheader_t * fhdr, char *direct){
	char            genbuf[100];
	int             not_owned;
	boardheader_t  *bp;

	                bp = getbcache(currbid);
	if              (strcmp(bp->brdname, "Security") == 0)
	                    return DONOTHING;
	if              ((fhdr->filemode & FILE_MARKED) || (fhdr->filemode & FILE_DIGEST) ||
			                 (fhdr->owner[0] == '-'))
	                    return DONOTHING;

	                not_owned = strcmp(fhdr->owner, cuser.userid);
	if              ((!(currmode & MODE_BOARD) && not_owned) ||
			                 ((bp->brdattr & BRD_VOTEBOARD) && !HAS_PERM(PERM_SYSOP)) ||
			                !strcmp(cuser.userid, STR_GUEST))
	                    return DONOTHING;

	                getdata(1, 0, msg_del_ny, genbuf, 3, LCECHO);
	if              (genbuf[0] == 'y' || genbuf[0] == 'Y') {
	    strcpy(currfile, fhdr->filename);

	    setbfile(genbuf, currboard, fhdr->filename);
	    if (!delete_file(direct, sizeof(fileheader_t), ent, cmpfilename)) {

		if (currmode & MODE_SELECT) {
		    /* rocker.011018: §Q¥Îreference´î§Cloading */
		    fileheader_t    hdr;
		    int             num;

		                    num = fhdr->money & ~FHR_REFERENCE;
		                    setbdir(genbuf, currboard);
		                    get_record(genbuf, &hdr, sizeof(hdr), num);

		    /* ¦A³o¸Ì­ncheck¤@¤U­ì¨Óªºdir¸Ì­±¬O¤£¬O¦³³Q¤H°Ê¹L... */
		    if              (strcmp(hdr.filename, fhdr->filename)) {
			num = getindex(genbuf, fhdr->filename, sizeof(fileheader_t));
			get_record(genbuf, &hdr, sizeof(hdr), num);
		    }
		    /* rocker.011018: ³o¸Ì­nÁÙ­ì³Q¯}Ãaªºmoney */
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
		    passwd_update(usernum, &cuser);	/* post ¼Æ */
		    prints("%s¡A±zªº¤å³¹´î¬° %d ½g¡A¤ä¥I²M¼ä¶O %d »È", msg_del_ok,
			   cuser.numposts, fhdr->money);
		    refresh();
		    pressanykey();
		}
		return DIRCHANGED;
	    }
	}
	return FULLUPDATE;
    }

    static int      view_postmoney(int ent, fileheader_t * fhdr, char *direct){
	move(b_lines - 1, 0);
	clrtoeol();
	prints("³o¤@½g¤å³¹­È %d »È", fhdr->money);
	refresh();
	pressanykey();
	return FULLUPDATE;
    }

#ifdef OUTJOBSPOOL
    /* ¬ÝªO³Æ¥÷ */
    static int      tar_addqueue(int ent, fileheader_t * fhdr, char *direct){
	char            email[60], qfn[80], ans[2];
	FILE           *fp;
	char            bakboard, bakman;
	                clear();
	                showtitle("¬ÝªO³Æ¥÷", BBSNAME);
	                move(2, 0);
	if              (!((currmode & MODE_BOARD) || HAS_PERM(PERM_SYSOP))) {
	    move(5, 10);
	    outs("©p­n¬OªO¥D©Î¬O¯¸ªø¤~¯àÂæÂæ°Ú -.-\"\"");
	    pressanykey();
	    return FULLUPDATE;
	}
	                sprintf(qfn, BBSHOME "/jobspool/tarqueue.%s", currboard);
	if (access(qfn, 0) == 0) {
	    outs("¤w¸g±Æ©w¦æµ{, µy«á·|¶i¦æ³Æ¥÷");
	    pressanykey();
	    return FULLUPDATE;
	}
	if (!getdata(4, 0, "½Ð¿é¤J¥Øªº«H½c¡G", email, sizeof(email), DOECHO))
	    return FULLUPDATE;

	/* check email -.-"" */
	if (strstr(email, "@") == NULL || strstr(email, ".bbs@") != NULL) {
	    move(6, 0);
	    outs("±z«ü©wªº«H½c¤£¥¿½T! ");
	    pressanykey();
	    return FULLUPDATE;
	}
	getdata(6, 0, "­n³Æ¥÷¬ÝªO¤º®e¶Ü(Y/N)?[Y]", ans, sizeof(ans), LCECHO);
	bakboard = (ans[0] == 'n' || ans[0] == 'N') ? 0 : 1;
	getdata(7, 0, "­n³Æ¥÷ºëµØ°Ï¤º®e¶Ü(Y/N)?[N]", ans, sizeof(ans), LCECHO);
	bakman = (ans[0] == 'y' || ans[0] == 'Y') ? 1 : 0;
	if (!bakboard && !bakman) {
	    move(8, 0);
	    outs("¥i¬O§Ú­Ì¥u¯à³Æ¥÷¬ÝªO©ÎºëµØ°Ïªº­C ^^\"\"\"");
	    pressanykey();
	    return FULLUPDATE;
	}
	fp = fopen(qfn, "w");
	fprintf(fp, "%s\n", cuser.userid);
	fprintf(fp, "%s\n", email);
	fprintf(fp, "%d,%d\n", bakboard, bakman);
	fclose(fp);

	move(10, 0);
	outs("¨t²Î¤w¸g±N±zªº³Æ¥÷±Æ¤J¦æµ{, \n");
	outs("µy«á±N·|¦b¨t²Î­t²ü¸û§Cªº®É­Ô±N¸ê®Æ±Hµ¹±z~ :) ");
	pressanykey();
	return FULLUPDATE;
    }
#endif

    static int      sequent_ent;
    static int      continue_flag;

    /* ----------------------------------------------------- */
    /* ¨Ì§ÇÅª·s¤å³¹                                          */
    /* ----------------------------------------------------- */
    static int      sequent_messages(fileheader_t * fptr) {
	static int      idc;
	char            genbuf[200];

	if              (fptr == NULL)
	                    return (idc = 0);

	if              (++idc < sequent_ent)
	                    return 0;

	if              (!brc_unread(fptr->filename, brc_num, brc_list))
	                    return 0;

	if              (continue_flag)
	                    genbuf[0] = 'y';
	else {
	    prints("Åª¨ú¤å³¹©ó¡G[%s] §@ªÌ¡G[%s]\n¼ÐÃD¡G[%s]",
		   currboard, fptr->owner, fptr->title);
	    getdata(3, 0, "(Y/N/Quit) [Y]: ", genbuf, 3, LCECHO);
	}

	if              (genbuf[0] != 'y' && genbuf[0]) {
	    clear();
	    return (genbuf[0] == 'q' ? QUIT : 0);
	}
	setbfile(genbuf, currboard, fptr->filename);
	brc_addlist(fptr->filename);

	if (more(genbuf, YEA) == 0)
	    outmsg("\033[31;47m  \033[31m(R)\033[30m¦^«H  \033[31m(¡õ,n)"
		   "\033[30m¤U¤@«Ê  \033[31m(¡ö,q)\033[30mÂ÷¶}  \033[m");
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
		strcpy(quote_file, genbuf);
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

    static int      sequential_read(int ent, fileheader_t * fhdr, char *direct){
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
    /* ¬ÝªO³Æ§Ñ¿ý¡B¤åºK¡BºëµØ°Ï                              */
    /* ----------------------------------------------------- */
    int             b_note_edit_bname(int bid){
	char            buf[64];
	int             aborted;
	boardheader_t  *fh = getbcache(bid);
	                setbfile(buf, fh->brdname, fn_notes);
	                aborted = vedit(buf, NA, NULL);
	if              (aborted == -1) {
	    clear();
	    outs(msg_cancel);
	    pressanykey();
	} else {
	    if (!getdata(2, 0, "³]©w¦³®Ä´Á­­¤Ñ¡H(n/Y)", buf, 3, LCECHO)
		|| buf[0] != 'n')
		fh->bupdate = gettime(3, fh->bupdate ? fh->bupdate : now);
	    else
		fh->bupdate = 0;
	    substitute_record(fn_board, fh, sizeof(boardheader_t), bid);
	}
	return 0;
    }

    static int      b_notes_edit() {
	if (currmode & MODE_BOARD) {
	    b_note_edit_bname(currbid);
	    return FULLUPDATE;
	}
	return 0;
    }

    static int      b_water_edit() {
	if (currmode & MODE_BOARD) {
	    friend_edit(BOARD_WATER);
	    return FULLUPDATE;
	}
	return 0;
    }

    static int      visable_list_edit() {
	if (currmode & MODE_BOARD) {
	    friend_edit(BOARD_VISABLE);
	    hbflreload(currbid);
	    return FULLUPDATE;
	}
	return 0;
    }

    static int      b_post_note() {
	char            buf[200], yn[3];

	if              (currmode & MODE_BOARD) {
	    setbfile(buf, currboard, FN_POST_NOTE);
	    if (more(buf, NA) == -1)
		more("etc/" FN_POST_NOTE, NA);
	    getdata(b_lines - 2, 0, "¬O§_­n¥Î¦Û­qpostª`·N¨Æ¶µ?", yn, sizeof(yn), LCECHO);
	    if (yn[0] == 'y')
		vedit(buf, NA, NULL);
	    else
		unlink(buf);
	    return FULLUPDATE;
	}
	                return 0;
    }

    static int      b_application() {
	char            buf[200];

	if              (currmode & MODE_BOARD) {
	    setbfile(buf, currboard, FN_APPLICATION);
	    vedit(buf, NA, NULL);
	    return FULLUPDATE;
	}
	                return 0;
    }

    static int      can_vote_edit() {
	if (currmode & MODE_BOARD) {
	    friend_edit(FRIEND_CANVOTE);
	    return FULLUPDATE;
	}
	return 0;
    }

    static int      bh_title_edit() {
	boardheader_t  *bp;

	if              (currmode & MODE_BOARD) {
	    char            genbuf[BTLEN];

	                    bp = getbcache(currbid);
	                    move(1, 0);
	                    clrtoeol();
	                    getdata_str(1, 0, "½Ð¿é¤J¬ÝªO·s¤¤¤å±Ô­z:", genbuf,
			                 BTLEN - 16, DOECHO, bp->title + 7);

	    if              (!genbuf[0])
		                return 0;
	                    strip_ansi(genbuf, genbuf, 0);
	                    strcpy(bp->title + 7, genbuf);
	                    substitute_record(fn_board, bp, sizeof(boardheader_t), currbid);
	                    log_usies("SetBoard", currboard);
	                    return FULLUPDATE;
	}
	                return 0;
    }

    static int      b_notes() {
	char            buf[64];

	                setbfile(buf, currboard, fn_notes);
	if              (more(buf, NA) == -1) {
	    clear();
	    move(4, 20);
	    outs("¥»¬ÝªO©|µL¡u³Æ§Ñ¿ý¡v¡C");
	}
	                pressanykey();
	return FULLUPDATE;
    }

    int             board_select() {
	char            fpath[80];
	char            genbuf[100];

	                currmode &= ~MODE_SELECT;
	                sprintf(fpath, "SR.%s", cuser.userid);
	                setbfile(genbuf, currboard, fpath);
	                unlink(genbuf);
	if              (currstat == RMAIL)
	                    sethomedir(currdirect, cuser.userid);
	else
	                    setbdir(currdirect, currboard);
	                return NEWDIRECT;
    }

    int             board_digest() {
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

    int             board_etc() {
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

    static int      good_post(int ent, fileheader_t * fhdr, char *direct){
	char            genbuf[200];
	char            genbuf2[200];
	int             delta = 0;

	if              ((currmode & MODE_DIGEST) || !(currmode & MODE_BOARD))
	                    return DONOTHING;

	if              (fhdr->filemode & FILE_DIGEST) {
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
	    strcpy(buf, direct);
	    ptr = strrchr(buf, '/') + 1;
	    ptr[0] = '\0';
	    sprintf(genbuf, "%s%s", buf, digest.filename);

	    if (dashf(genbuf))
		unlink(genbuf);

	    digest.filemode = 0;
	    sprintf(genbuf2, "%s%s", buf, fhdr->filename);
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
	/* rocker.011018: ¦ê±µ¼Ò¦¡¥Îreference¼W¶i®Ä²v */
	if ((currmode & MODE_SELECT) && (fhdr->money & FHR_REFERENCE)) {
	    fileheader_t    hdr;
	    char            genbuf[100];
	    int             num;

	    num = fhdr->money & ~FHR_REFERENCE;
	    setbdir(genbuf, currboard);
	    get_record(genbuf, &hdr, sizeof(hdr), num);

	    /* ¦A³o¸Ì­ncheck¤@¤U­ì¨Óªºdir¸Ì­±¬O¤£¬O¦³³Q¤H°Ê¹L... */
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
	"\0¥þ¥\\¯à¬ÝªO¾Þ§@»¡©ú",
	"\01°ò¥»©R¥O",
	"(p)(¡ô)   ¤W²¾¤@½g¤å³¹         (^P)     µoªí¤å³¹",
	"(n)(¡õ)   ¤U²¾¤@½g¤å³¹         (d)      §R°£¤å³¹",
	"(P)(PgUp) ¤W²¾¤@­¶             (S)      ¦ê³s¬ÛÃö¤å³¹",
	"(N)(PgDn) ¤U²¾¤@­¶             (##)     ¸õ¨ì ## ¸¹¤å³¹",
	"(r)(¡÷)   ¾\\Åª¦¹½g¤å³¹         ($)      ¸õ¨ì³Ì«á¤@½g¤å³¹",
	"\01¶i¶¥©R¥O",
	"(tab)/z   ¤åºK¼Ò¦¡/ºëµØ°Ï      (a/A)(^Q)§ä´M§@ªÌ/§@ªÌ¸ê®Æ",
	"(b/f)     ®iÅª³Æ§Ñ¿ý/°Ñ»P½ä½L  (?)(/)   §ä´M¼ÐÃD",
	"(V/R)     §ë²¼/¬d¸ß§ë²¼µ²ªG    (^W)(X)  §Ú¦b­þ¸Ì/±ÀÂË¤å³¹",
	"(x)(w)    Âà¿ý¤å³¹/¥á¤ô²y      (=)/([]<>-+) §ä´M­º½g¤å³¹/¥DÃD¦¡¾\\Åª",
#ifdef INTERNET_EMAIL
	"(F)       ¤å³¹±H¦^Internet¶l½c (U)      ±N¤å³¹ uuencode «á±H¦^¶l½c",
#endif
	"(E)       ­«½s¤å³¹             (^H)     ¦C¥X©Ò¦³ªº New Post(s)",
	"\01ªO¥D©R¥O",
	"(G)       Á|¿ì½ä½L/°±¤î¤Uª`/¶}¼ú(W/K/v) ½s¿è³Æ§Ñ¿ý/¤ô±í¦W³æ/¥i¬Ý¨£¦W³æ",
	"(M/o)     Á|¦æ§ë²¼/½s¨p§ë²¼¦W³æ (m/c/g) «O¯d¤å³¹/¿ï¿ýºëµØ/¤åºK",
	"(D)       §R°£¤@¬q½d³òªº¤å³¹    (T/B)   ­«½s¤å³¹¼ÐÃD/­«½s¬ÝªO¼ÐÃD",
	"(i)       ½s¿è¥Ó½Ð¤J·|ªí®æ      (t/^D)  ¼Ð°O¤å³¹/¬å°£¼Ð°Oªº¤å³¹",
	"(O)       ½s¿èPostª`·N¨Æ¶µ      (H)/(Y) ¬ÝªOÁôÂÃ/²{¨­ ¨ú®ø±ÀÂË¤å³¹",
	NULL
    };

    static int      b_help() {
	show_help(board_help);
	return FULLUPDATE;
    }

    /* ----------------------------------------------------- */
    /* ªO¥D³]©wÁô§Î/ ¸ÑÁô§Î                                  */
    /* ----------------------------------------------------- */
    char            board_hidden_status;
#ifdef  BMCHS
    static int      change_hidden(int ent, fileheader_t * fhdr, char *direct){
	boardheader_t   bh;
	int             bid;
	char            ans[4];

	if              (!((currmode & MODE_BOARD) || HAS_PERM(PERM_SYSOP)) ||
			                 currboard[0] == 0 ||
			                 (bid = getbnum(currboard)) < 0 ||
	                   get_record(fn_board, &bh, sizeof(bh), bid) == -1)
	                    return DONOTHING;

	if              (((bh.brdattr & BRD_HIDE) && (bh.brdattr & BRD_POSTMASK))) {
	    getdata(1, 0, "¥Ø«eªO¦bÁô§Îª¬ºA, ­n¸ÑÁô§Î¹À(Y/N)?[N]",
		    ans, sizeof(ans), LCECHO);
	    if (ans[0] != 'y' && ans[0] != 'Y')
		return FULLUPDATE;
	    getdata(2, 0, "¦A½T»{¤@¦¸, ¯uªº­n§âªOªO¤½¶}¹À @____@(Y/N)?[N]",
		    ans, sizeof(ans), LCECHO);
	    if (ans[0] != 'y' && ans[0] != 'Y')
		return FULLUPDATE;
	    if (bh.brdattr & BRD_HIDE)
		bh.brdattr -= BRD_HIDE;
	    if (bh.brdattr & BRD_POSTMASK)
		bh.brdattr -= BRD_POSTMASK;
	    log_usies("OpenBoard", bh.brdname);
	    outs("§g¤ß¤µ¶Ç²³¤H¡AµL³B¤£»D©¶ºq¡C\n");
	    board_hidden_status = 0;
	    hbflreload(bid);
	} else {
	    getdata(1, 0, "¥Ø«eªO¦b²{§Îª¬ºA, ­nÁô§Î¹À(Y/N)?[N]",
		    ans, sizeof(ans), LCECHO);
	    if (ans[0] != 'y' && ans[0] != 'Y')
		return FULLUPDATE;
	    bh.brdattr |= BRD_HIDE;
	    bh.brdattr |= BRD_POSTMASK;
	    log_usies("CloseBoard", bh.brdname);
	    outs("§g¤ß¤µ¤w±»§í¡A±©¬ßµ½¦Û¬Ã­«¡C\n");
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
    /* ¬ÝªO¥\¯àªí                                            */
    /* ----------------------------------------------------- */
    struct onekey_t read_comms[] = {
	{KEY_TAB, board_digest},
	{'C', board_etc},
	{'b', b_notes},
	{'c', cite_post},
	{'r', read_post},
	{'z', b_man},
	{'D', del_range},
	{'S', sequential_read},
	{'E', edit_post},
	{'T', edit_title},
	{'s', do_select},
	{'R', b_results},
	{'V', b_vote},
	{'M', b_vote_maintain},
	{'B', bh_title_edit},
	{'W', b_notes_edit},
	{'O', b_post_note},
	{'K', b_water_edit},
	{'w', b_call_in},
	{'v', visable_list_edit},
	{'i', b_application},
	{'o', can_vote_edit},
	{'x', cross_post},
	{'X', recommend},
	{'Y', recommend_cancel},
	{'h', b_help},
#ifndef NO_GAMBLE
	{'f', join_gamble},
	{'G', hold_gamble},
#endif
	{'g', good_post},
	{'y', reply_post},
	{'d', del_post},
	{'m', mark_post},
	{'L', solve_post},
	{Ctrl('P'), do_post},
	{Ctrl('W'), whereami},
	{'Q', view_postmoney},
#ifdef OUTJOBSPOOL
	{'u', tar_addqueue},
#endif
#ifdef BMCHS
	{'H', change_hidden},
#endif
	{'\0', NULL}
    };

    int             Read() {
	int             mode0 = currutmp->mode;
	int             stat0 = currstat, tmpbid = currutmp->brc_id;
	char            buf[40];
#ifdef LOG_BOARD
	time_t          usetime = now;
#endif

	                setutmpmode(READING);
	                set_board();

	if              (board_visit_time < board_note_time) {
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

    void            ReadSelect() {
	int             mode0 = currutmp->mode;
	int             stat0 = currstat;
	char            genbuf[200];

	                currstat = XMODE;
	if              (do_select(0, 0, genbuf) == NEWDIRECT)
	                    Read();
	                setutmpbid(0);
	                currutmp->mode = mode0;
	                currstat = stat0;
    }

#ifdef LOG_BOARD
    static void     log_board(char *mode, time_t usetime){
	char            buf[256];

	if              (usetime > 30) {
	    sprintf(buf, "USE %-20.20s Stay: %5ld (%s) %s",
		    mode, usetime, cuser.userid, ctime(&now));
	    log_file(FN_USEBOARD, buf);
	}
    }
#endif

    int             Select() {
	char            genbuf[200];

	                setutmpmode(SELECT);
	                do_select(0, NULL, genbuf);
	                return 0;
    }
#ifdef HAVEMOBILE
    void            mobile_message(char *mobile, char *message){



	bsmtp(char *fpath, char *title, char *rcpt, int method);
    }

#endif
