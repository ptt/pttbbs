/* $Id: mail.c,v 1.18 2002/07/05 17:10:27 in2 Exp $ */
#include "bbs.h"
char            currmaildir[32];
static char     msg_cc[] = "\033[32m[¸s²Õ¦W³æ]\033[m\n";
static char     listfile[] = "list.0";
static int      mailkeep = 0, mailsum = 0;
static int      mailsumlimit = 0, mailmaxkeep = 0;

int 
setforward()
{
    char            buf[80], ip[50] = "", yn[4];
    FILE           *fp;

    sethomepath(buf, cuser.userid);
    strcat(buf, "/.forward");
    if ((fp = fopen(buf, "r"))) {
	fscanf(fp, "%s", ip);
	fclose(fp);
    }
    getdata_buf(b_lines - 1, 0, "½Ð¿é¤J«H½c¦Û°ÊÂà±Hªºemail¦a§}:",
		ip, sizeof(ip), DOECHO);
    if (ip[0] && ip[0] != ' ') {
	getdata(b_lines, 0, "½T©w¶}±Ò¦Û°ÊÂà«H¥\\¯à?(Y/n)", yn, sizeof(yn),
		LCECHO);
	if (yn[0] != 'n' && (fp = fopen(buf, "w"))) {
	    move(b_lines, 0);
	    clrtoeol();
	    fprintf(fp, "%s", ip);
	    fclose(fp);
	    outs("³]©w§¹¦¨!");
	    refresh();
	    return 0;
	}
    }
    move(b_lines, 0);
    clrtoeol();
    outs("¨ú®ø¦Û°ÊÂà«H!");
    unlink(buf);
    refresh();
    return 0;
}

int 
built_mail_index()
{
    char            genbuf[128];

    getdata(b_lines, 0,
	    "­««Ø«H½c?(Äµ§i:½Ð½T©w«H½c¦³°ÝÃD®É¤~¨Ï¥Î)(y/N)", genbuf, 3,
	    LCECHO);
    if (genbuf[0] != 'y')
	return 0;

    sprintf(genbuf, BBSHOME "/bin/buildir " BBSHOME "/home/%c/%s",
	    cuser.userid[0], cuser.userid);
    move(22, 0);
    prints("\033[1;31m¤w¸g³B²z§¹²¦!! ½Ñ¦h¤£«K ·q½Ð­ì½Ì~\033[m");
    pressanykey();
    system(genbuf);
    return 0;
}

int 
mailalert(char *userid)
{
    userinfo_t     *uentp = NULL;
    int             n, tuid, i;

    if ((tuid = searchuser(userid)) == 0)
	return -1;

    n = count_logins(tuid, 0);
    for (i = 1; i <= n; i++)
	if ((uentp = (userinfo_t *) search_ulistn(tuid, i)))
	    uentp->mailalert = 1;
    return 0;
}

int 
mail_muser(userec_t muser, char *title, char *filename)
{
    return mail_id(muser.userid, title, filename, cuser.userid);
}

/* Heat: ¥Îid¨Ó±H«H,¤º®e«hlink·Ç³Æ¦nªºÀÉ®× */
int 
mail_id(char *id, char *title, char *filename, char *owner)
{
    fileheader_t    mhdr;
    char            genbuf[128];
    sethomepath(genbuf, id);
    if (stampfile(genbuf, &mhdr))
	return 0;
    strcpy(mhdr.owner, owner);
    strncpy(mhdr.title, title, TTLEN);
    mhdr.filemode = 0;
    Link(filename, genbuf);
    sethomedir(genbuf, id);
    append_record(genbuf, &mhdr, sizeof(mhdr));
    mailalert(id);
    return 0;
}

int 
invalidaddr(char *addr)
{
    if (*addr == '\0')
	return 1;		/* blank */
    while (*addr) {
	if (not_alnum(*addr) && !strchr("[].@-_", *addr))
	    return 1;
	addr++;
    }
    return 0;
}

int 
m_internet()
{
    char            receiver[60];

    getdata(20, 0, "¦¬«H¤H¡G", receiver, sizeof(receiver), DOECHO);
    if (strchr(receiver, '@') && !invalidaddr(receiver) &&
	getdata(21, 0, "¥D  ÃD¡G", save_title, STRLEN, DOECHO))
	do_send(receiver, save_title);
    else {
	move(22, 0);
	outs("¦¬«H¤H©Î¥DÃD¤£¥¿½T, ½Ð­«·s¿ï¨ú«ü¥O");
	pressanykey();
    }
    return 0;
}

void 
m_init()
{
    sethomedir(currmaildir, cuser.userid);
}

int 
chkmailbox()
{
    if (!HAVE_PERM(PERM_SYSOP) && !HAVE_PERM(PERM_MAILLIMIT)) {
	int             max_keepmail = MAX_KEEPMAIL;
	if (HAS_PERM(PERM_SYSSUBOP) || HAS_PERM(PERM_SMG) ||
	    HAS_PERM(PERM_PRG) || HAS_PERM(PERM_ACTION) || HAS_PERM(PERM_PAINT)) {
	    mailsumlimit = 700;
	    max_keepmail = 500;
	} else if (HAS_PERM(PERM_BM)) {
	    mailsumlimit = 500;
	    max_keepmail = 300;
	} else if (HAS_PERM(PERM_LOGINOK))
	    mailsumlimit = 200;
	else
	    mailsumlimit = 50;
	mailsumlimit += cuser.exmailbox * 10;
	mailmaxkeep = max_keepmail + cuser.exmailbox;
	m_init();
	if ((mailkeep = get_num_records(currmaildir, sizeof(fileheader_t))) >
	    mailmaxkeep) {
	    move(b_lines, 0);
	    clrtoeol();
	    bell();
	    prints("±z«O¦s«H¥ó¼Æ¥Ø %d ¶W¥X¤W­­ %d, ½Ð¾ã²z",
		   mailkeep, mailmaxkeep);
	    bell();
	    refresh();
	    igetch();
	    return mailkeep;
	}
	if ((mailsum = get_sum_records(currmaildir, sizeof(fileheader_t))) >
	    mailsumlimit) {
	    move(b_lines, 0);
	    clrtoeol();
	    bell();
	    prints("±z«O¦s«H¥ó®e¶q %d(k)¶W¥X¤W­­ %d(k), ½Ð¾ã²z",
		   mailsum, mailsumlimit);
	    bell();
	    refresh();
	    igetch();
	    return mailkeep;
	}
    }
    return 0;
}

static void 
do_hold_mail(char *fpath, char *receiver, char *holder)
{
    char            buf[80], title[128];

    fileheader_t    mymail;

    sethomepath(buf, holder);
    stampfile(buf, &mymail);

    mymail.filemode = FILE_READ | FILE_HOLD;
    strcpy(mymail.owner, "[³Æ.§Ñ.¿ý]");
    if (receiver) {
	sprintf(title, "(%s) %s", receiver, save_title);
	strncpy(mymail.title, title, TTLEN);
    } else
	strcpy(mymail.title, save_title);

    sethomedir(title, holder);

    unlink(buf);
    Link(fpath, buf);
    /* Ptt: append_record->do_append */
    do_append(title, &mymail, sizeof(mymail));
}

void 
hold_mail(char *fpath, char *receiver)
{
    char            buf[4];

    getdata(b_lines - 1, 0, "¤w¶¶§Q±H¥X¡A¬O§_¦Û¦s©³½Z(Y/N)¡H[N] ",
	    buf, sizeof(buf), LCECHO);

    if (buf[0] == 'y')
	do_hold_mail(fpath, receiver, cuser.userid);
}

int 
do_send(char *userid, char *title)
{
    fileheader_t    mhdr;
    char            fpath[STRLEN];
    char            receiver[IDLEN];
    char            genbuf[200];
    int             internet_mail, i;

    if (strchr(userid, '@'))
	internet_mail = 1;
    else {
	internet_mail = 0;
	if (!getuser(userid))
	    return -1;
	if (!(xuser.userlevel & PERM_READMAIL))
	    return -3;

	if (!title)
	    getdata(2, 0, "¥DÃD¡G", save_title, STRLEN - 20, DOECHO);
	curredit |= EDIT_MAIL;
	curredit &= ~EDIT_ITEM;
    }

    setutmpmode(SMAIL);

    fpath[0] = '\0';

    if (internet_mail) {
	int             res, ch;

	if (vedit(fpath, NA, NULL) == -1) {
	    unlink(fpath);
	    clear();
	    return -2;
	}
	clear();
	prints("«H¥ó§Y±N±Hµ¹ %s\n¼ÐÃD¬°¡G%s\n½T©w­n±H¥X¶Ü? (Y/N) [Y]",
	       userid, title);
	ch = igetch();
	switch (ch) {
	case 'N':
	case 'n':
	    outs("N\n«H¥ó¤w¨ú®ø");
	    res = -2;
	    break;
	default:
	    outs("Y\n½Ðµy­Ô, «H¥ó¶Ç»¼¤¤...\n");
	    res =
#ifndef USE_BSMTP
		bbs_sendmail(fpath, title, userid);
#else
		bsmtp(fpath, title, userid, 0);
#endif
	    hold_mail(fpath, userid);
	}
	unlink(fpath);
	return res;
    } else {
	strcpy(receiver, userid);
	sethomepath(genbuf, userid);
	stampfile(genbuf, &mhdr);
	strcpy(mhdr.owner, cuser.userid);
	strncpy(mhdr.title, save_title, TTLEN);
	if (vedit(genbuf, YEA, NULL) == -1) {
	    unlink(genbuf);
	    clear();
	    return -2;
	}
	clear();
	sethomefile(fpath, userid, FN_OVERRIDES);
	i = belong(fpath, cuser.userid);
	sethomefile(fpath, userid, FN_REJECT);

	if (i || !belong(fpath, cuser.userid))
    //Ptt: ¥Îbelong ¦ ³ÂI ° Q ¹ ½
	{
	    sethomedir(fpath, userid);
	    if (append_record(fpath, &mhdr, sizeof(mhdr)) == -1)
		return -1;
	    mailalert(userid);
	}
	hold_mail(genbuf, userid);
	return 0;
    }
}

void 
my_send(char *uident)
{
    switch (do_send(uident, NULL)) {
	case -1:
	outs(err_uid);
	break;
    case -2:
	outs(msg_cancel);
	break;
    case -3:
	prints("¨Ï¥ÎªÌ [%s] µLªk¦¬«H", uident);
	break;
    }
    pressanykey();
}

int 
m_send()
{
    char            uident[40];

    stand_title("¥BÅ¥­·ªº¸Ü");
    usercomplete(msg_uid, uident);
    showplans(uident);
    if (uident[0])
	my_send(uident);
    return 0;
}

/* ¸s²Õ±H«H¡B¦^«H : multi_send, multi_reply */
static void 
multi_list(int *reciper)
{
    char            uid[16];
    char            genbuf[200];

    while (1) {
	stand_title("¸s²Õ±H«H¦W³æ");
	ShowNameList(3, 0, msg_cc);
	getdata(1, 0,
		"(I)¤Þ¤J¦n¤Í (O)¤Þ¤J¤W½u³qª¾ (N)¤Þ¤J·s¤å³¹³qª¾ "
		"(0-9)¤Þ¤J¨ä¥L¯S§O¦W³æ\n"
	      "(A)¼W¥[     (D)§R°£         (M)½T»{±H«H¦W³æ   (Q)¨ú®ø ¡H[M]",
		genbuf, 4, LCECHO);
	switch (genbuf[0]) {
	case 'a':
	    while (1) {
		move(1, 0);
		usercomplete("½Ð¿é¤J­n¼W¥[ªº¥N¸¹(¥u«ö ENTER µ²§ô·s¼W): ", uid);
		if (uid[0] == '\0')
		    break;

		move(2, 0);
		clrtoeol();

		if (!searchuser(uid))
		    outs(err_uid);
		else if (!InNameList(uid)) {
		    AddNameList(uid);
		    (*reciper)++;
		}
		ShowNameList(3, 0, msg_cc);
	    }
	    break;
	case 'd':
	    while (*reciper) {
		move(1, 0);
		namecomplete("½Ð¿é¤J­n§R°£ªº¥N¸¹(¥u«ö ENTER µ²§ô§R°£): ", uid);
		if (uid[0] == '\0')
		    break;
		if (RemoveNameList(uid))
		    (*reciper)--;
		ShowNameList(3, 0, msg_cc);
	    }
	    break;
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	    listfile[5] = genbuf[0];
	    genbuf[0] = '1';
	case 'i':
	    setuserfile(genbuf, genbuf[0] == '1' ? listfile : fn_overrides);
	    ToggleNameList(reciper, genbuf, msg_cc);
	    break;
	case 'o':
	    setuserfile(genbuf, "alohaed");
	    ToggleNameList(reciper, genbuf, msg_cc);
	    break;
	case 'n':
	    setuserfile(genbuf, "postlist");
	    ToggleNameList(reciper, genbuf, msg_cc);
	    break;
	case 'q':
	    *reciper = 0;
	    return;
	default:
	    return;
	}
    }
}

static void 
multi_send(char *title)
{
    FILE           *fp;
    struct word_t  *p = NULL;
    fileheader_t    mymail;
    char            fpath[TTLEN], *ptr;
    int             reciper, listing;
    char            genbuf[256];

    CreateNameList();
    listing = reciper = 0;
    if (*quote_file) {
	AddNameList(quote_user);
	reciper = 1;
	fp = fopen(quote_file, "r");
	while (fgets(genbuf, 256, fp)) {
	    if (strncmp(genbuf, "¡° ", 3)) {
		if (listing)
		    break;
	    } else {
		if (listing) {
		    strtok(ptr = genbuf + 3, " \n\r");
		    do {
			if (searchuser(ptr) && !InNameList(ptr) &&
			    strcmp(cuser.userid, ptr)) {
			    AddNameList(ptr);
			    reciper++;
			}
		    } while ((ptr = (char *)strtok(NULL, " \n\r")));
		} else if (!strncmp(genbuf + 3, "[³q§i]", 6))
		    listing = 1;
	    }
	}
	ShowNameList(3, 0, msg_cc);
    }
    multi_list(&reciper);
    move(1, 0);
    clrtobot();

    if (reciper) {
	setutmpmode(SMAIL);
	if (title)
	    do_reply_title(2, title);
	else {
	    getdata(2, 0, "¥DÃD¡G", fpath, sizeof(fpath), DOECHO);
	    sprintf(save_title, "[³q§i] %s", fpath);
	}

	setuserfile(fpath, fn_notes);

	if ((fp = fopen(fpath, "w"))) {
	    fprintf(fp, "¡° [³q§i] ¦@ %d ¤H¦¬¥ó", reciper);
	    listing = 80;

	    for (p = toplev; p; p = p->next) {
		reciper = strlen(p->word) + 1;
		if (listing + reciper > 75) {
		    listing = reciper;
		    fprintf(fp, "\n¡°");
		} else
		    listing += reciper;

		fprintf(fp, " %s", p->word);
	    }
	    memset(genbuf, '-', 75);
	    genbuf[75] = '\0';
	    fprintf(fp, "\n%s\n\n", genbuf);
	    fclose(fp);
	}
	curredit |= EDIT_LIST;

	if (vedit(fpath, YEA, NULL) == -1) {
	    unlink(fpath);
	    curredit = 0;
	    outs(msg_cancel);
	    pressanykey();
	    return;
	}
	stand_title("±H«H¤¤...");
	refresh();

	listing = 80;

	for (p = toplev; p; p = p->next) {
	    reciper = strlen(p->word) + 1;
	    if (listing + reciper > 75) {
		listing = reciper;
		outc('\n');
	    } else {
		listing += reciper;
		outc(' ');
	    }
	    outs(p->word);
	    if (searchuser(p->word) && strcmp(STR_GUEST, p->word))
		sethomepath(genbuf, p->word);
	    else
		continue;
	    stampfile(genbuf, &mymail);
	    unlink(genbuf);
	    Link(fpath, genbuf);

	    strcpy(mymail.owner, cuser.userid);
	    strcpy(mymail.title, save_title);
	    mymail.filemode |= FILE_MULTI;	/* multi-send flag */
	    sethomedir(genbuf, p->word);
	    if (append_record(genbuf, &mymail, sizeof(mymail)) == -1)
		outs(err_uid);
	    mailalert(p->word);
	}
	hold_mail(fpath, NULL);
	unlink(fpath);
	curredit = 0;
    } else
	outs(msg_cancel);
    pressanykey();
}

static int 
multi_reply(int ent, fileheader_t * fhdr, char *direct)
{
    if (!(fhdr->filemode & FILE_MULTI))
	return mail_reply(ent, fhdr, direct);

    stand_title("¸s²Õ¦^«H");
    strcpy(quote_user, fhdr->owner);
    setuserfile(quote_file, fhdr->filename);
    multi_send(fhdr->title);
    return 0;
}

int 
mail_list()
{
    stand_title("¸s²Õ§@·~");
    multi_send(NULL);
    return 0;
}

int 
mail_all()
{
    FILE           *fp;
    fileheader_t    mymail;
    char            fpath[TTLEN];
    char            genbuf[200];
    int             i, unum;
    char           *userid;

    stand_title("µ¹©Ò¦³¨Ï¥ÎªÌªº¨t²Î³q§i");
    setutmpmode(SMAIL);
    getdata(2, 0, "¥DÃD¡G", fpath, sizeof(fpath), DOECHO);
    sprintf(save_title, "[¨t²Î³q§i]\033[1;32m %s\033[m", fpath);

    setuserfile(fpath, fn_notes);

    if ((fp = fopen(fpath, "w"))) {
	fprintf(fp, "¡° [\033[1m¨t²Î³q§i\033[m] ³o¬O«Êµ¹©Ò¦³¨Ï¥ÎªÌªº«H\n");
	fprintf(fp, "-----------------------------------------------------"
		"----------------------\n");
	fclose(fp);
    }
    *quote_file = 0;

    curredit |= EDIT_MAIL;
    curredit &= ~EDIT_ITEM;
    if (vedit(fpath, YEA, NULL) == -1) {
	curredit = 0;
	unlink(fpath);
	outs(msg_cancel);
	pressanykey();
	return 0;
    }
    curredit = 0;

    setutmpmode(MAILALL);
    stand_title("±H«H¤¤...");

    sethomepath(genbuf, cuser.userid);
    stampfile(genbuf, &mymail);
    unlink(genbuf);
    Link(fpath, genbuf);
    unlink(fpath);
    strcpy(fpath, genbuf);

    strcpy(mymail.owner, cuser.userid);	/* ¯¸ªø ID */
    strcpy(mymail.title, save_title);

    sethomedir(genbuf, cuser.userid);
    if (append_record(genbuf, &mymail, sizeof(mymail)) == -1)
	outs(err_uid);

    for (unum = SHM->number, i = 0; i < unum; i++) {
	if (bad_user_id(SHM->userid[i]))
	    continue;		/* Ptt */

	userid = SHM->userid[i];
	if (strcmp(userid, STR_GUEST) && strcmp(userid, "new") &&
	    strcmp(userid, cuser.userid)) {
	    sethomepath(genbuf, userid);
	    stampfile(genbuf, &mymail);
	    unlink(genbuf);
	    Link(fpath, genbuf);

	    strcpy(mymail.owner, cuser.userid);
	    strcpy(mymail.title, save_title);
	    /* mymail.filemode |= FILE_MARKED; Ptt ¤½§i§ï¦¨¤£·|mark */
	    sethomedir(genbuf, userid);
	    if (append_record(genbuf, &mymail, sizeof(mymail)) == -1)
		outs(err_uid);
	    sprintf(genbuf, "%*s %5d / %5d", IDLEN + 1, userid, i + 1, unum);
	    outmsg(genbuf);
	    refresh();
	}
    }
    return 0;
}

int 
mail_mbox()
{
    char            cmd[100];
    fileheader_t    fhdr;

    sprintf(cmd, "/tmp/%s.uu", cuser.userid);
    sprintf(fhdr.title, "%s ¨p¤H¸ê®Æ", cuser.userid);
    doforward(cmd, &fhdr, 'Z');
    return 0;
}

static int 
m_forward(int ent, fileheader_t * fhdr, char *direct)
{
    char            uid[STRLEN];

    stand_title("Âà¹F«H¥ó");
    usercomplete(msg_uid, uid);
    if (uid[0] == '\0')
	return FULLUPDATE;

    strcpy(quote_user, fhdr->owner);
    setuserfile(quote_file, fhdr->filename);
    sprintf(save_title, "%.64s (fwd)", fhdr->title);
    move(1, 0);
    clrtobot();
    prints("Âà«Hµ¹: %s\n¼Ð  ÃD: %s\n", uid, save_title);

    switch (do_send(uid, save_title)) {
    case -1:
	outs(err_uid);
	break;
    case -2:
	outs(msg_cancel);
	break;
    case -3:
	prints("¨Ï¥ÎªÌ [%s] µLªk¦¬«H", uid);
	break;
    }
    pressanykey();
    return FULLUPDATE;
}

static int      delmsgs[128];
static int      delcnt;
static int      mrd;

static int 
read_new_mail(fileheader_t * fptr)
{
    static int      idc;
    char            done = NA, delete_it;
    char            fname[256];
    char            genbuf[4];

    if (fptr == NULL) {
	delcnt = 0;
	idc = 0;
	return 0;
    }
    idc++;
    if (fptr->filemode)
	return 0;
    clear();
    move(10, 0);
    prints("±z­nÅª¨Ó¦Û[%s]ªº°T®§(%s)¶Ü¡H", fptr->owner, fptr->title);
    getdata(11, 0, "½Ð±z½T©w(Y/N/Q)?[Y] ", genbuf, 3, DOECHO);
    if (genbuf[0] == 'q')
	return QUIT;
    if (genbuf[0] == 'n')
	return 0;

    setuserfile(fname, fptr->filename);
    fptr->filemode |= FILE_READ;
    if (substitute_record(currmaildir, fptr, sizeof(*fptr), idc))
	return -1;

    mrd = 1;
    delete_it = NA;
    while (!done) {
	int             more_result = more(fname, YEA);

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
	    return 0;
	case 7:
	    mail_reply(idc, fptr, currmaildir);
	    return FULLUPDATE;
	case 8:
	    multi_reply(idc, fptr, currmaildir);
	    return FULLUPDATE;
	}
	move(b_lines, 0);
	clrtoeol();
	outs(msg_mailer);
	refresh();

	switch (egetch()) {
	case 'r':
	case 'R':
	    mail_reply(idc, fptr, currmaildir);
	    break;
	case 'x':
	    m_forward(idc, fptr, currmaildir);
	    break;
	case 'y':
	    multi_reply(idc, fptr, currmaildir);
	    break;
	case 'd':
	case 'D':
	    delete_it = YEA;
	default:
	    done = YEA;
	}
    }
    if (delete_it) {
	clear();
	prints("§R°£«H¥ó¡m%s¡n", fptr->title);
	getdata(1, 0, msg_sure_ny, genbuf, 2, LCECHO);
	if (genbuf[0] == 'y') {
	    unlink(fname);
	    delmsgs[delcnt++] = idc;
	}
    }
    clear();
    return 0;
}

int 
m_new()
{
    clear();
    mrd = 0;
    setutmpmode(RMAIL);
    read_new_mail(NULL);
    clear();
    curredit |= EDIT_MAIL;
    curredit &= ~EDIT_ITEM;
    if (apply_record(currmaildir, read_new_mail, sizeof(fileheader_t)) == -1) {
	outs("¨S¦³·s«H¥ó¤F");
	pressanykey();
	return -1;
    }
    curredit = 0;
    if (delcnt) {
	while (delcnt--)
	    delete_record(currmaildir, sizeof(fileheader_t), delmsgs[delcnt]);
    }
    outs(mrd ? "«H¤w¾\\²¦" : "¨S¦³·s«H¥ó¤F");
    pressanykey();
    return -1;
}

static void 
mailtitle()
{
    char            buf[256] = "";

    showtitle("\0¶l¥ó¿ï³æ", BBSName);
    sprintf(buf, "[¡ö]Â÷¶}[¡ô¡õ]¿ï¾Ü[¡÷]¾\\Åª«H¥ó [R]¦^«H [x]Âà¹F "
	    "[y]¸s²Õ¦^«H [O]¯¸¥~«H:%s [h]¨D§U\n\033[7m"
	    "½s¸¹   ¤é ´Á  §@ ªÌ          «H  ¥ó  ¼Ð  ÃD     \033[32m",
	    HAS_PERM(PERM_NOOUTMAIL) ? "\033[31mÃö\033[m" : "¶}");
    outs(buf);
    buf[0] = 0;
    if (mailsumlimit) {
	sprintf(buf, "(®e¶q:%d/%dk %d/%d½g)", mailsum, mailsumlimit,
		mailkeep, mailmaxkeep);
    }
    sprintf(buf, "%s%*s\033[m", buf, 29 - (int)strlen(buf), "");
    outs(buf);
}

static void 
maildoent(int num, fileheader_t * ent)
{
    char           *title, *mark, color, type = "+ Mm"[(ent->filemode & 3)];

    if (TagNum && !Tagger(atoi(ent->filename + 2), 0, TAG_NIN))
	type = 'D';

    title = subject(mark = ent->title);
    if (title == mark) {
	color = '1';
	mark = "¡º";
    } else {
	color = '3';
	mark = "R:";
    }

    if (strncmp(currtitle, title, TTLEN))
	prints("%5d %c %-7s%-15.14s%s %.46s\n", num, type,
	       ent->date, ent->owner, mark, title);
    else
	prints("%5d %c %-7s%-15.14s\033[1;3%cm%s %.46s\033[0m\n", num, type,
	       ent->date, ent->owner, color, mark, title);
}

static int 
m_idle(int ent, fileheader_t * fhdr, char *direct)
{
    t_idle();
    return FULLUPDATE;
}

static int 
mail_del(int ent, fileheader_t * fhdr, char *direct)
{
    char            genbuf[200];

    if (fhdr->filemode & FILE_MARKED)
	return DONOTHING;

    getdata(1, 0, msg_del_ny, genbuf, 3, LCECHO);
    if (genbuf[0] == 'y') {
	strcpy(currfile, fhdr->filename);
	if (!delete_file(direct, sizeof(*fhdr), ent, cmpfilename)) {
	    setdirpath(genbuf, direct, fhdr->filename);
	    unlink(genbuf);
	    if ((currmode & MODE_SELECT)) {
		int             index;
		sethomedir(genbuf, cuser.userid);
		index = getindex(genbuf, fhdr->filename, sizeof(fileheader_t));
		delete_file(genbuf, sizeof(fileheader_t), index, cmpfilename);
	    }
	    return DIRCHANGED;
	}
    }
    return FULLUPDATE;
}

static int 
mail_read(int ent, fileheader_t * fhdr, char *direct)
{
    char            buf[64];
    char            done, delete_it, replied;

    clear();
    setdirpath(buf, direct, fhdr->filename);
    strncpy(currtitle, subject(fhdr->title), TTLEN);
    done = delete_it = replied = NA;
    while (!done) {
	int             more_result = more(buf, YEA);

	if (more_result != -1) {
	    fhdr->filemode |= FILE_READ;
	    if ((currmode & MODE_SELECT)) {
		int             index;

		index = getindex(currmaildir, fhdr->filename,
				 sizeof(fileheader_t));
		substitute_record(currmaildir, fhdr, sizeof(*fhdr), index);
		substitute_record(direct, fhdr, sizeof(*fhdr), ent);
	    } else
		substitute_record(currmaildir, fhdr, sizeof(*fhdr), ent);
	}
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
	    mail_reply(ent, fhdr, direct);
	    return FULLUPDATE;
	case 8:
	    multi_reply(ent, fhdr, direct);
	    return FULLUPDATE;
	}
	move(b_lines, 0);
	clrtoeol();
	refresh();
	outs(msg_mailer);

	switch (egetch()) {
	case 'r':
	case 'R':
	    replied = YEA;
	    mail_reply(ent, fhdr, direct);
	    break;
	case 'x':
	    m_forward(ent, fhdr, direct);
	    break;
	case 'y':
	    multi_reply(ent, fhdr, direct);
	    break;
	case 'd':
	    delete_it = YEA;
	default:
	    done = YEA;
	}
    }
    if (delete_it)
	mail_del(ent, fhdr, direct);
    else {
	fhdr->filemode |= FILE_READ;
#ifdef POSTBUG
	if (replied)
	    bug_possible = YEA;
#endif
	if ((currmode & MODE_SELECT)) {
	    int             index;

	    index = getindex(currmaildir, fhdr->filename, sizeof(fileheader_t));
	    substitute_record(currmaildir, fhdr, sizeof(*fhdr), index);
	    substitute_record(direct, fhdr, sizeof(*fhdr), ent);
	} else
	    substitute_record(currmaildir, fhdr, sizeof(*fhdr), ent);
#ifdef POSTBUG
	bug_possible = NA;
#endif
    }
    return FULLUPDATE;
}

/* in boards/mail ¦^«Hµ¹­ì§@ªÌ¡AÂà«H¯¸¥ç¥i */
int 
mail_reply(int ent, fileheader_t * fhdr, char *direct)
{
    char            uid[STRLEN];
    char           *t;
    FILE           *fp;
    char            genbuf[512];

    stand_title("¦^  «H");

    /* §PÂ_¬O boards ©Î mail */
    if (curredit & EDIT_MAIL)
	setuserfile(quote_file, fhdr->filename);
    else
	setbfile(quote_file, currboard, fhdr->filename);

    /* find the author */
    strcpy(quote_user, fhdr->owner);
    if (strchr(quote_user, '.')) {
	genbuf[0] = '\0';
	if ((fp = fopen(quote_file, "r"))) {
	    fgets(genbuf, 512, fp);
	    fclose(fp);
	}
	t = strtok(genbuf, str_space);
	if (!strcmp(t, str_author1) || !strcmp(t, str_author2))
	    strcpy(uid, strtok(NULL, str_space));
	else {
	    outs("¿ù»~: §ä¤£¨ì§@ªÌ¡C");
	    pressanykey();
	    return FULLUPDATE;
	}
    } else
	strcpy(uid, quote_user);

    /* make the title */
    do_reply_title(3, fhdr->title);
    prints("\n¦¬«H¤H: %s\n¼Ð  ÃD: %s\n", uid, save_title);

    /* edit, then send the mail */
    ent = curredit;
    switch (do_send(uid, save_title)) {
    case -1:
	outs(err_uid);
	break;
    case -2:
	outs(msg_cancel);
	break;
    case -3:
	prints("¨Ï¥ÎªÌ [%s] µLªk¦¬«H", uid);
	break;
    }
    curredit = ent;
    pressanykey();
    return FULLUPDATE;
}

static int 
mail_edit(int ent, fileheader_t * fhdr, char *direct)
{
    char            genbuf[200];

    if (!HAS_PERM(PERM_SYSOP) &&
	strcmp(cuser.userid, fhdr->owner) &&
	strcmp("[³Æ.§Ñ.¿ý]", fhdr->owner))
	return DONOTHING;

    setdirpath(genbuf, direct, fhdr->filename);
    vedit(genbuf, NA, NULL);
    return FULLUPDATE;
}

static int 
mail_nooutmail(int ent, fileheader_t * fhdr, char *direct)
{
    cuser.userlevel ^= PERM_NOOUTMAIL;
    passwd_update(usernum, &cuser);
    return FULLUPDATE;

}

static int 
mail_mark(int ent, fileheader_t * fhdr, char *direct)
{
    fhdr->filemode ^= FILE_MARKED;

    if ((currmode & MODE_SELECT)) {
	int             index;

	index = getindex(currmaildir, fhdr->filename, sizeof(fileheader_t));
	substitute_record(currmaildir, fhdr, sizeof(*fhdr), index);
	substitute_record(direct, fhdr, sizeof(*fhdr), ent);
    } else
	substitute_record(currmaildir, fhdr, sizeof(*fhdr), ent);
    return PART_REDRAW;
}

/* help for mail reading */
static char    *mail_help[] = {
    "\0¹q¤l«H½c¾Þ§@»¡©ú",
    "\01°ò¥»©R¥O",
    "(p)(¡ô)    «e¤@½g¤å³¹",
    "(n)(¡õ)    ¤U¤@½g¤å³¹",
    "(P)(PgUp)  «e¤@­¶",
    "(N)(PgDn)  ¤U¤@­¶",
    "(##)(cr)   ¸õ¨ì²Ä ## µ§",
    "($)        ¸õ¨ì³Ì«á¤@µ§",
    "\01¶i¶¥©R¥O",
    "(r)(¡÷)/(R)Åª«H / ¦^«H",
    "(O)        Ãö³¬/¶}±Ò ¯¸¥~«H¥óÂà¤J",
    "(c/z)      ¦¬¤J¦¹«H¥ó¶i¤J¨p¤H«H¥ó§¨/¶i¤J¨p¤H«H¥ó§¨",
    "(x/X)      Âà¹F«H¥ó/Âà¿ý¤å³¹¨ì¨ä¥L¬ÝªO",
    "(y)        ¸s²Õ¦^«H",
    "(F)        ±N«H¶Ç°e¦^±zªº¹q¤l«H½c      (u)¤ô²y¾ã²z±H¦^«H½c",
    "(d)        ±þ±¼¦¹«H",
    "(D)        ±þ±¼«ü©w½d³òªº«H",
    "(m)        ±N«H¼Ð°O¡A¥H¨¾³Q²M°£",
    "(^G)       ¥ß§Y­««Ø«H½c («H½c·´·l®É¥Î)",
    "(t)        ¼Ð°O±ý§R°£«H¥ó",
    "(^D)       §R°£¤w¼Ð°O«H¥ó",
    NULL
};

static int 
m_help()
{
    show_help(mail_help);
    return FULLUPDATE;
}

static int 
mail_cross_post(int ent, fileheader_t * fhdr, char *direct)
{
    char            xboard[20], fname[80], xfpath[80], xtitle[80], inputbuf[10];
    fileheader_t    xfile;
    FILE           *xptr;
    int             author = 0;
    char            genbuf[200];
    char            genbuf2[4];

    move(2, 0);
    clrtoeol();
    move(3, 0);
    clrtoeol();
    move(1, 0);
    generalnamecomplete("Âà¿ý¥»¤å³¹©ó¬ÝªO¡G", xboard, sizeof(xboard),
			SHM->Bnumber,
			completeboard_compar,
			completeboard_permission,
			completeboard_getname);
    if (*xboard == '\0' || !haspostperm(xboard))
	return FULLUPDATE;

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
    getdata(2, 0, genbuf, genbuf2, sizeof(genbuf2), LCECHO);
    if (*genbuf2 == 'n')
	if (getdata(2, 0, "¼ÐÃD¡G", genbuf, TTLEN, DOECHO))
	    strcpy(xtitle, genbuf);

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
	setuserfile(fname, fhdr->filename);
	if (ent) {
	    xptr = fopen(xfpath, "w");

	    strcpy(save_title, xfile.title);
	    strcpy(xfpath, currboard);
	    strcpy(currboard, xboard);
	    write_header(xptr);
	    strcpy(currboard, xfpath);

	    fprintf(xptr, "¡° [¥»¤åÂà¿ý¦Û %s «H½c]\n\n", cuser.userid);

	    b_suckinfile(xptr, fname);
	    addsignature(xptr, 0);
	    fclose(xptr);
	} else {
	    unlink(xfpath);
	    Link(fname, xfpath);
	}

	setbdir(fname, xboard);
	append_record(fname, &xfile, sizeof(xfile));
	setbtotal(getbnum(xboard));
	if (!xfile.filemode)
	    outgo_post(&xfile, xboard);
	cuser.numposts++;
	passwd_update(usernum, &cuser);
	outs("¤å³¹Âà¿ý§¹¦¨");
	pressanykey();
	currmode = currmode0;
    }
    return FULLUPDATE;
}

int 
mail_man()
{
    char            buf[64], buf1[64];
    if (HAS_PERM(PERM_MAILLIMIT)) {
	int             mode0 = currutmp->mode;
	int             stat0 = currstat;

	sethomeman(buf, cuser.userid);
	sprintf(buf1, "%s ªº«H¥ó§¨", cuser.userid);
	a_menu(buf1, buf, 1);
	currutmp->mode = mode0;
	currstat = stat0;
	return FULLUPDATE;
    }
    return DONOTHING;
}

static int 
mail_cite(int ent, fileheader_t * fhdr, char *direct)
{
    char            fpath[256];
    char            title[TTLEN + 1];
    static char     xboard[20];
    char            buf[20];
    boardheader_t  *bp;

    setuserfile(fpath, fhdr->filename);
    strcpy(title, "¡º ");
    strncpy(title + 3, fhdr->title, TTLEN - 3);
    title[TTLEN] = '\0';
    a_copyitem(fpath, title, 0, 1);

    if (cuser.userlevel >= PERM_BM) {
	move(2, 0);
	clrtoeol();
	move(3, 0);
	clrtoeol();
	move(1, 0);

	generalnamecomplete("¿é¤J¬ÝªO¦WºÙ (ª½±µEnter¶i¤J¨p¤H«H¥ó§¨)¡G",
			    buf, sizeof(buf),
			    SHM->Bnumber,
			    completeboard_compar,
			    completeboard_permission,
			    completeboard_getname);
	if (*buf)
	    strcpy(xboard, buf);
	if (*xboard && (bp = getbcache(getbnum(xboard)))) {
	    setapath(fpath, xboard);
	    setutmpmode(ANNOUNCE);
	    a_menu(xboard, fpath, HAS_PERM(PERM_ALLBOARD) ? 2 :
		   is_BM(bp->BM) ? 1 : 0);
	} else {
	    mail_man();
	}
	return FULLUPDATE;
    } else {
	mail_man();
	return FULLUPDATE;
    }
}

static int 
mail_save(int ent, fileheader_t * fhdr, char *direct)
{
    char            fpath[256];
    char            title[TTLEN + 1];

    if (HAS_PERM(PERM_MAILLIMIT)) {
	setuserfile(fpath, fhdr->filename);
	strcpy(title, "¡º ");
	strncpy(title + 3, fhdr->title, TTLEN - 3);
	title[TTLEN] = '\0';
	a_copyitem(fpath, title, fhdr->owner, 1);
	sethomeman(fpath, cuser.userid);
	a_menu(cuser.userid, fpath, 1);
	return FULLUPDATE;
    }
    return DONOTHING;
}

#ifdef OUTJOBSPOOL
static int 
mail_waterball(int ent, fileheader_t * fhdr, char *direct)
{
    static char     address[60], cmode = 1;
    char            fname[500], genbuf[200];
    FILE           *fp;

    if (!(strstr(fhdr->title, "¼ö½u") && strstr(fhdr->title, "°O¿ý"))) {
	vmsg("¥²¶·¬O ¼ö½u°O¿ý ¤~¯à¨Ï¥Î¤ô²y¾ã²zªº­ò!");
	return 1;
    }
    if (!address[0])
	strcpy(address, cuser.email);
    move(b_lines - 5, 0);
    outs("¤ô²y¾ã²zµ{¦¡:\n"
	 "¨t²Î±N·|«ö·Ó©M¤£¦P¤H¥áªº¤ô²y¦U¦Û¿W¥ß\n"
	 "©ó¾ãÂIªº®É­Ô (¦y¾W®É¬q°£¥~) ±N¸ê®Æ¾ã²z¦n±H°eµ¹±z\n\n\n");
    if (address[0]) {
	sprintf(genbuf, "±Hµ¹ [%s] ¶Ü(Y/N/Q)¡H[Y] ", address);
	getdata(b_lines - 2, 0, genbuf, fname, 3, LCECHO);
	if (fname[0] == 'q') {
	    outmsg("¨ú®ø³B²z");
	    return 1;
	}
	if (fname[0] == 'n')
	    address[0] = '\0';
    }
    if (!address[0]) {
	getdata(b_lines - 2, 0, "½Ð¿é¤J¶l¥ó¦a§}¡G", fname, 60, DOECHO);
	if (fname[0] && strchr(fname, '.')) {
	    strcpy(address, fname);
	} else {
	    vmsg("¨ú®ø³B²z");
	    return 1;
	}
    }
    if (invalidaddr(address))
	return -2;

    //sprintf(fname, "%d\n", cmode);
    getdata(b_lines - 1, 0, "¨Ï¥Î¼Ò¦¡(0/1/Q)? [1]", fname, 3, LCECHO);
    if (fname[0] == 'Q' || fname[0] == 'q') {
	outmsg("¨ú®ø³B²z");
	return 1;
    }
    cmode = (fname[0] != '0' && fname[0] != '1') ? 1 : fname[0] - '0';

    sprintf(fname, BBSHOME "/jobspool/water.src.%s-%d",
	    cuser.userid, (int)now);
    sprintf(genbuf, "cp " BBSHOME "/home/%c/%s/%s %s",
	    cuser.userid[0], cuser.userid, fhdr->filename, fname);
    system(genbuf);
    /* dirty code ;x */
    sprintf(fname, BBSHOME "/jobspool/water.des.%s-%d",
	    cuser.userid, (int)now);
    fp = fopen(fname, "wt");
    fprintf(fp, "%s\n%s\n%d\n", cuser.userid, address, cmode);
    fclose(fp);
    vmsg("³]©w§¹¦¨, ¨t²Î±N¦b¤U¤@­Ó¾ãÂI(¦y¾W®É¬q°£¥~)±N¸ê®Æ±Hµ¹±z");
    return FULLUPDATE;
}
#endif
static struct onekey_t mail_comms[] = {
    {'z', mail_man},
    {'c', mail_cite},
    {'s', mail_save},
    {'d', mail_del},
    {'D', del_range},
    {'r', mail_read},
    {'R', mail_reply},
    {'E', mail_edit},
    {'m', mail_mark},
    {'O', mail_nooutmail},
    {'T', edit_title},
    {'x', m_forward},
    {'X', mail_cross_post},
    {Ctrl('G'), built_mail_index},	/* ­×«H½c */
    {'y', multi_reply},
    {Ctrl('I'), m_idle},
    {'h', m_help},
#ifdef OUTJOBSPOOL
    {'u', mail_waterball},
#endif
    {'\0', NULL}
};

int 
m_read()
{
    if (get_num_records(currmaildir, sizeof(fileheader_t))) {
	curredit = EDIT_MAIL;
	curredit &= ~EDIT_ITEM;
	i_read(RMAIL, currmaildir, mailtitle, maildoent, mail_comms, -1);
	curredit = 0;
	currutmp->mailalert = load_mailalert(cuser.userid);
	return 0;
    } else {
	outs("±z¨S¦³¨Ó«H");
	return XEASY;
    }
}

/* ±H¯¸¤º«H */
static int 
send_inner_mail(char *fpath, char *title, char *receiver)
{
    char            genbuf[256];
    fileheader_t    mymail;

    if (!searchuser(receiver))
	return -2;
    sethomepath(genbuf, receiver);
    stampfile(genbuf, &mymail);
    if (!strcmp(receiver, cuser.userid)) {
	strcpy(mymail.owner, "[" BBSNAME "]");
	mymail.filemode = FILE_READ;
    } else
	strcpy(mymail.owner, cuser.userid);
    strncpy(mymail.title, title, TTLEN);
    unlink(genbuf);
    Link(fpath, genbuf);
    sethomedir(genbuf, receiver);
    return do_append(genbuf, &mymail, sizeof(mymail));
}

#include <netdb.h>
#include <pwd.h>
#include <time.h>

#ifndef USE_BSMTP
static int 
bbs_sendmail(char *fpath, char *title, char *receiver)
{
    static int      configured = 0;
    static char     myhostname[STRLEN];
    static char     myusername[20];
    struct hostent *hbuf;
    struct passwd  *pbuf;
    char           *ptr;
    char            genbuf[256];
    FILE           *fin, *fout;

    /* ¤¤³~ÄdºI */
    if ((ptr = strchr(receiver, ';'))) {
	struct tm      *ptime;

	*ptr = '\0';
    }
    if ((ptr = strstr(receiver, str_mail_address)) || !strchr(receiver, '@')) {
	char            hacker[20];
	int             len;

	if (strchr(receiver, '@')) {
	    len = ptr - receiver;
	    memcpy(hacker, receiver, len);
	    hacker[len] = '\0';
	} else
	    strcpy(hacker, receiver);
	return send_inner_mail(fpath, title, hacker);
    }
    /* setup the hostname and username */
    if (!configured) {
	/* get host name */
	hbuf = gethostbyname("localhost");
	if (hbuf)
	    strncpy(myhostname, hbuf->h_name, STRLEN);

	/* get bbs uident */
	pbuf = getpwuid(getuid());
	if (pbuf)
	    strncpy(myusername, pbuf->pw_name, 20);
	if (hbuf && pbuf)
	    configured = 1;
	else
	    return -1;
    }
    /* Running the sendmail */
    if (fpath == NULL) {
	sprintf(genbuf, "/usr/sbin/sendmail %s > /dev/null", receiver);
	fin = fopen("etc/confirm", "r");
    } else {
	sprintf(genbuf, "/usr/sbin/sendmail -f %s%s %s > /dev/null",
		cuser.userid, str_mail_address, receiver);
	fin = fopen(fpath, "r");
    }
    fout = popen(genbuf, "w");
    if (fin == NULL || fout == NULL)
	return -1;

    if (fpath)
	fprintf(fout, "Reply-To: %s%s\nFrom: %s%s\n",
		cuser.userid, str_mail_address, cuser.userid,
		str_mail_address);
    fprintf(fout, "To: %s\nSubject: %s\n", receiver, title);
    fprintf(fout, "X-Disclaimer: " BBSNAME "¹ï¥»«H¤º®e®¤¤£­t³d¡C\n\n");

    while (fgets(genbuf, 255, fin)) {
	if (genbuf[0] == '.' && genbuf[1] == '\n')
	    fputs(". \n", fout);
	else
	    fputs(genbuf, fout);
    }
    fclose(fin);
    fprintf(fout, ".\n");
    pclose(fout);
    return 0;
}
#else				/* USE_BSMTP */

int 
bsmtp(char *fpath, char *title, char *rcpt, int method)
{
    char            buf[80], *ptr;
    time_t          chrono;
    MailQueue       mqueue;

    /* check if the mail is a inner mail */
    if ((ptr = strstr(rcpt, str_mail_address)) || !strchr(rcpt, '@')) {
	char            hacker[20];
	int             len;

	if (strchr(rcpt, '@')) {
	    len = ptr - rcpt;
	    memcpy(hacker, rcpt, len);
	    hacker[len] = '\0';
	} else
	    strcpy(hacker, rcpt);
	return send_inner_mail(fpath, title, hacker);
    }
    chrono = now;
    if (method != MQ_JUSTIFY) {	/* »{ÃÒ«H */
	/* stamp the queue file */
	strcpy(buf, "out/");
	for (;;) {
	    sprintf(buf + 4, "M.%ld.A", ++chrono);
	    if (!dashf(buf)) {
		Link(fpath, buf);
		break;
	    }
	}

	fpath = buf;

	strcpy(mqueue.filepath, fpath);
	strcpy(mqueue.subject, title);
    }
    /* setup mail queue */
    mqueue.mailtime = chrono;
    mqueue.method = method;
    strcpy(mqueue.sender, cuser.userid);
    strcpy(mqueue.username, cuser.username);
    strcpy(mqueue.rcpt, rcpt);
    if (do_append("out/.DIR", (fileheader_t *) & mqueue, sizeof(mqueue)) < 0)
	return 0;
    return chrono;
}
#endif				/* USE_BSMTP */

int 
doforward(char *direct, fileheader_t * fh, int mode)
{
    static char     address[60];
    char            fname[500];
    int             return_no;
    char            genbuf[200];

    if (!address[0])
	strcpy(address, cuser.email);

    if (address[0]) {
	sprintf(genbuf, "½T©wÂà±Hµ¹ [%s] ¶Ü(Y/N/Q)¡H[Y] ", address);
	getdata(b_lines - 1, 0, genbuf, fname, 3, LCECHO);

	if (fname[0] == 'q') {
	    outmsg("¨ú®øÂà±H");
	    return 1;
	}
	if (fname[0] == 'n')
	    address[0] = '\0';
    }
    if (!address[0]) {
	do {
	    getdata(b_lines - 1, 0, "½Ð¿é¤JÂà±H¦a§}¡G", fname, 60, DOECHO);
	    if (fname[0]) {
		if (strchr(fname, '.'))
		    strcpy(address, fname);
		else
		    sprintf(address, "%s.bbs@%s", fname, MYHOSTNAME);
	    } else {
		outmsg("¨ú®øÂà±H");
		return 1;
	    }
	} while (mode == 'Z' && strstr(address, MYHOSTNAME));
    }
    if (invalidaddr(address))
	return -2;

    sprintf(fname, "¥¿Âà±Hµ¹ %s, ½Ðµy­Ô...", address);
    outmsg(fname);
    move(b_lines - 1, 0);
    refresh();

    /* °lÂÜ¨Ï¥ÎªÌ */
    if (HAS_PERM(PERM_LOGUSER)) {
	char            msg[200];

	sprintf(msg, "%s mailforward to %s at %s",
		cuser.userid, address, Cdate(&now));
	log_user(msg);
    }
    if (mode == 'Z') {
	sprintf(fname, TAR_PATH " cfz /tmp/home.%s.tgz home/%c/%s; "
	  MUTT_PATH " -a /tmp/home.%s.tgz -s 'home.%s.tgz' '%s' </dev/null;"
		"rm /tmp/home.%s.tgz",
		cuser.userid, cuser.userid[0], cuser.userid,
		cuser.userid, cuser.userid, address, cuser.userid);
	system(fname);
	return 0;
	sprintf(fname, TAR_PATH " cfz - home/%c/%s | "
		"/usr/bin/uuencode %s.tgz > %s",
		cuser.userid[0], cuser.userid, cuser.userid, direct);
	system(fname);
	strcpy(fname, direct);
    } else if (mode == 'U') {
	char            tmp_buf[128];

	sprintf(fname, "/tmp/bbs.uu%05d", currpid);
	sprintf(tmp_buf, "/usr/bin/uuencode %s/%s uu.%05d > %s",
		direct, fh->filename, currpid, fname);
	system(tmp_buf);
    } else if (mode == 'F') {
	char            tmp_buf[128];

	sprintf(fname, "/tmp/bbs.f%05d", currpid);
	sprintf(tmp_buf, "cp %s/%s %s", direct, fh->filename, fname);
	system(tmp_buf);
    } else
	return -1;

    return_no =
#ifndef USE_BSMTP
	bbs_sendmail(fname, fh->title, address);
#else
	bsmtp(fname, fh->title, address, mode);
#endif
    unlink(fname);
    return (return_no);
}

int 
load_mailalert(char *userid)
{
    struct stat     st;
    char            maildir[256];
    int             fd;
    register int    numfiles;
    fileheader_t    my_mail;

    sethomedir(maildir, userid);
    if (!HAS_PERM(PERM_BASIC))
	return 0;
    if (stat(maildir, &st) < 0)
	return 0;
    numfiles = st.st_size / sizeof(fileheader_t);
    if (numfiles <= 0)
	return 0;

    /* ¬Ý¬Ý¦³¨S¦³«H¥óÁÙ¨SÅª¹L¡H±qÀÉ§À¦^ÀYÀË¬d¡A®Ä²v¸û°ª */
    if ((fd = open(maildir, O_RDONLY)) > 0) {
	lseek(fd, st.st_size - sizeof(fileheader_t), SEEK_SET);
	while (numfiles--) {
	    read(fd, &my_mail, sizeof(fileheader_t));
	    if (!(my_mail.filemode & FILE_READ)) {
		close(fd);
		return 1;
	    }
	    lseek(fd, -(off_t) 2 * sizeof(fileheader_t), SEEK_CUR);
	}
	close(fd);
    }
    return 0;
}

#ifdef  EMAIL_JUSTIFY
static void 
mail_justify(userec_t muser)
{
    fileheader_t    mhdr;
    char            title[128], buf1[80];
    FILE           *fp;

    sethomepath(buf1, muser.userid);
    stampfile(buf1, &mhdr);
    unlink(buf1);
    strcpy(mhdr.owner, cuser.userid);
    strncpy(mhdr.title, "[¼f®Ö³q¹L]", TTLEN);
    mhdr.filemode = 0;

    if (valid_ident(muser.email) && !invalidaddr(muser.email)) {
	char            title[80], *ptr;
	unsigned short  checksum;	/* 16-bit is enough */
	char            ch;

	checksum = searchuser(muser.userid);
	ptr = muser.email;
	while ((ch = *ptr++)) {
	    if (ch <= ' ')
		break;
	    if (ch >= 'A' && ch <= 'Z')
		ch |= 0x20;
	    checksum = (checksum << 1) ^ ch;
	}

	sprintf(title, "[PTT BBS]To %s(%d:%d) [User Justify]",
		muser.userid, getuser(muser.userid) + MAGIC_KEY, checksum);
	if (
#ifndef USE_BSMTP
	    bbs_sendmail(NULL, title, muser.email)
#else
	    bsmtp(NULL, title, muser.email, MQ_JUSTIFY);
#endif
	<0)
	    Link("etc/bademail", buf1);
	else
	Link("etc/replyemail", buf1);
    } else
	Link("etc/bademail", buf1);
    sethomedir(title, muser.userid);
    append_record(title, &mhdr, sizeof(mhdr));
}
#endif				/* EMAIL_JUSTIFY */
