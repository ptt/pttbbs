/* $Id$ */
#define _XOPEN_SOURCE
#define _ISOC99_SOURCE
 
#include "bbs.h"

static char    *sex[8] = {
    MSG_BIG_BOY, MSG_BIG_GIRL, MSG_LITTLE_BOY, MSG_LITTLE_GIRL,
    MSG_MAN, MSG_WOMAN, MSG_PLANT, MSG_MIME
};
int
kill_user(int num)
{
  userec_t u;
  memset(&u, 0, sizeof(u));
  log_usies("KILL", getuserid(num));
  setuserid(num, "");
  passwd_update(num, &u);
  return 0;
}
int
u_loginview()
{
    int             i;
    unsigned int    pbits = cuser->loginview;
    char            choice[5];

    clear();
    move(4, 0);
    for (i = 0; i < NUMVIEWFILE; i++)
	prints("    %c. %-20s %-15s \n", 'A' + i,
	       loginview_file[i][1], ((pbits >> i) & 1 ? "£¾" : "¢æ"));

    clrtobot();
    while (getdata(b_lines - 1, 0, "½Ð«ö [A-N] ¤Á´«³]©w¡A«ö [Return] µ²§ô¡G",
		   choice, sizeof(choice), LCECHO)) {
	i = choice[0] - 'a';
	if (i >= NUMVIEWFILE || i < 0)
	    bell();
	else {
	    pbits ^= (1 << i);
	    move(i + 4, 28);
	    prints((pbits >> i) & 1 ? "£¾" : "¢æ");
	}
    }

    if (pbits != cuser->loginview) {
	cuser->loginview = pbits;
	passwd_update(usernum, cuser);
    }
    return 0;
}

void
user_display(userec_t * u, int real)
{
    int             diff = 0;
    char            genbuf[200];

    clrtobot();
    prints(
	   "        \033[30;41m¢r¢s¢r¢s¢r¢s\033[m  \033[1;30;45m    ¨Ï ¥Î ªÌ"
	   " ¸ê ®Æ        "
	   "     \033[m  \033[30;41m¢r¢s¢r¢s¢r¢s\033[m\n");
    prints("                ¥N¸¹¼ÊºÙ: %s(%s)\n"
	   "                ¯u¹ê©m¦W: %s"
#ifdef FOREIGN_REG
	   " %s%s"
#endif
	   "\n"
	   "                ©~¦í¦í§}: %s\n"
	   "                ¹q¤l«H½c: %s\n"
	   "                ©Ê    §O: %s\n"
	   "                »È¦æ±b¤á: %d »È¨â\n",
	   u->userid, u->username, u->realname,
#ifdef FOREIGN_REG
	   u->uflag2 & FOREIGN ? "(¥~Äy: " : "",
	   u->uflag2 & FOREIGN ?
		(u->uflag2 & LIVERIGHT) ? "¥Ã¤[©~¯d)" : "¥¼¨ú±o©~¯dÅv)"
		: "",
#endif
	   u->address, u->email,
	   sex[u->sex % 8], u->money);

    sethomedir(genbuf, u->userid);
    prints("                ¨p¤H«H½c: %d «Ê  (ÁÊ¶R«H½c: %d «Ê)\n"
	   "                ¤â¾÷¸¹½X: %010d\n"
	   "                ¥Í    ¤é: %02i/%02i/%02i\n"
	   "                ¤pÂû¦W¦r: %s\n",
	   get_num_records(genbuf, sizeof(fileheader_t)),
	   u->exmailbox, u->mobile,
	   u->month, u->day, u->year % 100, u->mychicken.name);
    prints("                µù¥U¤é´Á: %s", ctime(&u->firstlogin));
    prints("                «e¦¸¥úÁ{: %s", ctime(&u->lastlogin));
    prints("                «e¦¸ÂIºq: %s", ctime(&u->lastsong));
    prints("                ¤W¯¸¤å³¹: %d ¦¸ / %d ½g\n",
	   u->numlogins, u->numposts);

    if (real) {
	strlcpy(genbuf, "bTCPRp#@XWBA#VSM0123456789ABCDEF", sizeof(genbuf));
	for (diff = 0; diff < 32; diff++)
	    if (!(u->userlevel & (1 << diff)))
		genbuf[diff] = '-';
	prints("                »{ÃÒ¸ê®Æ: %s\n"
	       "                userÅv­­: %s\n",
	       u->justify, genbuf);
    } else {
	diff = (now - login_start_time) / 60;
	prints("                °±¯d´Á¶¡: %d ¤p®É %2d ¤À\n",
	       diff / 60, diff % 60);
    }

    /* Thor: ·Q¬Ý¬Ý³o­Ó user ¬O¨º¨ÇªOªºªO¥D */
    if (u->userlevel >= PERM_BM) {
	int             i;
	boardheader_t  *bhdr;

	outs("                ¾á¥ôªO¥D: ");

	for (i = 0, bhdr = bcache; i < numboards; i++, bhdr++) {
	    if (is_uBM(bhdr->BM, u->userid)) {
		outs(bhdr->brdname);
		outc(' ');
	    }
	}
	outc('\n');
    }
    outs("        \033[30;41m¢r¢s¢r¢s¢r¢s¢r¢s¢r¢s¢r¢s¢r¢s¢r¢s¢r¢s¢r¢s¢r¢s¢r"
	 "¢s¢r¢s¢r¢s¢r¢s\033[m");

    outs((u->userlevel & PERM_LOGINOK) ?
	 "\n±zªºµù¥Uµ{§Ç¤w¸g§¹¦¨¡AÅwªï¥[¤J¥»¯¸" :
	 "\n¦pªG­n´£ª@Åv­­¡A½Ð°Ñ¦Ò¥»¯¸¤½§GÄæ¿ì²zµù¥U");

#ifdef NEWUSER_LIMIT
    if ((u->lastlogin - u->firstlogin < 3 * 86400) && !HAS_PERM(PERM_POST))
	outs("\n·s¤â¤W¸ô¡A¤T¤Ñ«á¶}©ñÅv­­");
#endif
}

void
mail_violatelaw(char *crime, char *police, char *reason, char *result)
{
    char            genbuf[200];
    fileheader_t    fhdr;
    FILE           *fp;
    snprintf(genbuf, sizeof(genbuf), "home/%c/%s", crime[0], crime);
    stampfile(genbuf, &fhdr);
    if (!(fp = fopen(genbuf, "w")))
	return;
    fprintf(fp, "§@ªÌ: [Pttªk°|]\n"
	    "¼ÐÃD: [³ø§i] ¹Hªk§P¨M³ø§i\n"
	    "®É¶¡: %s\n"
	    "\033[1;32m%s\033[m§P¨M¡G\n     \033[1;32m%s\033[m"
	    "¦]\033[1;35m%s\033[m¦æ¬°¡A\n¹H¤Ï¥»¯¸¯¸³W¡A³B¥H\033[1;35m%s\033[m¡A¯S¦¹³qª¾"
	"\n½Ð¨ì PttLaw ¬d¸ß¬ÛÃöªk³W¸ê°T¡A¨Ã¨ì Play-Pay-ViolateLaw Ãº¥æ»@³æ",
	    ctime(&now), police, crime, reason, result);
    fclose(fp);
    snprintf(fhdr.title, sizeof(fhdr.title), "[³ø§i] ¹Hªk§P¨M³ø§i");
    strlcpy(fhdr.owner, "[Pttªk°|]", sizeof(fhdr.owner));
    snprintf(genbuf, sizeof(genbuf), "home/%c/%s/.DIR", crime[0], crime);
    append_record(genbuf, &fhdr, sizeof(fhdr));
}

static void
violate_law(userec_t * u, int unum)
{
    char            ans[4], ans2[4];
    char            reason[128];
    move(1, 0);
    clrtobot();
    move(2, 0);
    prints("(1)Cross-post (2)¶Ãµo¼s§i«H (3)¶Ãµo³sÂê«H\n");
    prints("(4)ÄÌÂZ¯¸¤W¨Ï¥ÎªÌ (8)¨ä¥L¥H»@³æ³B¸m¦æ¬°\n(9)¬å id ¦æ¬°\n");
    getdata(5, 0, "(0)µ²§ô", ans, sizeof(ans), DOECHO);
    switch (ans[0]) {
    case '1':
	snprintf(reason, sizeof(reason), "%s", "Cross-post");
	break;
    case '2':
	snprintf(reason, sizeof(reason), "%s", "¶Ãµo¼s§i«H");
	break;
    case '3':
	snprintf(reason, sizeof(reason), "%s", "¶Ãµo³sÂê«H");
	break;
    case '4':
	while (!getdata(7, 0, "½Ð¿é¤J³QÀËÁ|²z¥Ñ¥H¥Ü­t³d¡G", reason, 50, DOECHO));
	strcat(reason, "[ÄÌÂZ¯¸¤W¨Ï¥ÎªÌ]");
	break;
    case '8':
    case '9':
	while (!getdata(6, 0, "½Ð¿é¤J²z¥Ñ¥H¥Ü­t³d¡G", reason, 50, DOECHO));
	break;
    default:
	return;
    }
    getdata(7, 0, msg_sure_ny, ans2, sizeof(ans2), LCECHO);
    if (*ans2 != 'y')
	return;
    if (ans[0] == '9') {
	char            src[STRLEN], dst[STRLEN];
	snprintf(src, sizeof(src), "home/%c/%s", u->userid[0], u->userid);
	snprintf(dst, sizeof(dst), "tmp/%s", u->userid);
	Rename(src, dst);
	post_violatelaw(u->userid, cuser->userid, reason, "¬å°£ ID");
        kill_user(unum);

    } else {
	u->userlevel |= PERM_VIOLATELAW;
	u->vl_count++;
	passwd_update(unum, u);
	post_violatelaw(u->userid, cuser->userid, reason, "»@³æ³B¥÷");
	mail_violatelaw(u->userid, cuser->userid, reason, "»@³æ³B¥÷");
    }
    pressanykey();
}

static void Customize(void)
{
    char    ans[4], done = 0, mindbuf[5];
    char    *wm[3] = {"¤@¯ë", "¶i¶¥", "¥¼¨Ó"};

    showtitle("­Ó¤H¤Æ³]©w", "­Ó¤H¤Æ³]©w");
    memcpy(mindbuf, &currutmp->mind, 4);
    mindbuf[4] = 0;
    while( !done ){
	move(2, 0);
	prints("±z¥Ø«eªº­Ó¤H¤Æ³]©w: ");
	move(4, 0);
	prints("%-30s%10s\n", "A. ¤ô²y¼Ò¦¡",
	       wm[(cuser->uflag2 & WATER_MASK)]);
	prints("%-30s%10s\n", "B. ±µ¨ü¯¸¥~«H",
	       ((cuser->userlevel & PERM_NOOUTMAIL) ? "§_" : "¬O"));
	prints("%-30s%10s\n", "C. ·sªO¦Û°Ê¶i§Úªº³Ì·R",
	       ((cuser->uflag2 & FAVNEW_FLAG) ? "¬O" : "§_"));
	prints("%-30s%10s\n", "D. ¥Ø«eªº¤ß±¡", mindbuf);
	prints("%-30s%10s\n", "E. °ª«G«×Åã¥Ü§Úªº³Ì·R", 
	       ((cuser->uflag2 & FAVNOHILIGHT) ? "§_" : "¬O"));
	getdata(b_lines - 1, 0, "½Ð«ö [A-E] ¤Á´«³]©w¡A«ö [Return] µ²§ô¡G",
		ans, sizeof(ans), DOECHO);

	switch( ans[0] ){
	case 'A':
	case 'a':{
	    int     currentset = cuser->uflag2 & WATER_MASK;
	    currentset = (currentset + 1) % 3;
	    cuser->uflag2 &= ~WATER_MASK;
	    cuser->uflag2 |= currentset;
	    vmsg("­×¥¿¤ô²y¼Ò¦¡«á½Ð¥¿±`Â÷½u¦A­«·s¤W½u");
	}
	    break;
	case 'B':
	case 'b':
	    cuser->userlevel ^= PERM_NOOUTMAIL;
	    break;
	case 'C':
	case 'c':
	    cuser->uflag2 ^= FAVNEW_FLAG;
	    if (cuser->uflag2 & FAVNEW_FLAG)
		subscribe_newfav();
	    break;
	case 'D':
	case 'd':{
	    getdata(b_lines - 1, 0, "²{¦bªº¤ß±¡? ",
		    mindbuf, sizeof(mindbuf), DOECHO);
	    if (strcmp(mindbuf, "³q½r") == 0)
		vmsg("¤£¥i¥H§â¦Û¤v³]³q½r°Õ!");
	    else if (strcmp(mindbuf, "¹Ø¬P") == 0)
		vmsg("§A¤£¬O¤µ¤Ñ¥Í¤éÕÙ!");
	    else
		memcpy(currutmp->mind, mindbuf, 4);
	}
	    break;
	case 'E':
	case 'e':
	    cuser->uflag2 ^= FAVNOHILIGHT;
	    break;
	default:
	    done = 1;
	}
	passwd_update(usernum, cuser);
    }
    pressanykey();
}

void
uinfo_query(userec_t * u, int real, int unum)
{
    userec_t        x;
    register int    i = 0, fail, mail_changed;
    int             uid;
    char            ans[4], buf[STRLEN], *p;
    char            genbuf[200], reason[50];
    int money = 0;
    fileheader_t    fhdr;
    int             flag = 0, temp = 0, money_change = 0;

    FILE           *fp;

    fail = mail_changed = 0;

    memcpy(&x, u, sizeof(userec_t));
    getdata(b_lines - 1, 0, real ?
	    "(1)§ï¸ê®Æ(2)³]±K½X(3)³]Åv­­(4)¬å±b¸¹(5)§ïID"
	    "(6)±þ/´_¬¡Ãdª«(7)¼f§P [0]µ²§ô " :
	    "½Ð¿ï¾Ü (1)­×§ï¸ê®Æ (2)³]©w±K½X (C) ­Ó¤H¤Æ³]©w ==> [0]µ²§ô ",
	    ans, sizeof(ans), DOECHO);

    if (ans[0] > '2' && ans[0] != 'C' && ans[0] != 'c' && !real)
	ans[0] = '0';

    if (ans[0] == '1' || ans[0] == '3') {
	clear();
	i = 1;
	move(i++, 0);
	outs(msg_uid);
	outs(x.userid);
    }
    switch (ans[0]) {
    case 'C':
    case 'c':
	Customize();
	return;
    case '7':
	violate_law(&x, unum);
	return;
    case '1':
	move(0, 0);
	outs("½Ð³v¶µ­×§ï¡C");

	getdata_buf(i++, 0, " ¼Ê ºÙ  ¡G", x.username,
		    sizeof(x.username), DOECHO);
	if (real) {
	    getdata_buf(i++, 0, "¯u¹ê©m¦W¡G",
			x.realname, sizeof(x.realname), DOECHO);
#ifdef FOREIGN_REG
	    getdata_buf(i++, 0, cuser->uflag2 & FOREIGN ? "Å@·Ó¸¹½X" : "¨­¤ÀÃÒ¸¹¡G", x.ident, sizeof(x.ident), DOECHO);
#else
	    getdata_buf(i++, 0, "¨­¤ÀÃÒ¸¹¡G", x.ident, sizeof(x.ident), DOECHO);
#endif
	    getdata_buf(i++, 0, "©~¦í¦a§}¡G",
			x.address, sizeof(x.address), DOECHO);
	}
	snprintf(buf, sizeof(buf), "%010d", x.mobile);
	getdata_buf(i++, 0, "¤â¾÷¸¹½X¡G", buf, 11, LCECHO);
	x.mobile = atoi(buf);
	getdata_str(i++, 0, "¹q¤l«H½c[ÅÜ°Ê­n­«·s»{ÃÒ]¡G", buf, 50, DOECHO,
		    x.email);
	if (strcmp(buf, x.email) && strchr(buf, '@')) {
	    strlcpy(x.email, buf, sizeof(x.email));
	    mail_changed = 1 - real;
	}
	snprintf(genbuf, sizeof(genbuf), "%i", (u->sex + 1) % 8);
	getdata_str(i++, 0, "©Ê§O (1)¸¯®æ (2)©j±µ (3)©³­} (4)¬ü¬Ü (5)Á¦¨û "
		    "(6)ªü«¼ (7)´Óª« (8)Äqª«¡G",
		    buf, 3, DOECHO, genbuf);
	if (buf[0] >= '1' && buf[0] <= '8')
	    x.sex = (buf[0] - '1') % 8;
	else
	    x.sex = u->sex % 8;

	while (1) {
	    int             len;

	    snprintf(genbuf, sizeof(genbuf), "%02i/%02i/%02i",
		     u->month, u->day, u->year % 100);
	    len = getdata_str(i, 0, "¥Í¤é ¤ë¤ë/¤é¤é/¦è¤¸¡G", buf, 9,
			      DOECHO, genbuf);
	    if (len && len != 8)
		continue;
	    if (!len) {
		x.month = u->month;
		x.day = u->day;
		x.year = u->year;
	    } else if (len == 8) {
		x.month = (buf[0] - '0') * 10 + (buf[1] - '0');
		x.day = (buf[3] - '0') * 10 + (buf[4] - '0');
		x.year = (buf[6] - '0') * 10 + (buf[7] - '0');
	    } else
		continue;
	    if (!real && (x.month > 12 || x.month < 1 || x.day > 31 ||
			  x.day < 1 || x.year > 90 || x.year < 40))
		continue;
	    i++;
	    break;
	}
	if (real) {
	    int l;
	    if (HAS_PERM(PERM_BBSADM)) {
		snprintf(genbuf, sizeof(genbuf), "%d", x.money);
		if (getdata_str(i++, 0, "»È¦æ±b¤á¡G", buf, 10, DOECHO, genbuf))
		    if ((l = atol(buf)) != 0) {
			if (l != x.money) {
			    money_change = 1;
			    money = x.money;
			    x.money = l;
			}
		    }
	    }
	    snprintf(genbuf, sizeof(genbuf), "%d", x.exmailbox);
	    if (getdata_str(i++, 0, "ÁÊ¶R«H½c¼Æ¡G", buf, 6,
			    DOECHO, genbuf))
		if ((l = atol(buf)) != 0)
		    x.exmailbox = (int)l;

	    getdata_buf(i++, 0, "»{ÃÒ¸ê®Æ¡G", x.justify,
			sizeof(x.justify), DOECHO);
	    getdata_buf(i++, 0, "³Ìªñ¥úÁ{¾÷¾¹¡G",
			x.lasthost, sizeof(x.lasthost), DOECHO);

	    snprintf(genbuf, sizeof(genbuf), "%d", x.numlogins);
	    if (getdata_str(i++, 0, "¤W½u¦¸¼Æ¡G", buf, 10, DOECHO, genbuf))
		if ((fail = atoi(buf)) >= 0)
		    x.numlogins = fail;
	    snprintf(genbuf, sizeof(genbuf), "%d", u->numposts);
	    if (getdata_str(i++, 0, "¤å³¹¼Æ¥Ø¡G", buf, 10, DOECHO, genbuf))
		if ((fail = atoi(buf)) >= 0)
		    x.numposts = fail;
	    snprintf(genbuf, sizeof(genbuf), "%d", u->goodpost);
	    if (getdata_str(i++, 0, "Àu¨}¤å³¹¼Æ:", buf, 10, DOECHO, genbuf))
		if ((fail = atoi(buf)) >= 0)
		    x.goodpost = fail;
	    snprintf(genbuf, sizeof(genbuf), "%d", u->badpost);
	    if (getdata_str(i++, 0, "´c¦H¤å³¹¼Æ:", buf, 10, DOECHO, genbuf))
		if ((fail = atoi(buf)) >= 0)
		    x.badpost = fail;
	    snprintf(genbuf, sizeof(genbuf), "%d", u->vl_count);
	    if (getdata_str(i++, 0, "¹Hªk°O¿ý¡G", buf, 10, DOECHO, genbuf))
		if ((fail = atoi(buf)) >= 0)
		    x.vl_count = fail;

	    snprintf(genbuf, sizeof(genbuf),
		     "%d/%d/%d", u->five_win, u->five_lose, u->five_tie);
	    if (getdata_str(i++, 0, "¤­¤l´Ñ¾ÔÁZ ³Ó/±Ñ/©M¡G", buf, 16, DOECHO,
			    genbuf))
		while (1) {
		    p = strtok(buf, "/\r\n");
		    if (!p)
			break;
		    x.five_win = atoi(p);
		    p = strtok(NULL, "/\r\n");
		    if (!p)
			break;
		    x.five_lose = atoi(p);
		    p = strtok(NULL, "/\r\n");
		    if (!p)
			break;
		    x.five_tie = atoi(p);
		    break;
		}
	    snprintf(genbuf, sizeof(genbuf),
		     "%d/%d/%d", u->chc_win, u->chc_lose, u->chc_tie);
	    if (getdata_str(i++, 0, "¶H´Ñ¾ÔÁZ ³Ó/±Ñ/©M¡G", buf, 16, DOECHO,
			    genbuf))
		while (1) {
		    p = strtok(buf, "/\r\n");
		    if (!p)
			break;
		    x.chc_win = atoi(p);
		    p = strtok(NULL, "/\r\n");
		    if (!p)
			break;
		    x.chc_lose = atoi(p);
		    p = strtok(NULL, "/\r\n");
		    if (!p)
			break;
		    x.chc_tie = atoi(p);
		    break;
		}
#ifdef FOREIGN_REG
	    if (getdata_str(i++, 0, "©~¥Á 1)¥xÆW 2)¨ä¥L¡G", buf, 2, DOECHO, x.uflag2 & FOREIGN ? "2" : "1"))
		if ((fail = atoi(buf)) > 0){
		    if (fail == 2){
			x.uflag2 |= FOREIGN;
		    }
		    else
			x.uflag2 &= ~FOREIGN;
		}
	    if (x.uflag2 & FOREIGN)
		if (getdata_str(i++, 0, "¥Ã¤[©~¯dÅv 1)¬O 2)§_¡G", buf, 2, DOECHO, x.uflag2 & LIVERIGHT ? "1" : "2")){
		    if ((fail = atoi(buf)) > 0){
			if (fail == 1){
			    x.uflag2 |= LIVERIGHT;
			    x.userlevel |= (PERM_LOGINOK | PERM_POST);
			}
			else{
			    x.uflag2 &= ~LIVERIGHT;
			    x.userlevel &= ~(PERM_LOGINOK | PERM_POST);
			}
		    }
		}
#endif
	    fail = 0;
	}
	break;

    case '2':
	i = 19;
	if (!real) {
	    if (!getdata(i++, 0, "½Ð¿é¤J­ì±K½X¡G", buf, PASSLEN, NOECHO) ||
		!checkpasswd(u->passwd, buf)) {
		outs("\n\n±z¿é¤Jªº±K½X¤£¥¿½T\n");
		fail++;
		break;
	    }
	} else {
	    char            witness[3][32];
	    for (i = 0; i < 3; i++) {
		if (!getdata(19 + i, 0, "½Ð¿é¤J¨ó§UÃÒ©ú¤§¨Ï¥ÎªÌ¡G",
			     witness[i], sizeof(witness[i]), DOECHO)) {
		    outs("\n¤£¿é¤J«hµLªk§ó§ï\n");
		    fail++;
		    break;
		} else if (!(uid = getuser(witness[i]))) {
		    outs("\n¬dµL¦¹¨Ï¥ÎªÌ\n");
		    fail++;
		    break;
		} else {
		    userec_t        atuser;
		    passwd_query(uid, &atuser);
		    if (now - atuser.firstlogin < 6 * 30 * 24 * 60 * 60) {
			outs("\nµù¥U¥¼¶W¹L¥b¦~¡A½Ð­«·s¿é¤J\n");
			i--;
		    }
		}
	    }
	    if (i < 3)
		break;
	    else
		i = 20;
	}

	if (!getdata(i++, 0, "½Ð³]©w·s±K½X¡G", buf, PASSLEN, NOECHO)) {
	    outs("\n\n±K½X³]©w¨ú®ø, Ä~Äò¨Ï¥ÎÂÂ±K½X\n");
	    fail++;
	    break;
	}
	strncpy(genbuf, buf, PASSLEN);

	getdata(i++, 0, "½ÐÀË¬d·s±K½X¡G", buf, PASSLEN, NOECHO);
	if (strncmp(buf, genbuf, PASSLEN)) {
	    outs("\n\n·s±K½X½T»{¥¢±Ñ, µLªk³]©w·s±K½X\n");
	    fail++;
	    break;
	}
	buf[8] = '\0';
	strncpy(x.passwd, genpasswd(buf), PASSLEN);
	if (real)
	    x.userlevel &= (!PERM_LOGINOK);
	break;

    case '3':
	i = setperms(x.userlevel, str_permid);
	if (i == x.userlevel)
	    fail++;
	else {
	    flag = 1;
	    temp = x.userlevel;
	    x.userlevel = i;
	}
	break;

    case '4':
	i = QUIT;
	break;

    case '5':
	if (getdata_str(b_lines - 3, 0, "·sªº¨Ï¥ÎªÌ¥N¸¹¡G", genbuf, IDLEN + 1,
			DOECHO, x.userid)) {
	    if (searchuser(genbuf)) {
		outs("¿ù»~! ¤w¸g¦³¦P¼Ë ID ªº¨Ï¥ÎªÌ");
		fail++;
	    } else
		strlcpy(x.userid, genbuf, sizeof(x.userid));
	}
	break;
    case '6':
	if (x.mychicken.name[0])
	    x.mychicken.name[0] = 0;
	else
	    strlcpy(x.mychicken.name, "[¦º]", sizeof(x.mychicken.name));
	break;
    default:
	return;
    }

    if (fail) {
	pressanykey();
	return;
    }
    getdata(b_lines - 1, 0, msg_sure_ny, ans, 3, LCECHO);
    if (*ans == 'y') {
	if (flag)
	    post_change_perm(temp, i, cuser->userid, x.userid);
	if (strcmp(u->userid, x.userid)) {
	    char            src[STRLEN], dst[STRLEN];

	    sethomepath(src, u->userid);
	    sethomepath(dst, x.userid);
	    Rename(src, dst);
	    setuserid(unum, x.userid);
	}
	memcpy(u, &x, sizeof(x));
	if (mail_changed) {
#ifdef EMAIL_JUSTIFY
	    x.userlevel &= ~PERM_LOGINOK;
	    mail_justify();
#endif
	}
	if (i == QUIT) {
	    char            src[STRLEN], dst[STRLEN];

	    snprintf(src, sizeof(src), "home/%c/%s", x.userid[0], x.userid);
	    snprintf(dst, sizeof(dst), "tmp/%s", x.userid);
	    Rename(src, dst);	/* do not remove user home */
            kill_user(unum);
	    return;
	} else
	    log_usies("SetUser", x.userid);
	if (money_change)
	    setumoney(unum, x.money);
	passwd_update(unum, &x);
	if (money_change) {
	    strlcpy(genbuf, "boards/S/Security", sizeof(genbuf));
	    stampfile(genbuf, &fhdr);
	    if (!(fp = fopen(genbuf, "w")))
		return;

	    fprintf(fp, "§@ªÌ: [¨t²Î¦w¥þ§½] ¬ÝªO: Security\n"
		    "¼ÐÃD: [¤½¦w³ø§i] ¯¸ªø­×§ïª÷¿ú³ø§i\n"
		    "®É¶¡: %s\n"
		    "   ¯¸ªø\033[1;32m%s\033[m§â\033[1;32m%s\033[m"
		    "ªº¿ú±q\033[1;35m%d\033[m§ï¦¨\033[1;35m%d\033[m",
		    ctime(&now), cuser->userid, x.userid, money, x.money);

	    clrtobot();
	    clear();
	    while (!getdata(5, 0, "½Ð¿é¤J²z¥Ñ¥H¥Ü­t³d¡G",
			    reason, sizeof(reason), DOECHO));

	    fprintf(fp, "\n   \033[1;37m¯¸ªø%s­×§ï¿ú²z¥Ñ¬O¡G%s\033[m",
		    cuser->userid, reason);
	    fclose(fp);
	    snprintf(fhdr.title, sizeof(fhdr.title),
		     "[¤½¦w³ø§i] ¯¸ªø%s­×§ï%s¿ú³ø§i", cuser->userid,
		     x.userid);
	    strlcpy(fhdr.owner, "[¨t²Î¦w¥þ§½]", sizeof(fhdr.owner));
	    append_record("boards/S/Security/.DIR", &fhdr, sizeof(fhdr));
	}
    }
}

int
u_info()
{
    move(2, 0);
    user_display(cuser, 0);
    uinfo_query(cuser, 0, usernum);
    strlcpy(currutmp->username, cuser->username, sizeof(currutmp->username));
    return 0;
}

int
u_ansi()
{
    showansi ^= 1;
    cuser->uflag ^= COLOR_FLAG;
    outs(reset_color);
    return 0;
}

int
u_cloak()
{
    outs((currutmp->invisible ^= 1) ? MSG_CLOAKED : MSG_UNCLOAK);
    return XEASY;
}

int
u_switchproverb()
{
    /* char *state[4]={"¥Î¥\\«¬","¦w¶h«¬","¦Û©w«¬","SHUTUP"}; */
    char            buf[100];

    cuser->proverb = (cuser->proverb + 1) % 4;
    setuserfile(buf, fn_proverb);
    if (cuser->proverb == 2 && dashd(buf)) {
	FILE           *fp = fopen(buf, "a");
	assert(fp);

	fprintf(fp, "®y¥k»Êª¬ºA¬°[¦Û©w«¬]­n°O±o³]®y¥k»Êªº¤º®e­ò!!");
	fclose(fp);
    }
    passwd_update(usernum, cuser);
    return 0;
}

int
u_editproverb()
{
    char            buf[100];

    setutmpmode(PROVERB);
    setuserfile(buf, fn_proverb);
    move(1, 0);
    clrtobot();
    outs("\n\n ½Ð¤@¦æ¤@¦æ¨Ì§ÇÁä¤J·Q¨t²Î´£¿ô§Aªº¤º®e,\n"
	 " Àx¦s«á°O±o§âª¬ºA³]¬° [¦Û©w«¬] ¤~¦³§@¥Î\n"
	 " ®y¥k»Ê³Ì¦h100±ø");
    pressanykey();
    vedit(buf, NA, NULL);
    return 0;
}

void
showplans(char *uid)
{
    char            genbuf[200];

    sethomefile(genbuf, uid, fn_plans);
    if (!show_file(genbuf, 7, MAX_QUERYLINES, ONLY_COLOR))
	prints("¡m­Ó¤H¦W¤ù¡n%s ¥Ø«e¨S¦³¦W¤ù", uid);
}

int
showsignature(char *fname, int *j)
{
    FILE           *fp;
    char            buf[256];
    int             i, num = 0;
    char            ch;

    clear();
    move(2, 0);
    setuserfile(fname, "sig.0");
    *j = strlen(fname) - 1;

    for (ch = '1'; ch <= '9'; ch++) {
	fname[*j] = ch;
	if ((fp = fopen(fname, "r"))) {
	    prints("\033[36m¡i Ã±¦WÀÉ.%c ¡j\033[m\n", ch);
	    for (i = 0; i < MAX_SIGLINES && fgets(buf, sizeof(buf), fp); i++)
		outs(buf);
	    num++;
	    fclose(fp);
	}
    }
    return num;
}

int
u_editsig()
{
    int             aborted;
    char            ans[4];
    int             j;
    char            genbuf[200];

    showsignature(genbuf, &j);

    getdata(0, 0, "Ã±¦WÀÉ (E)½s¿è (D)§R°£ (Q)¨ú®ø¡H[Q] ",
	    ans, sizeof(ans), LCECHO);

    aborted = 0;
    if (ans[0] == 'd')
	aborted = 1;
    if (ans[0] == 'e')
	aborted = 2;

    if (aborted) {
	if (!getdata(1, 0, "½Ð¿ï¾ÜÃ±¦WÀÉ(1-9)¡H[1] ", ans, sizeof(ans), DOECHO))
	    ans[0] = '1';
	if (ans[0] >= '1' && ans[0] <= '9') {
	    genbuf[j] = ans[0];
	    if (aborted == 1) {
		unlink(genbuf);
		outs(msg_del_ok);
	    } else {
		setutmpmode(EDITSIG);
		aborted = vedit(genbuf, NA, NULL);
		if (aborted != -1)
		    outs("Ã±¦WÀÉ§ó·s§¹²¦");
	    }
	}
	pressanykey();
    }
    return 0;
}

int
u_editplan()
{
    char            genbuf[200];

    getdata(b_lines - 1, 0, "¦W¤ù (D)§R°£ (E)½s¿è [Q]¨ú®ø¡H[Q] ",
	    genbuf, 3, LCECHO);

    if (genbuf[0] == 'e') {
	int             aborted;

	setutmpmode(EDITPLAN);
	setuserfile(genbuf, fn_plans);
	aborted = vedit(genbuf, NA, NULL);
	if (aborted != -1)
	    outs("¦W¤ù§ó·s§¹²¦");
	pressanykey();
	return 0;
    } else if (genbuf[0] == 'd') {
	setuserfile(genbuf, fn_plans);
	unlink(genbuf);
	outmsg("¦W¤ù§R°£§¹²¦");
    }
    return 0;
}

int
u_editcalendar()
{
    char            genbuf[200];

    getdata(b_lines - 1, 0, "¦æ¨Æ¾ä (D)§R°£ (E)½s¿è [Q]¨ú®ø¡H[Q] ",
	    genbuf, 3, LCECHO);

    if (genbuf[0] == 'e') {
	int             aborted;

	setutmpmode(EDITPLAN);
	setcalfile(genbuf, cuser->userid);
	aborted = vedit(genbuf, NA, NULL);
	if (aborted != -1)
	    outs("¦æ¨Æ¾ä§ó·s§¹²¦");
	pressanykey();
	return 0;
    } else if (genbuf[0] == 'd') {
	setcalfile(genbuf, cuser->userid);
	unlink(genbuf);
	outmsg("¦æ¨Æ¾ä§R°£§¹²¦");
    }
    return 0;
}

/* ¨Ï¥ÎªÌ¶ñ¼gµù¥Uªí®æ */
static void
getfield(int line, char *info, char *desc, char *buf, int len)
{
    char            prompt[STRLEN];
    char            genbuf[200];

    move(line, 2);
    prints("­ì¥ý³]©w¡G%-30.30s (%s)", buf, info);
    snprintf(prompt, sizeof(prompt), "%s¡G", desc);
    if (getdata_str(line + 1, 2, prompt, genbuf, len, DOECHO, buf))
	strcpy(buf, genbuf);
    move(line, 2);
    prints("%s¡G%s", desc, buf);
    clrtoeol();
}

static int
removespace(char *s)
{
    int             i, index;

    for (i = 0, index = 0; s[i]; i++) {
	if (s[i] != ' ')
	    s[index++] = s[i];
    }
    s[index] = '\0';
    return index;
}

static int
ispersonalid(char *inid)
{
    char           *lst = "ABCDEFGHJKLMNPQRSTUVXYWZIO", id[20];
    int             i, j, cksum;

    strlcpy(id, inid, sizeof(id));
    i = cksum = 0;
    if (!isalpha(id[0]) && (strlen(id) != 10))
	return 0;
    if (!(id[1] == '1' || id[1] == '2'))
	return 0;
    id[0] = toupper(id[0]);

    if( strcmp(id, "A100000001") == 0 ||
	strcmp(id, "A200000003") == 0 ||
	strcmp(id, "A123456789") == 0    )
	return 0;
    /* A->10, B->11, ..H->17,I->34, J->18... */
    while (lst[i] != id[0])
	i++;
    i += 10;
    id[0] = i % 10 + '0';
    if (!isdigit(id[9]))
	return 0;
    cksum += (id[9] - '0') + (i / 10);

    for (j = 0; j < 9; ++j) {
	if (!isdigit(id[j]))
	    return 0;
	cksum += (id[j] - '0') * (9 - j);
    }
    return (cksum % 10) == 0;
}

static char    *
getregcode(char *buf)
{
    sprintf(buf, "%s", crypt(cuser->userid, "02"));
    return buf;
}

static int
isvalidemail(char *email)
{
    FILE           *fp;
    char            buf[128], *c;
    if (!strstr(email, "@"))
	return 0;
    for (c = strstr(email, "@"); *c != 0; ++c)
	if ('A' <= *c && *c <= 'Z')
	    *c += 32;

    if ((fp = fopen("etc/banemail", "r"))) {
	while (fgets(buf, sizeof(buf), fp)) {
	    if (buf[0] == '#')
		continue;
	    buf[strlen(buf) - 1] = 0;
	    if (buf[0] == 'A' && strcasecmp(&buf[1], email) == 0)
		return 0;
	    if (buf[0] == 'P' && strcasestr(email, &buf[1]))
		return 0;
	    if (buf[0] == 'S' && strcasecmp(strstr(email, "@") + 1, &buf[1]) == 0)
		return 0;
	}
	fclose(fp);
    }
    return 1;
}

static void
toregister(char *email, char *genbuf, char *phone, char *career,
	   char *ident, char *rname, char *addr, char *mobile)
{
    FILE           *fn;
    char            buf[128];

    sethomefile(buf, cuser->userid, "justify.wait");
    if (phone[0] != 0) {
	fn = fopen(buf, "w");
	assert(fn);
	fprintf(fn, "%s\n%s\n%s\n%s\n%s\n%s\n",
		phone, career, ident, rname, addr, mobile);
	fclose(fn);
    }
    clear();
    stand_title("»{ÃÒ³]©w");
    if (cuser->userlevel & PERM_NOREGCODE){
	strcpy(email, "x");
	goto REGFORM2;
    }
    move(2, 0);
    outs("±z¦n, ¥»¯¸»{ÃÒ»{ÃÒªº¤è¦¡¦³:\n"
	 "  1.­Y±z¦³ E-Mail  (¥»¯¸¤£±µ¨ü yahoo, kimoµ¥§K¶Oªº E-Mail)\n"
	 "    ½Ð¿é¤J±zªº E-Mail , §Ú­Ì·|±Hµo§t¦³»{ÃÒ½Xªº«H¥óµ¹±z\n"
	 "    ¦¬¨ì«á½Ð¨ì (U)ser => (R)egister ¿é¤J»{ÃÒ½X, §Y¥i³q¹L»{ÃÒ\n"
	 "\n"
	 "  2.­Y±z¨S¦³ E-Mail , ½Ð¿é¤J x ,\n"
	 "    §Ú­Ì·|¥Ñ¯¸ªø¿Ë¦Û¼f®Ö±zªºµù¥U¸ê®Æ\n"
	 "************************************************************\n"
	 "* ª`·N!                                                    *\n"
	 "* ±zÀ³¸Ó·|¦b¿é¤J§¹¦¨«á¤Q¤ÀÄÁ¤º¦¬¨ì»{ÃÒ«H, ­Y¹L¤[¥¼¦¬¨ì,    *\n"
	 "* ©Î¿é¤J«áµo¥Í»{ÃÒ½X¿ù»~, ³Â·Ð­«¶ñ¤@¦¸ E-Mail ©Î§ï¤â°Ê»{ÃÒ *\n"
	 "************************************************************\n");

#ifdef HAVEMOBILE
    outs("  3.­Y±z¦³¤â¾÷ªù¸¹¥B·Q±Ä¨ú¤â¾÷Â²°T»{ÃÒªº¤è¦¡ , ½Ð¿é¤J m \n"
	 "    §Ú­Ì±N·|±Hµo§t¦³»{ÃÒ½XªºÂ²°Tµ¹±z \n"
	 "    ¦¬¨ì«á½Ð¨ì(U)ser => (R)egister ¿é¤J»{ÃÒ½X, §Y¥i³q¹L»{ÃÒ\n");
#endif

    while (1) {
	email[0] = 0;
	getfield(15, "¨­¤À»{ÃÒ¥Î", "E-Mail Address", email, 50);
	if (strcmp(email, "x") == 0 || strcmp(email, "X") == 0)
	    break;
#ifdef HAVEMOBILE
	else if (strcmp(email, "m") == 0 || strcmp(email, "M") == 0) {
	    if (isvalidmobile(mobile)) {
		char            yn[3];
		getdata(16, 0, "½Ð¦A¦¸½T»{±z¿é¤Jªº¤â¾÷¸¹½X¥¿½T¹À? [y/N]",
			yn, sizeof(yn), LCECHO);
		if (yn[0] == 'Y' || yn[0] == 'y')
		    break;
	    } else {
		move(17, 0);
		prints("«ü©wªº¤â¾÷¸¹½X¤£¦Xªk,"
		       "­Y±zµL¤â¾÷ªù¸¹½Ð¿ï¾Ü¨ä¥L¤è¦¡»{ÃÒ");
	    }

	}
#endif
	else if (isvalidemail(email)) {
	    char            yn[3];
	    getdata(16, 0, "½Ð¦A¦¸½T»{±z¿é¤Jªº E-Mail ¦ì¸m¥¿½T¹À? [y/N]",
		    yn, sizeof(yn), LCECHO);
	    if (yn[0] == 'Y' || yn[0] == 'y')
		break;
	} else {
	    move(17, 0);
	    prints("«ü©wªº E-Mail ¤£¦Xªk,"
		   "­Y±zµL E-Mail ½Ð¿é¤J x¥Ñ¯¸ªø¤â°Ê»{ÃÒ");
	}
    }
    strncpy(cuser->email, email, sizeof(cuser->email));
 REGFORM2:
    if (strcasecmp(email, "x") == 0) {	/* ¤â°Ê»{ÃÒ */
	if ((fn = fopen(fn_register, "a"))) {
	    fprintf(fn, "num: %d, %s", usernum, ctime(&now));
	    fprintf(fn, "uid: %s\n", cuser->userid);
	    fprintf(fn, "ident: %s\n", ident);
	    fprintf(fn, "name: %s\n", rname);
	    fprintf(fn, "career: %s\n", career);
	    fprintf(fn, "addr: %s\n", addr);
	    fprintf(fn, "phone: %s\n", phone);
	    fprintf(fn, "mobile: %s\n", mobile);
	    fprintf(fn, "email: %s\n", email);
	    fprintf(fn, "----\n");
	    fclose(fn);
	}
    } else {
	char            tmp[IDLEN + 1];
	if (phone != NULL) {
#ifdef HAVEMOBILE
	    if (strcmp(email, "m") == 0 || strcmp(email, "M") == 0)
		sprintf(genbuf, sizeof(genbuf),
			"%s:%s:<Mobile>", phone, career);
	    else
#endif
		snprintf(genbuf, sizeof(genbuf),
			 "%s:%s:<Email>", phone, career);
	    strncpy(cuser->justify, genbuf, REGLEN);
	    sethomefile(buf, cuser->userid, "justify");
	}
	snprintf(buf, sizeof(buf),
		 "±z¦b " BBSNAME " ªº»{ÃÒ½X: %s", getregcode(genbuf));
	strlcpy(tmp, cuser->userid, sizeof(tmp));
	strlcpy(cuser->userid, "SYSOP", sizeof(cuser->userid));
#ifdef HAVEMOBILE
	if (strcmp(email, "m") == 0 || strcmp(email, "M") == 0)
	    mobile_message(mobile, buf);
	else
#endif
	    bsmtp("etc/registermail", buf, email, 0);
	strlcpy(cuser->userid, tmp, sizeof(cuser->userid));
	outs("\n\n\n§Ú­Ì§Y±N±H¥X»{ÃÒ«H (±zÀ³¸Ó·|¦b 10 ¤ÀÄÁ¤º¦¬¨ì)\n"
	     "¦¬¨ì«á±z¥i¥H¸ò¾Ú»{ÃÒ«H¼ÐÃDªº»{ÃÒ½X\n"
	     "¿é¤J¨ì (U)ser -> (R)egister «á´N¥i¥H§¹¦¨µù¥U");
	pressanykey();
	return;
    }
}

static int HaveRejectStr(char *s, char **rej)
{
    int     i;
    char    *ptr, *rejectstr[] =
	{"·F", "ªü", "¤£", "§A¶ý", "¬Y", "²Â", "§b", "..", "xx",
	 "§AºÞ", "ºÞ§Ú", "²q", "¤Ñ¤~", "¶W¤H", 
	 "£t", "£u", "£v", "£w", "£x", "£y", "£z", "£{", "£|", "£}", "£~",
	 "£¡", "£¢", "££", "£¤",/*"£¥",*/    "£¦", "£§", "£¨", "£©", "£ª",
	 "£¸", "£¹", "£º", "£«", "£¬", "£­", "£®", "£¯", "£°", "£±", "£²",
	 "£³", "£´", "£µ", "£¶", "£·", NULL};

    if( rej != NULL )
	for( i = 0 ; rej[i] != NULL ; ++i )
	    if( strstr(s, rej[i]) )
		return 1;

    for( i = 0 ; rejectstr[i] != NULL ; ++i )
	if( strstr(s, rejectstr[i]) )
	    return 1;

    if( (ptr = strstr(s, "£¥")) != NULL ){
	if( ptr != s && strncmp(ptr - 1, "³£¥«", 4) == 0 )
	    return 0;
	return 1;
    }
    return 0;
}

static char *isvalidname(char *rname)
{
#ifdef FOREIGN_REG
    return NULL;
#else
    char    *rejectstr[] =
	{"ªÎ", "­D", "½ÞÀY", "¤p¥Õ", "¤p©ú", "¸ô¤H", "¦Ñ¤ý", "¦Ñ§õ", "Ä_¨©",
	 "¥ý¥Í", "«Ó­ô", "¦ÑÀY", "¤p©n", "¤p©j", "¬ü¤k", "¤p©f", "¤jÀY", 
	 "¤½¥D", "¦P¾Ç", "Ä_Ä_", "¤½¤l", "¤jÀY", "¤p¤p", "¤p§Ì", "¤p©f",
	 "©f©f", "¼K", "¶â", "·Ý·Ý", "¤j­ô", "µL",
	 NULL};
    if( removespace(rname) && rname[0] < 0 &&
	strlen(rname) >= 4 &&
	!HaveRejectStr(rname, rejectstr) &&
	strncmp(rname, "¤p", 2) != 0   && //°_ÀY¬O¡u¤p¡v
	strncmp(rname, "§Ú¬O", 4) != 0 && //°_ÀY¬O¡u§Ú¬O¡v
	!(strlen(rname) == 4 && strncmp(&rname[2], "¨à", 2) == 0) &&
	!(strlen(rname) >= 4 && strncmp(&rname[0], &rname[2], 2) == 0))
	return NULL;
    return "±zªº¿é¤J¤£¥¿½T";
#endif

}

static char *isvalidcareer(char *career)
{
#ifndef FOREIGN_REG
    char    *rejectstr[] = {NULL};
    if (!(removespace(career) && career[0] < 0 && strlen(career) >= 6) ||
	strcmp(career, "®a¸Ì") == 0 || HaveRejectStr(career, rejectstr) )
	return "±zªº¿é¤J¤£¥¿½T";
    if (strcmp(&career[strlen(career) - 2], "¤j") == 0 ||
	strcmp(&career[strlen(career) - 4], "¤j¾Ç") == 0 ||
	strcmp(career, "¾Ç¥Í¤j¾Ç") == 0)
	return "³Â·Ð½Ð¥[¾Ç®Õ¨t©Ò";
    if (strcmp(career, "¾Ç¥Í°ª¤¤") == 0)
	return "³Â·Ð¿é¤J¾Ç®Õ¦WºÙ";
#endif
    return NULL;
}

static char *isvalidaddr(char *addr)
{
    char    *rejectstr[] =
	{"¦a²y", "»Èªe", "¤õ¬P", NULL};

    if (!removespace(addr) || addr[0] > 0 || strlen(addr) < 15) 
	return "³o­Ó¦a§}¨Ã¤£¦Xªk";
    if (strstr(addr, "«H½c") != NULL || strstr(addr, "¶l¬F") != NULL) 
	return "©êºp§Ú­Ì¤£±µ¨ü¶l¬F«H½c";
    if ((strstr(addr, "¥«") == NULL && strstr(addr, "É]") == NULL &&
	 strstr(addr, "¿¤") == NULL && strstr(addr, "«Ç") == NULL) ||
	HaveRejectStr(addr, rejectstr)             ||
	strcmp(&addr[strlen(addr) - 2], "¬q") == 0 ||
	strcmp(&addr[strlen(addr) - 2], "¸ô") == 0 ||
	strcmp(&addr[strlen(addr) - 2], "«Ñ") == 0 ||
	strcmp(&addr[strlen(addr) - 2], "§Ë") == 0 ||
	strcmp(&addr[strlen(addr) - 2], "°Ï") == 0 ||
	strcmp(&addr[strlen(addr) - 2], "¥«") == 0 ||
	strcmp(&addr[strlen(addr) - 2], "µó") == 0    )
	return "³o­Ó¦a§}¨Ã¤£¦Xªk";
    return NULL;
}

static char *isvalidphone(char *phone)
{
    int     i;
    for( i = 0 ; phone[i] != 0 ; ++i )
	if( !isdigit(phone[i]) )
	    return "½Ð¤£­n¥[¤À¹j²Å¸¹";
    if (!removespace(phone) || 
	strlen(phone) < 9 || 
	strstr(phone, "00000000") != NULL ||
	strstr(phone, "22222222") != NULL    ) {
	return "³o­Ó¹q¸Ü¸¹½X¨Ã¤£¦Xªk(½Ð§t°Ï½X)" ;
    }
    return NULL;
}

int
u_register(void)
{
    char            rname[21], addr[51], ident[12], mobile[21];
#ifdef FOREIGN_REG
    char            fore[2];
#endif
    char            phone[21], career[41], email[51], birthday[9], sex_is[2],
                    year, mon, day;
    char            inregcode[14], regcode[50];
    char            ans[3], *ptr, *errcode;
    char            genbuf[200];
    FILE           *fn;

    if (cuser->userlevel & PERM_LOGINOK) {
	outs("±zªº¨­¥÷½T»{¤w¸g§¹¦¨¡A¤£»Ý¶ñ¼g¥Ó½Ðªí");
	return XEASY;
    }
    if ((fn = fopen(fn_register, "r"))) {
	while (fgets(genbuf, STRLEN, fn)) {
	    if ((ptr = strchr(genbuf, '\n')))
		*ptr = '\0';
	    if (strncmp(genbuf, "uid: ", 5) == 0 &&
		strcmp(genbuf + 5, cuser->userid) == 0) {
		fclose(fn);
		outs("±zªºµù¥U¥Ó½Ð³æ©|¦b³B²z¤¤¡A½Ð­@¤ßµ¥­Ô");
		return XEASY;
	    }
	}
	fclose(fn);
    }
    strlcpy(ident, cuser->ident, sizeof(ident));
    strlcpy(rname, cuser->realname, sizeof(rname));
    strlcpy(addr, cuser->address, sizeof(addr));
    strlcpy(email, cuser->email, sizeof(email));
    snprintf(mobile, sizeof(mobile), "0%09d", cuser->mobile);
    if (cuser->month == 0 && cuser->day && cuser->year == 0)
	birthday[0] = 0;
    else
	snprintf(birthday, sizeof(birthday), "%02i/%02i/%02i",
		 cuser->month, cuser->day, cuser->year % 100);
    sex_is[0] = (cuser->sex % 8) + '1';
    sex_is[1] = 0;
    career[0] = phone[0] = '\0';
    sethomefile(genbuf, cuser->userid, "justify.wait");
    if ((fn = fopen(genbuf, "r"))) {
	fgets(phone, 21, fn);
	phone[strlen(phone) - 1] = 0;
	fgets(career, 41, fn);
	career[strlen(career) - 1] = 0;
	fgets(ident, 12, fn);
	ident[strlen(ident) - 1] = 0;
	fgets(rname, 21, fn);
	rname[strlen(rname) - 1] = 0;
	fgets(addr, 51, fn);
	addr[strlen(addr) - 1] = 0;
	fgets(mobile, 21, fn);
	mobile[strlen(mobile) - 1] = 0;
	fclose(fn);
    }

    if (cuser->userlevel & PERM_NOREGCODE) {
	vmsg("±z¤£³Q¤¹³\\¨Ï¥Î»{ÃÒ½X»{ÃÒ¡C½Ð¶ñ¼gµù¥U¥Ó½Ð³æ");
	goto REGFORM;
    }

    if (cuser->year != 0 &&	/* ¤w¸g²Ä¤@¦¸¶ñ¹L¤F~ ^^" */
	strcmp(cuser->email, "x") != 0 &&	/* ¤W¦¸¤â°Ê»{ÃÒ¥¢±Ñ */
	strcmp(cuser->email, "X") != 0) {
	clear();
	stand_title("EMail»{ÃÒ");
	move(2, 0);
	prints("%s(%s) ±z¦n¡A½Ð¿é¤J±zªº»{ÃÒ½X¡C\n"
	       "©Î±z¥i¥H¿é¤J x¨Ó­«·s¶ñ¼g E-Mail ©Î§ï¥Ñ¯¸ªø¤â°Ê»{ÃÒ\n",
	       cuser->userid, cuser->username);
	inregcode[0] = 0;
	do{
	    getdata(10, 0, "±zªº¿é¤J: ", inregcode, sizeof(inregcode), DOECHO);
	    if( strcmp(inregcode, "x") == 0 ||
		strcmp(inregcode, "X") == 0 ||
		strlen(inregcode) == 13 )
		break;
	    if( strlen(inregcode) != 13 )
		vmsg("»{ÃÒ½X¿é¤J¤£§¹¥þ¡AÀ³¸Ó¤@¦@¦³¤Q¤T½X¡C");
	} while( 1 );

	if (strcmp(inregcode, getregcode(regcode)) == 0) {
	    int             unum;
	    if ((unum = getuser(cuser->userid)) == 0) {
		vmsg("¨t²Î¿ù»~¡A¬dµL¦¹¤H¡I");
		u_exit("getuser error");
		exit(0);
	    }
	    mail_muser(*cuser, "[µù¥U¦¨¥\\Åo]", "etc/registeredmail");
	    if(cuser->uflag2 & FOREIGN)
		mail_muser(*cuser, "[¥X¤J¹ÒºÞ²z§½]", "etc/foreign_welcome");
	    cuser->userlevel |= (PERM_LOGINOK | PERM_POST);
	    prints("\nµù¥U¦¨¥\\, ­«·s¤W¯¸«á±N¨ú±o§¹¾ãÅv­­\n"
		   "½Ð«ö¤U¥ô¤@Áä¸õÂ÷«á­«·s¤W¯¸~ :)");
	    sethomefile(genbuf, cuser->userid, "justify.wait");
	    unlink(genbuf);
	    pressanykey();
	    u_exit("registed");
	    exit(0);
	    return QUIT;
	} else if (strcmp(inregcode, "x") != 0 &&
		   strcmp(inregcode, "X") != 0) {
	    vmsg("»{ÃÒ½X¿ù»~¡I");
	} else {
	    toregister(email, genbuf, phone, career,
		       ident, rname, addr, mobile);
	    return FULLUPDATE;
	}
    }

    REGFORM:
    getdata(b_lines - 1, 0, "±z½T©w­n¶ñ¼gµù¥U³æ¶Ü(Y/N)¡H[N] ",
	    ans, sizeof(ans), LCECHO);
    if (ans[0] != 'y')
	return FULLUPDATE;

    move(2, 0);
    clrtobot();
    while (1) {
	clear();
	move(1, 0);
	prints("%s(%s) ±z¦n¡A½Ð¾Ú¹ê¶ñ¼g¥H¤Uªº¸ê®Æ:",
	       cuser->userid, cuser->username);
#ifdef FOREIGN_REG
	fore[0] = 'y';
	fore[1] = 0;
	getfield(2, "Y/n", "¬O§_¬°¥xÆW©~¥ÁÁ¡H", fore, 2);
    	if (fore[0] == 'n')
	    fore[0] |= FOREIGN;
	else
	    fore[0] = 0;
	if (!fore[0]){
#endif
	    while( 1 ){
		getfield(3, "D123456789", "¨­¤ÀÃÒ¸¹", ident, 11);
		if ('a' <= ident[0] && ident[0] <= 'z')
		    ident[0] -= 32;
		if( ispersonalid(ident) )
		    break;
		vmsg("±zªº¿é¤J¤£¥¿½T(­Y¦³°ÝÃD³Â·Ð¦ÜSYSOPªO)");
	    }
#ifdef FOREIGN_REG
	}
	else{
	    int i;
	    while( 1 ){
		getfield(4, "0123456789","¨­¤ÀÃÒ¸¹ Å@·Ó¸¹½X ©Î SSN", ident, 11);
		move(6, 2);
		prints("¸¹½X¦³»~ªÌ±NµLªk¨ú±o¶i¤@¨BªºÅv­­¡I");
		getdata(7, 2, "¬O§_½T©w(Y/N)", ans, sizeof(ans), LCECHO);
		if (ans[0] == 'y' || ans[0] == 'Y')
		    break;
		vmsg("½Ð­«·s¿é¤J(­Y¦³°ÝÃD³Â·Ð¦ÜSYSOPªO)");
	    }
	    for(i = 0; ans[i] != 0; i++)
		if ('a' <= ident[0] && ident[0] <= 'z')
		    ident[0] -= 32;
	    if( ispersonalid(ident) ){
		fore[0] = 0;
		vmsg("±zªº¨­¥÷¤w§ó§ï¬°¥xÆW©~¥Á");
	    }
	}
#endif
	while (1) {
	    getfield(8, 
#ifdef FOREIGN_REG
                     "½Ð¥Î¥»¦W",
#else
                     "½Ð¥Î¤¤¤å",
#endif
                     "¯u¹ê©m¦W", rname, 20);
	    if( (errcode = isvalidname(rname)) == NULL )
		break;
	    else
		vmsg(errcode);
	}

	move(11, 0);
	prints("  ºÉ¶q¸Ô²Óªº¶ñ¼g±zªºªA°È³æ¦ì, ¤j±M°|®Õ½Ð³Â·Ð"
	       "  ¥[\033[1;33m¨t©Ò\033[m, ¤½¥q³æ¦ì½Ð¥[Â¾ºÙ\n"
	       );
	while (1) {
	    getfield(9, "¾Ç®Õ(§t\033[1;33m¨t©Ò¦~¯Å\033[m)©Î³æ¦ìÂ¾ºÙ",
		     "ªA°È³æ¦ì", career, 40);
	    if( (errcode = isvalidcareer(career)) == NULL )
		break;
	    else
		vmsg(errcode);
	}
	while (1) {
	    getfield(11, "§t\033[1;33m¿¤¥«\033[m¤Îªù¹ì¸¹½X"
		     "(¥x¥_½Ð¥[\033[1;33m¦æ¬F°Ï\033[m)",
		     "¥Ø«e¦í§}", addr, 50);
	    if( (errcode = isvalidaddr(addr) 
#ifdef FOREIGN_REG
                && fore[0] ==0 
#endif
                ) == NULL )
		break;
	    else
		vmsg(errcode);
	}
	while (1) {
	    getfield(13, "¤£¥[-(), ¥]¬Aªø³~°Ï¸¹", "³sµ¸¹q¸Ü", phone, 11);
	    if( (errcode = isvalidphone(phone)) == NULL )
		break;
	    else
		vmsg(errcode);
	}
	getfield(15, "¥u¿é¤J¼Æ¦r ¦p:0912345678 (¥i¤£¶ñ)",
		 "¤â¾÷¸¹½X", mobile, 20);
	while (1) {
	    int             len;

	    getfield(17, "¤ë¤ë/¤é¤é/¦è¤¸ ¦p:09/27/76", "¥Í¤é", birthday, 9);
	    len = strlen(birthday);
	    if (!len) {
		snprintf(birthday, sizeof(birthday), "%02i/%02i/%02i",
			 cuser->month, cuser->day, cuser->year % 100);
		mon = cuser->month;
		day = cuser->day;
		year = cuser->year;
	    } else if (len == 8) {
		mon = (birthday[0] - '0') * 10 + (birthday[1] - '0');
		day = (birthday[3] - '0') * 10 + (birthday[4] - '0');
		year = (birthday[6] - '0') * 10 + (birthday[7] - '0');
	    } else{
		vmsg("±zªº¿é¤J¤£¥¿½T");
		continue;
	    }
	    if (mon > 12 || mon < 1 || day > 31 || day < 1 || year > 90 ||
		year < 40){
		vmsg("±zªº¿é¤J¤£¥¿½T");
		continue;
	    }
	    break;
	}
	getfield(19, "1.¸¯®æ 2.©j±µ ", "©Ê§O", sex_is, 2);
	getdata(20, 0, "¥H¤W¸ê®Æ¬O§_¥¿½T(Y/N)¡H(Q)¨ú®øµù¥U [N] ",
		ans, sizeof(ans), LCECHO);
	if (ans[0] == 'q')
	    return 0;
	if (ans[0] == 'y')
	    break;
    }
    strlcpy(cuser->ident, ident, sizeof(cuser->ident));
    strlcpy(cuser->realname, rname, sizeof(cuser->realname));
    strlcpy(cuser->address, addr, sizeof(cuser->address));
    strlcpy(cuser->email, email, sizeof(cuser->email));
    cuser->mobile = atoi(mobile);
    cuser->sex = (sex_is[0] - '1') % 8;
    cuser->month = mon;
    cuser->day = day;
    cuser->year = year;
#ifdef FOREIGN_REG
    if (fore[0])
	cuser->uflag2 |= FOREIGN;
    else
	cuser->uflag2 &= ~FOREIGN;
#endif
    trim(career);
    trim(addr);
    trim(phone);

    toregister(email, genbuf, phone, career, ident, rname, addr, mobile);

    clear();
    move(9, 3);
    prints("³Ì«áPost¤@½g\033[32m¦Û§Ú¤¶²Ð¤å³¹\033[mµ¹¤j®a§a¡A"
	   "§i¶D©Ò¦³¦Ñ°©ÀY\033[31m§Ú¨Ó°Õ^$¡C\\n\n\n\n");
    pressanykey();
    cuser->userlevel |= PERM_POST;
    brc_initial_board("WhoAmI");
    set_board();
    do_post();
    cuser->userlevel &= ~PERM_POST;
    return 0;
}

/* ¦C¥X©Ò¦³µù¥U¨Ï¥ÎªÌ */
static int      usercounter, totalusers;
static unsigned short u_list_special;

static int
u_list_CB(int num, userec_t * uentp)
{
    static int      i;
    char            permstr[8], *ptr;
    register int    level;

    if (uentp == NULL) {
	move(2, 0);
	clrtoeol();
	prints("\033[7m  ¨Ï¥ÎªÌ¥N¸¹   %-25s   ¤W¯¸  ¤å³¹  %s  "
	       "³Ìªñ¥úÁ{¤é´Á     \033[0m\n",
	       "ºï¸¹¼ÊºÙ",
	       HAS_PERM(PERM_SEEULEVELS) ? "µ¥¯Å" : "");
	i = 3;
	return 0;
    }
    if (bad_user_id(uentp->userid))
	return 0;

    if ((uentp->userlevel & ~(u_list_special)) == 0)
	return 0;

    if (i == b_lines) {
	prints("\033[34;46m  ¤wÅã¥Ü %d/%d ¤H(%d%%)  \033[31;47m  "
	       "(Space)\033[30m ¬Ý¤U¤@­¶  \033[31m(Q)\033[30m Â÷¶}  \033[m",
	       usercounter, totalusers, usercounter * 100 / totalusers);
	i = igetch();
	if (i == 'q' || i == 'Q')
	    return QUIT;
	i = 3;
    }
    if (i == 3) {
	move(3, 0);
	clrtobot();
    }
    level = uentp->userlevel;
    strlcpy(permstr, "----", sizeof(permstr));
    if (level & PERM_SYSOP)
	permstr[0] = 'S';
    else if (level & PERM_ACCOUNTS)
	permstr[0] = 'A';
    else if (level & PERM_DENYPOST)
	permstr[0] = 'p';

    if (level & (PERM_BOARD))
	permstr[1] = 'B';
    else if (level & (PERM_BM))
	permstr[1] = 'b';

    if (level & (PERM_XEMPT))
	permstr[2] = 'X';
    else if (level & (PERM_LOGINOK))
	permstr[2] = 'R';

    if (level & (PERM_CLOAK | PERM_SEECLOAK))
	permstr[3] = 'C';

    ptr = (char *)Cdate(&uentp->lastlogin);
    ptr[18] = '\0';
    prints("%-14s %-27.27s%5d %5d  %s  %s\n",
	   uentp->userid,
	   uentp->username,
	   uentp->numlogins, uentp->numposts,
	   HAS_PERM(PERM_SEEULEVELS) ? permstr : "", ptr);
    usercounter++;
    i++;
    return 0;
}

int
u_list()
{
    char            genbuf[3];

    setutmpmode(LAUSERS);
    u_list_special = usercounter = 0;
    totalusers = SHM->number;
    if (HAS_PERM(PERM_SEEULEVELS)) {
	getdata(b_lines - 1, 0, "Æ[¬Ý [1]¯S®íµ¥¯Å (2)¥þ³¡¡H",
		genbuf, 3, DOECHO);
	if (genbuf[0] != '2')
	    u_list_special = PERM_BASIC | PERM_CHAT | PERM_PAGE | PERM_POST | PERM_LOGINOK | PERM_BM;
    }
    u_list_CB(0, NULL);
    if (passwd_apply(u_list_CB) == -1) {
	outs(msg_nobody);
	return XEASY;
    }
    move(b_lines, 0);
    clrtoeol();
    prints("\033[34;46m  ¤wÅã¥Ü %d/%d ªº¨Ï¥ÎªÌ(¨t²Î®e¶qµL¤W­­)  "
	   "\033[31;47m  (½Ð«ö¥ô·NÁäÄ~Äò)  \033[m", usercounter, totalusers);
    egetch();
    return 0;
}
