/* $Id$ */
#include "bbs.h"

static char    * const sex[8] = {
    MSG_BIG_BOY, MSG_BIG_GIRL, MSG_LITTLE_BOY, MSG_LITTLE_GIRL,
    MSG_MAN, MSG_WOMAN, MSG_PLANT, MSG_MIME
};

#ifdef CHESSCOUNTRY
static const char * const chess_photo_name[2] = {
    "photo_fivechess", "photo_cchess"
};

static const char * const chess_type[2] = {
    "¤­¤l´Ñ", "¶H´Ñ"
};
#endif

int
kill_user(int num)
{
  userec_t u;
  memset(&u, 0, sizeof(userec_t));
  log_usies("KILL", getuserid(num));
  setuserid(num, "");
  passwd_update(num, &u);
  return 0;
}
int
u_loginview(void)
{
    int             i;
    unsigned int    pbits = cuser.loginview;

    clear();
    move(4, 0);
    for (i = 0; i < NUMVIEWFILE; i++)
	prints("    %c. %-20s %-15s \n", 'A' + i,
	       loginview_file[i][1], ((pbits >> i) & 1 ? "£¾" : "¢æ"));

    clrtobot();
    while ((i = getkey("½Ð«ö [A-N] ¤Á´«³]©w¡A«ö [Return] µ²§ô¡G"))!='\r')
       {
	i = i - 'a';
	if (i >= NUMVIEWFILE || i < 0)
	    bell();
	else {
	    pbits ^= (1 << i);
	    move(i + 4, 28);
	    outs((pbits >> i) & 1 ? "£¾" : "¢æ");
	}
    }

    if (pbits != cuser.loginview) {
	cuser.loginview = pbits;
	passwd_update(usernum, &cuser);
    }
    return 0;
}

void
user_display(const userec_t * u, int adminmode)
{
    int             diff = 0;
    char            genbuf[200];

    clrtobot();
    prints(
	   "        " ANSI_COLOR(30;41) "¢r¢s¢r¢s¢r¢s" ANSI_RESET "  " ANSI_COLOR(1;30;45) "    ¨Ï ¥Î ªÌ"
	   " ¸ê ®Æ        "
	   "     " ANSI_RESET "  " ANSI_COLOR(30;41) "¢r¢s¢r¢s¢r¢s" ANSI_RESET "\n");
    prints("                ¥N¸¹¼ÊºÙ: %s(%s)\n"
	   "                ¯u¹ê©m¦W: %s"
#if FOREIGN_REG_DAY > 0
	   " %s%s"
#elif defined(FOREIGN_REG)
	   " %s"
#endif
	   "\n"
	   "                ©~¦í¦í§}: %s\n"
	   "                ¹q¤l«H½c: %s\n"
	   "                ©Ê    §O: %s\n"
	   "                »È¦æ±b¤á: %d »È¨â\n",
	   u->userid, u->username, u->realname,
#if FOREIGN_REG_DAY > 0
	   u->uflag2 & FOREIGN ? "(¥~Äy: " : "",
	   u->uflag2 & FOREIGN ?
		(u->uflag2 & LIVERIGHT) ? "¥Ã¤[©~¯d)" : "¥¼¨ú±o©~¯dÅv)"
		: "",
#elif defined(FOREIGN_REG)
	   u->uflag2 & FOREIGN ? "(¥~Äy)" : "",
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
#ifdef PLAY_ANGEL
    if (adminmode)
	prints("                ¤p ¤Ñ ¨Ï: %s\n",
		u->myangel[0] ? u->myangel : "µL");
#endif
    prints("                µù¥U¤é´Á: %s", ctime4(&u->firstlogin));
    prints("                «e¦¸¥úÁ{: %s", ctime4(&u->lastlogin));
    prints("                «e¦¸ÂIºq: %s", ctime4(&u->lastsong));
    prints("                ¤W¯¸¤å³¹: %d ¦¸ / %d ½g\n",
	   u->numlogins, u->numposts);

#ifdef CHESSCOUNTRY
    {
	int i, j;
	FILE* fp;
	for(i = 0; i < 2; ++i){
	    sethomefile(genbuf, u->userid, chess_photo_name[i]);
	    fp = fopen(genbuf, "r");
	    if(fp != NULL){
		for(j = 0; j < 11; ++j)
		    fgets(genbuf, 200, fp);
		fgets(genbuf, 200, fp);
		prints("%12s´Ñ°ê¦Û§Ú´y­z: %s", chess_type[i], genbuf + 11);
	    }
	}
    }
#endif

    if (adminmode) {
	strcpy(genbuf, "bTCPRp#@XWBA#VSM0123456789ABCDEF");
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
    outs("        " ANSI_COLOR(30;41) "¢r¢s¢r¢s¢r¢s¢r¢s¢r¢s¢r¢s¢r¢s¢r¢s¢r¢s¢r¢s¢r¢s¢r"
	 "¢s¢r¢s¢r¢s¢r¢s" ANSI_RESET);

    outs((u->userlevel & PERM_LOGINOK) ?
	 "\n±zªºµù¥Uµ{§Ç¤w¸g§¹¦¨¡AÅwªï¥[¤J¥»¯¸" :
	 "\n¦pªG­n´£ª@Åv­­¡A½Ð°Ñ¦Ò¥»¯¸¤½§GÄæ¿ì²zµù¥U");

#ifdef NEWUSER_LIMIT
    if ((u->lastlogin - u->firstlogin < 3 * 86400) && !HasUserPerm(PERM_POST))
	outs("\n·s¤â¤W¸ô¡A¤T¤Ñ«á¶}©ñÅv­­");
#endif
}

void
mail_violatelaw(const char *crime, const char *police, const char *reason, const char *result)
{
    char            genbuf[200];
    fileheader_t    fhdr;
    FILE           *fp;

    sethomepath(genbuf, crime);
    stampfile(genbuf, &fhdr);
    if (!(fp = fopen(genbuf, "w")))
	return;
    fprintf(fp, "§@ªÌ: [Pttªk°|]\n"
	    "¼ÐÃD: [³ø§i] ¹Hªk§P¨M³ø§i\n"
	    "®É¶¡: %s\n"
	    ANSI_COLOR(1;32) "%s" ANSI_RESET "§P¨M¡G\n     " ANSI_COLOR(1;32) "%s" ANSI_RESET
	    "¦]" ANSI_COLOR(1;35) "%s" ANSI_RESET "¦æ¬°¡A\n¹H¤Ï¥»¯¸¯¸³W¡A³B¥H" ANSI_COLOR(1;35) "%s" ANSI_RESET "¡A¯S¦¹³qª¾"
	    "\n½Ð¨ì PttLaw ¬d¸ß¬ÛÃöªk³W¸ê°T¡A¨Ã¨ì Play-Pay-ViolateLaw Ãº¥æ»@³æ",
	    ctime4(&now), police, crime, reason, result);
    fclose(fp);
    strcpy(fhdr.title, "[³ø§i] ¹Hªk§P¨M³ø§i");
    strcpy(fhdr.owner, "[Pttªk°|]");
    sethomedir(genbuf, crime);
    append_record(genbuf, &fhdr, sizeof(fhdr));
}

void
kick_all(char *user)
{
   userinfo_t *ui;
   int num = searchuser(user, NULL), i=1;
   while((ui = (userinfo_t *) search_ulistn(num, i))>0)
       {
         if(ui == currutmp) i++;
         if ((ui->pid <= 0 || kill(ui->pid, SIGHUP) == -1))
                         purge_utmp(ui);
         log_usies("KICK ALL", user);
       }
}

static void
violate_law(userec_t * u, int unum)
{
    char            ans[4], ans2[4];
    char            reason[128];
    move(1, 0);
    clrtobot();
    move(2, 0);
    outs("(1)Cross-post (2)¶Ãµo¼s§i«H (3)¶Ãµo³sÂê«H\n");
    outs("(4)ÄÌÂZ¯¸¤W¨Ï¥ÎªÌ (8)¨ä¥L¥H»@³æ³B¸m¦æ¬°\n(9)¬å id ¦æ¬°\n");
    getdata(5, 0, "(0)µ²§ô", ans, 3, DOECHO);
    switch (ans[0]) {
    case '1':
	strcpy(reason, "Cross-post");
	break;
    case '2':
	strcpy(reason, "¶Ãµo¼s§i«H");
	break;
    case '3':
	strcpy(reason, "¶Ãµo³sÂê«H");
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
    getdata(7, 0, msg_sure_ny, ans2, 3, LCECHO);
    if (*ans2 != 'y')
	return;
    if (ans[0] == '9') {
	char            src[STRLEN], dst[STRLEN];
	sethomepath(src, u->userid);
	snprintf(dst, sizeof(dst), "tmp/%s", u->userid);
	friend_delete_all(u->userid, FRIEND_ALOHA);
	Rename(src, dst);
	post_violatelaw(u->userid, cuser.userid, reason, "¬å°£ ID");
        kill_user(unum);

    } else {
        kick_all(u->userid);
	u->userlevel |= PERM_VIOLATELAW;
	u->vl_count++;
	passwd_update(unum, u);
	post_violatelaw(u->userid, cuser.userid, reason, "»@³æ³B¥÷");
	mail_violatelaw(u->userid, cuser.userid, reason, "»@³æ³B¥÷");
    }
    pressanykey();
}

void Customize(void)
{
    char    done = 0, mindbuf[5];
    int     dirty = 0;
    int     key;
    char    *wm[3] = {"¤@¯ë", "¶i¶¥", "¥¼¨Ó"};
#ifdef PLAY_ANGEL
    char    *am[4] = {"¨k¤k¬Ò¥i", "­­¤k¥Í", "­­¨k¥Í", "¼È¤£±µ¨ü·sªº¤p¥D¤H"};
#endif

    showtitle("­Ó¤H¤Æ³]©w", "­Ó¤H¤Æ³]©w");
    memcpy(mindbuf, &currutmp->mind, 4);
    mindbuf[4] = 0;
    while( !done ) {
	char maxc = 'a';
	move(2, 0);
	outs("±z¥Ø«eªº­Ó¤H¤Æ³]©w: ");
	move(4, 0);
	prints("%-40s%10s\n", "a. ¤ô²y¼Ò¦¡",
	       wm[(cuser.uflag2 & WATER_MASK)]);
	prints("%-40s%10s\n", "b. ±µ¨ü¯¸¥~«H", REJECT_OUTTAMAIL ? "§_" : "¬O");
	prints("%-40s%10s\n", "c. ·sªO¦Û°Ê¶i§Úªº³Ì·R",
	       ((cuser.uflag2 & FAVNEW_FLAG) ? "¬O" : "§_"));
	prints("%-40s%10s\n", "d. ¥Ø«eªº¤ß±¡", mindbuf);
	prints("%-40s%10s\n", "e. °ª«G«×Åã¥Ü§Úªº³Ì·R", 
	       (!(cuser.uflag2 & FAVNOHILIGHT) ? "¬O" : "§_"));
	prints("%-40s%10s\n", "f. °ÊºA¬ÝªO", 
	       ((cuser.uflag & MOVIE_FLAG) ? "¬O" : "§_"));
	maxc = 'f';

#ifdef PLAY_ANGEL
	if( HasUserPerm(PERM_ANGEL) ){
	    prints("%-40s%10s\n", "g. ¶}©ñ¤p¥D¤H¸ß°Ý", 
		    (REJECT_QUESTION ? "§_" : "¬O"));
	    prints("%-40s%10s\n", "h. ±µ¨üªº¤p¥D¤H©Ê§O", am[ANGEL_STATUS()]);
	    maxc = 'H';
	}
#endif

#ifdef DBCSAWARE
	prints("%-40s%10s\n", "i. ¦Û°Ê°»´úÂù¦ì¤¸¦r¶°(¦p¥þ«¬¤¤¤å)",
			((cuser.uflag & DBCSAWARE_FLAG) ? "¬O" : "§_"));
	maxc = 'i';
#endif
	key = getkey("½Ð«ö [a-%c] ¤Á´«³]©w¡A«ö [Return] µ²§ô¡G", maxc);

	dirty++;

	switch (tolower(key)) {
	case 'a':{
	    int     currentset = cuser.uflag2 & WATER_MASK;
	    currentset = (currentset + 1) % 3;
	    cuser.uflag2 &= ~WATER_MASK;
	    cuser.uflag2 |= currentset;
	    vmsg("­×¥¿¤ô²y¼Ò¦¡«á½Ð¥¿±`Â÷½u¦A­«·s¤W½u");
	}
	    break;
	case 'b':
	    cuser.uflag2 ^= REJ_OUTTAMAIL;
	    break;
	case 'c':
	    cuser.uflag2 ^= FAVNEW_FLAG;
	    break;
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
	case 'e':
	    cuser.uflag2 ^= FAVNOHILIGHT;
	    break;
	case 'f':
	    cuser.uflag ^= MOVIE_FLAG;
	    break;

#ifdef PLAY_ANGEL
	case 'g':
	    if( HasUserPerm(PERM_ANGEL) ){
		SwitchBeingAngel();
	    }
	    else
		done = 1;
	    break;

	case 'h':
	    if( HasUserPerm(PERM_ANGEL) ){
		SwitchAngelSex(ANGEL_STATUS() + 1);
	    }
	    break;
#endif

#ifdef DBCSAWARE
	case 'i':
#if 0
	    /* Actually, you can't try detection here.
	     * this function (customization)was not designed with the ability
	     * to refresh itself.
	     */
	    if(key == 'I') // one more try
	    {
		if(u_detectDBCSAwareEvilClient())
		    cuser.uflag &= ~DBCSAWARE_FLAG;
		else
		    cuser.uflag |= DBCSAWARE_FLAG;

	    } else
#endif
		cuser.uflag ^= DBCSAWARE_FLAG;
	    break;
#endif

	default:
	    dirty --;
	    done = 1;
	    break;
	}
    }
    if(dirty)
	passwd_update(usernum, &cuser);
    vmsg("³]©w§¹¦¨");
}

void
uinfo_query(userec_t *u, int adminmode, int unum)
{
    userec_t        x;
    register int    i = 0, fail, mail_changed;
    int             uid, ans;
    char            buf[STRLEN], *p;
    char            genbuf[200], reason[50];
    int money = 0;
    int             flag = 0, temp = 0, money_change = 0;

    fail = mail_changed = 0;

    memcpy(&x, u, sizeof(userec_t));
    ans = getans(adminmode ?
	    "(1)§ï¸ê®Æ(2)³]±K½X(3)³]Åv­­(4)¬å±b¸¹(5)§ïID"
	    "(6)±þ/´_¬¡Ãdª«(7)¼f§P [0]µ²§ô " :
	    "½Ð¿ï¾Ü (1)­×§ï¸ê®Æ (2)³]©w±K½X (C) ­Ó¤H¤Æ³]©w ==> [0]µ²§ô ");

    if (ans > '2' && ans != 'C' && ans != 'c' && !adminmode)
	ans = '0';

    if (ans == '1' || ans == '3') {
	clear();
	i = 1;
	move(i++, 0);
	outs(msg_uid);
	outs(x.userid);
    }
    switch (ans) {
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
	if (adminmode) {
	    getdata_buf(i++, 0, "¯u¹ê©m¦W¡G",
			x.realname, sizeof(x.realname), DOECHO);
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
	    mail_changed = 1 - adminmode;
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
	    if (!adminmode && (x.month > 12 || x.month < 1 || x.day > 31 ||
			  x.day < 1 || x.year > 90 || x.year < 40))
		continue;
	    i++;
	    break;
	}

#ifdef PLAY_ANGEL
	if (adminmode)
	    while (1) {
	        userec_t xuser;
		getdata_str(i, 0, "¤p¤Ñ¨Ï¡G", buf, IDLEN + 1, DOECHO,
			x.myangel);
		if(buf[0] == 0 || (getuser(buf, &xuser) &&
			    (xuser.userlevel & PERM_ANGEL))){
		    strlcpy(x.myangel, xuser.userid, IDLEN + 1);
		    ++i;
		    break;
		}
	    }
#endif

#ifdef CHESSCOUNTRY
	{
	    int j, k;
	    FILE* fp;
	    for(j = 0; j < 2; ++j){
		sethomefile(genbuf, u->userid, chess_photo_name[j]);
		fp = fopen(genbuf, "r");
		if(fp != NULL){
		    FILE* newfp;
		    char mybuf[200];
		    for(k = 0; k < 11; ++k)
			fgets(genbuf, 200, fp);
		    fgets(genbuf, 200, fp);
		    chomp(genbuf);

		    snprintf(mybuf, 200, "%s´Ñ°ê¦Û§Ú´y­z¡G", chess_type[j]);
		    getdata_buf(i, 0, mybuf, genbuf + 11, 80 - 11, DOECHO);
		    ++i;

		    sethomefile(mybuf, u->userid, chess_photo_name[j]);
		    strcat(mybuf, ".new");
		    if((newfp = fopen(mybuf, "w")) != NULL){
			rewind(fp);
			for(k = 0; k < 11; ++k){
			    fgets(mybuf, 200, fp);
			    fputs(mybuf, newfp);
			}
			fputs(genbuf, newfp);
			fputc('\n', newfp);

			fclose(newfp);

			sethomefile(genbuf, u->userid, chess_photo_name[j]);
			sethomefile(mybuf, u->userid, chess_photo_name[j]);
			strcat(mybuf, ".new");
			
			Rename(mybuf, genbuf);
		    }
		    fclose(fp);
		}
	    }
	}
#endif

	if (adminmode) {
	    int l;
	    if (HasUserPerm(PERM_BBSADM)) {
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

	    // XXX ¤@ÅÜ¼Æ¤£­n¦h¥Î³~¶Ã¥Î "fail"
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
	    if (getdata_str(i++, 0, "¦í¦b 1)¥xÆW 2)¨ä¥L¡G", buf, 2, DOECHO, x.uflag2 & FOREIGN ? "2" : "1"))
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
	if (!adminmode) {
	    if (!getdata(i++, 0, "½Ð¿é¤J­ì±K½X¡G", buf, PASSLEN, NOECHO) ||
		!checkpasswd(u->passwd, buf)) {
		outs("\n\n±z¿é¤Jªº±K½X¤£¥¿½T\n");
		fail++;
		break;
	    }
	} else {
            FILE *fp;
	    char  witness[3][32], title[100];
	    for (i = 0; i < 3; i++) {
		if (!getdata(19 + i, 0, "½Ð¿é¤J¨ó§UÃÒ©ú¤§¨Ï¥ÎªÌ¡G",
			     witness[i], sizeof(witness[i]), DOECHO)) {
		    outs("\n¤£¿é¤J«hµLªk§ó§ï\n");
		    fail++;
		    break;
		} else if (!(uid = searchuser(witness[i], NULL))) {
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

            i = 20;
	    sprintf(title, "%s ªº±K½X­«³]³qª¾ (by %s)",u->userid, cuser.userid);
            unlink("etc/updatepwd.log");
            if(! (fp = fopen("etc/updatepwd.log", "w")))
                     break;

            fprintf(fp, "%s ­n¨D±K½X­«³]:\n"
                        "¨£ÃÒ¤H¬° %s, %s, %s",
                         u->userid, witness[0], witness[1], witness[2] );
            fclose(fp);

            post_file("Security", title, "etc/updatepwd.log", "[¨t²Î¦w¥þþ§½]");
	    mail_id(u->userid, title, "etc/updatepwd.log", cuser.userid);
	    for(i=0; i<3; i++)
	     {
	       mail_id(witness[i], title, "etc/updatepwd.log", cuser.userid);
             }
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
	if (adminmode)
	    x.userlevel &= (~PERM_LOGINOK);
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
	    if (searchuser(genbuf, NULL)) {
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
    if (getans(msg_sure_ny) == 'y') {
	if (flag) {
	    post_change_perm(temp, i, cuser.userid, x.userid);
#ifdef PLAY_ANGEL
	    if (i & ~temp & PERM_ANGEL)
		mail_id(x.userid, "¯Í»Hªø¥X¨Ó¤F¡I", "etc/angel_notify", "[¤W«Ò]");
#endif
	}
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

	    sethomepath(src, x.userid);
	    snprintf(dst, sizeof(dst), "tmp/%s", x.userid);
	    friend_delete_all(x.userid, FRIEND_ALOHA);
	    Rename(src, dst);	/* do not remove user home */
            kill_user(unum);
	    return;
	} else
	    log_usies("SetUser", x.userid);
	if (money_change)
	    setumoney(unum, x.money);
	passwd_update(unum, &x);
	if (money_change) {
	    char title[TTLEN+1];
	    char msg[200];
	    clrtobot();
	    clear();
	    // XXX ¦¹®ÉÂ_½u«h­×§ï¸ê®Æ¨S log
	    while (!getdata(5, 0, "½Ð¿é¤J²z¥Ñ¥H¥Ü­t³d¡G",
			    reason, sizeof(reason), DOECHO));

	    snprintf(msg, sizeof(msg),
		    "   ¯¸ªø" ANSI_COLOR(1;32) "%s" ANSI_RESET "§â" ANSI_COLOR(1;32) "%s" ANSI_RESET "ªº¿ú"
		    "±q" ANSI_COLOR(1;35) "%d" ANSI_RESET "§ï¦¨" ANSI_COLOR(1;35) "%d" ANSI_RESET "\n"
		    "   " ANSI_COLOR(1;37) "¯¸ªø%s­×§ï¿ú²z¥Ñ¬O¡G%s" ANSI_RESET,
		    cuser.userid, x.userid, money, x.money,
		    cuser.userid, reason);
	    snprintf(title, sizeof(title),
		    "[¤½¦w³ø§i] ¯¸ªø%s­×§ï%s¿ú³ø§i", cuser.userid,
		    x.userid);
	    post_msg("Security", title, msg, "[¨t²Î¦w¥þ§½]");
	}
    }
}

int
u_info(void)
{
    move(2, 0);
    user_display(&cuser, 0);
    uinfo_query(&cuser, 0, usernum);
    strlcpy(currutmp->username, cuser.username, sizeof(currutmp->username));
    return 0;
}

int
u_cloak(void)
{
    outs((currutmp->invisible ^= 1) ? MSG_CLOAKED : MSG_UNCLOAK);
    return XEASY;
}

void
showplans(const char *uid)
{
    char            genbuf[200];

#ifdef CHESSCOUNTRY
    if (user_query_mode) {
	int    i = 0;
	FILE  *fp;
	userec_t xuser;

	sethomefile(genbuf, uid, chess_photo_name[user_query_mode - 1]);
	if ((fp = fopen(genbuf, "r")) != NULL)
	{
	    char   photo[6][256];
	    int    kingdom_bid = 0;
	    int    win = 0, lost = 0;

	    move(7, 0);
	    while (i < 12 && fgets(genbuf, 256, fp))
	    {
		chomp(genbuf);
		if (i < 6)  /* Åª·Ó¤ùÀÉ */
		    strcpy(photo[i], genbuf);
		else if (i == 6)
		    kingdom_bid = atoi(genbuf);
		else
		    prints("%s %s\n", photo[i - 7], genbuf);

		i++;
	    }

	    getuser(uid, &xuser);
	    if (user_query_mode == 1) {
		win = xuser.five_win;
		lost = xuser.five_lose;
	    } else if(user_query_mode == 2) {
		win = xuser.chc_win;
		lost = xuser.chc_lose;
	    }
	    prints("%s <Á`¦@¾ÔÁZ> %d ³Ó %d ±Ñ\n", photo[5], win, lost);


	    /* ´Ñ°ê°êÀ² */
	    setapath(genbuf, bcache[kingdom_bid - 1].brdname);
	    strlcat(genbuf, "/chess_ensign", sizeof(genbuf));
	    show_file(genbuf, 13, 10, ONLY_COLOR);
	    return;
	}
    }
#endif /* defined(CHESSCOUNTRY) */

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
	    prints(ANSI_COLOR(36) "¡i Ã±¦WÀÉ.%c ¡j" ANSI_RESET "\n", ch);
	    for (i = 0; i < MAX_SIGLINES && fgets(buf, sizeof(buf), fp); i++)
		outs(buf);
	    num++;
	    fclose(fp);
	}
    }
    return num;
}

int
u_editsig(void)
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
u_editplan(void)
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
u_editcalendar(void)
{
    char            genbuf[200];

    getdata(b_lines - 1, 0, "¦æ¨Æ¾ä (D)§R°£ (E)½s¿è [Q]¨ú®ø¡H[Q] ",
	    genbuf, 3, LCECHO);

    if (genbuf[0] == 'e') {
	int             aborted;

	setutmpmode(EDITPLAN);
	sethomefile(genbuf, cuser.userid, "calendar");
	aborted = vedit(genbuf, NA, NULL);
	if (aborted != -1)
	    vmsg("¦æ¨Æ¾ä§ó·s§¹²¦");
	return 0;
    } else if (genbuf[0] == 'd') {
	sethomefile(genbuf, cuser.userid, "calendar");
	unlink(genbuf);
	vmsg("¦æ¨Æ¾ä§R°£§¹²¦");
    }
    return 0;
}

/* ¨Ï¥ÎªÌ¶ñ¼gµù¥Uªí®æ */
static void
getfield(int line, const char *info, const char *desc, char *buf, int len)
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

static char    *
getregcode(unsigned char *buf)
{
    unsigned char *uid = &cuser.userid[0];
    int i;

    /* init seed with magic */
    strncpy(buf, REGCODE_MAGIC, 13); /* des keys are only 13 byte */
    buf[13] = 0;

    /* scramble with user id */
    for (i = 0; i < IDLEN && uid[i]; i++)
    {
	buf[i] ^= uid[i];
	while (!(buf[i] >= '0' && buf[i] <= 'z'))
	{
	    buf[i] = (buf[i] + '0') & 0xff;
	    buf[i+1] = (buf[i+1] + 0x17) & 0xff;
	}
    }
    /* leave last character untouched anyway */
    buf[13] = 0;

    /* real encryption */
    strcpy(buf, crypt(buf, "pt"));
    /* hack to prevent trailing dots */
    if (buf[strlen(buf)-1] == '.')
	buf[strlen(buf)-1] = 'd';
    return buf;
}

#ifdef DEBUG
int
_debug_testregcode()
{
    char buf[16], rcode[16];
    char myid[16];
    int i = 1;

    clear();
    strcpy(myid, cuser.userid);
    do {
	getdata(0, 0, "¿é¤J id (ªÅ¥Õµ²§ô): ",
		buf, IDLEN+1, DOECHO);
	if(buf[0])
	{
	    move(i++, 0);
	    i %= t_lines;
	    if(i == 0)
		i = 1;
	    strcpy(cuser.userid, buf);
	    prints("id: [%s], regcode: [%s]\n",
		    cuser.userid, getregcode(rcode));
	    move(i, 0);
	    clrtoeol();
	}
    } while (buf[0]);
    strcpy(cuser.userid, myid);

    pressanykey();
    return 0;
}
#endif

static int
isvalidemail(const char *email)
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
	    chomp(buf);
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

    sethomefile(buf, cuser.userid, "justify.wait");
    if (phone[0] != 0) {
	fn = fopen(buf, "w");
	assert(fn);
	fprintf(fn, "%s\n%s\ndummy\n%s\n%s\n%s\n",
		phone, career, rname, addr, mobile);
	fclose(fn);
    }
    clear();
    stand_title("»{ÃÒ³]©w");
    if (cuser.userlevel & PERM_NOREGCODE){
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
		outs("«ü©wªº¤â¾÷¸¹½X¤£¦Xªk,"
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
	    outs("«ü©wªº E-Mail ¤£¦Xªk,"
		   "­Y±zµL E-Mail ½Ð¿é¤J x¥Ñ¯¸ªø¤â°Ê»{ÃÒ");
	}
    }
    strncpy(cuser.email, email, sizeof(cuser.email));
 REGFORM2:
    if (strcasecmp(email, "x") == 0) {	/* ¤â°Ê»{ÃÒ */
	if ((fn = fopen(fn_register, "a"))) {
	    fprintf(fn, "num: %d, %s", usernum, ctime4(&now));
	    fprintf(fn, "uid: %s\n", cuser.userid);
	    fprintf(fn, "ident: \n");
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
	    strncpy(cuser.justify, genbuf, REGLEN);
	    sethomefile(buf, cuser.userid, "justify");
	}

	/* 
	 * It is intended to use BBSENAME instead of BBSNAME here.
	 * Because recently many poor users with poor mail clients
	 * (or evil mail servers) cannot handle/decode Chinese 
	 * subjects (BBSNAME) correctly, so we'd like to use 
	 * BBSENAME here to prevent subject being messed up.
	 * And please keep BBSENAME short or it may be truncated
	 * by evil mail servers.
	 */
	snprintf(buf, sizeof(buf),
		 " " BBSENAME " - [ %s ]", getregcode(genbuf));

	strlcpy(tmp, cuser.userid, sizeof(tmp));
	strlcpy(cuser.userid, str_sysop, sizeof(cuser.userid));
#ifdef HAVEMOBILE
	if (strcmp(email, "m") == 0 || strcmp(email, "M") == 0)
	    mobile_message(mobile, buf);
	else
#endif
	    bsmtp("etc/registermail", buf, email, 0);
	strlcpy(cuser.userid, tmp, sizeof(cuser.userid));
	outs("\n\n\n§Ú­Ì§Y±N±H¥X»{ÃÒ«H (±zÀ³¸Ó·|¦b 10 ¤ÀÄÁ¤º¦¬¨ì)\n"
	     "¦¬¨ì«á±z¥i¥H®Ú¾Ú»{ÃÒ«H¼ÐÃDªº»{ÃÒ½X\n"
	     "¿é¤J¨ì (U)ser -> (R)egister «á´N¥i¥H§¹¦¨µù¥U");
	pressanykey();
	return;
    }
}

static int HaveRejectStr(const char *s, const char **rej)
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
    const char    *rejectstr[] =
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
    const char    *rejectstr[] = {NULL};
    if (!(removespace(career) && career[0] < 0 && strlen(career) >= 6) ||
	strcmp(career, "®a¸Ì") == 0 || HaveRejectStr(career, rejectstr) )
	return "±zªº¿é¤J¤£¥¿½T";
    if (strcmp(&career[strlen(career) - 2], "¤j") == 0 ||
	strcmp(&career[strlen(career) - 4], "¤j¾Ç") == 0 ||
	strcmp(career, "¾Ç¥Í¤j¾Ç") == 0)
	return "³Â·Ð½Ð¥[¾Ç®Õ¨t©Ò";
    if (strcmp(career, "¾Ç¥Í°ª¤¤") == 0)
	return "³Â·Ð¿é¤J¾Ç®Õ¦WºÙ";
#else
    if( strlen(career) < 6 )
	return "±zªº¿é¤J¤£¥¿½T";
#endif
    return NULL;
}

static char *isvalidaddr(char *addr)
{
    const char    *rejectstr[] =
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
	if( !isdigit((int)phone[i]) )
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
    char            rname[20], addr[50], ident[11], mobile[16];
#ifdef FOREIGN_REG
    char            fore[2];
#endif
    char            phone[20], career[40], email[50], birthday[9], sex_is[2],
                    year, mon, day;
    char            inregcode[14], regcode[50];
    char            ans[3], *ptr, *errcode;
    char            genbuf[200];
    FILE           *fn;

    if (cuser.userlevel & PERM_LOGINOK) {
	outs("±zªº¨­¥÷½T»{¤w¸g§¹¦¨¡A¤£»Ý¶ñ¼g¥Ó½Ðªí");
	return XEASY;
    }
    if ((fn = fopen(fn_register, "r"))) {
	while (fgets(genbuf, STRLEN, fn)) {
	    if ((ptr = strchr(genbuf, '\n')))
		*ptr = '\0';
	    if (strncmp(genbuf, "uid: ", 5) == 0 &&
		strcmp(genbuf + 5, cuser.userid) == 0) {
		fclose(fn);
		outs("±zªºµù¥U¥Ó½Ð³æ©|¦b³B²z¤¤¡A½Ð­@¤ßµ¥­Ô");
		return XEASY;
	    }
	}
	fclose(fn);
    }
    strlcpy(rname, cuser.realname, sizeof(rname));
    strlcpy(addr, cuser.address, sizeof(addr));
    strlcpy(email, cuser.email, sizeof(email));
    snprintf(mobile, sizeof(mobile), "0%09d", cuser.mobile);
    if (cuser.month == 0 && cuser.day && cuser.year == 0)
	birthday[0] = 0;
    else
	snprintf(birthday, sizeof(birthday), "%02i/%02i/%02i",
		 cuser.month, cuser.day, cuser.year % 100);
    sex_is[0] = (cuser.sex % 8) + '1';
    sex_is[1] = 0;
    career[0] = phone[0] = '\0';
    sethomefile(genbuf, cuser.userid, "justify.wait");
    if ((fn = fopen(genbuf, "r"))) {
	fgets(genbuf, sizeof(genbuf), fn);
	chomp(genbuf);
	strlcpy(phone, genbuf, sizeof(phone));

	fgets(genbuf, sizeof(genbuf), fn);
	chomp(genbuf);
	strlcpy(career, genbuf, sizeof(career));

	fgets(genbuf, sizeof(genbuf), fn); // old version compatible

	fgets(genbuf, sizeof(genbuf), fn);
	chomp(genbuf);
	strlcpy(rname, genbuf, sizeof(rname));

	fgets(genbuf, sizeof(genbuf), fn);
	chomp(genbuf);
	strlcpy(addr, genbuf, sizeof(addr));

	fgets(genbuf, sizeof(genbuf), fn);
	chomp(genbuf);
	strlcpy(mobile, genbuf, sizeof(mobile));

	fclose(fn);
    }

    if (cuser.userlevel & PERM_NOREGCODE) {
	vmsg("±z¤£³Q¤¹³\\¨Ï¥Î»{ÃÒ½X»{ÃÒ¡C½Ð¶ñ¼gµù¥U¥Ó½Ð³æ");
	goto REGFORM;
    }

    if (cuser.year != 0 &&	/* ¤w¸g²Ä¤@¦¸¶ñ¹L¤F~ ^^" */
	strcmp(cuser.email, "x") != 0 &&	/* ¤W¦¸¤â°Ê»{ÃÒ¥¢±Ñ */
	strcmp(cuser.email, "X") != 0) {
	clear();
	stand_title("EMail»{ÃÒ");
	move(2, 0);
	prints("%s(%s) ±z¦n¡A½Ð¿é¤J±zªº»{ÃÒ½X¡C\n"
	       "©Î±z¥i¥H¿é¤J x ¨Ó­«·s¶ñ¼g E-Mail ©Î§ï¥Ñ¯¸ªø¤â°Ê»{ÃÒ\n",
	       cuser.userid, cuser.username);
	inregcode[0] = 0;

	do{
	    getdata(10, 0, "±zªº¿é¤J: ", inregcode, sizeof(inregcode), DOECHO);
	    if (inregcode[0] == '0' && inregcode[1] == '2')
	    {
		/* old regcode */
		vmsg("±z¿é¤Jªº»{ÃÒ½X¦]¨t²Îª@¯Å¤w¥¢®Ä¡A"
			"½Ð¿é¤J x ­«¶ñ¤@¦¸ E-Mail");
	    } else
	    if( strcmp(inregcode, "x") == 0 ||
		strcmp(inregcode, "X") == 0 ||
		strlen(inregcode) == 13 )
		break;
	    if( strlen(inregcode) != 13 )
		vmsg("»{ÃÒ½X¿é¤J¤£§¹¥þ¡AÀ³¸Ó¤@¦@¦³¤Q¤T½X¡C");
	} while( 1 );

	if (strcmp(inregcode, getregcode(regcode)) == 0) {
	    int             unum;
	    if ((unum = searchuser(cuser.userid, NULL)) == 0) {
		vmsg("¨t²Î¿ù»~¡A¬dµL¦¹¤H¡I");
		u_exit("getuser error");
		exit(0);
	    }
	    mail_muser(cuser, "[µù¥U¦¨¥\\Åo]", "etc/registeredmail");
#if FOREIGN_REG_DAY > 0
	    if(cuser.uflag2 & FOREIGN)
		mail_muser(cuser, "[¥X¤J¹ÒºÞ²z§½]", "etc/foreign_welcome");
#endif
	    cuser.userlevel |= (PERM_LOGINOK | PERM_POST);
	    outs("\nµù¥U¦¨¥\\, ­«·s¤W¯¸«á±N¨ú±o§¹¾ãÅv­­\n"
		   "½Ð«ö¤U¥ô¤@Áä¸õÂ÷«á­«·s¤W¯¸~ :)");
	    sethomefile(genbuf, cuser.userid, "justify.wait");
	    unlink(genbuf);
	    snprintf(cuser.justify, sizeof(cuser.justify),
		     "%s:%s:auto", phone, career);
	    sethomefile(genbuf, cuser.userid, "justify");
	    log_file(genbuf, LOG_CREAT, cuser.justify);
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
	    ans, 3, LCECHO);
    if (ans[0] != 'y')
	return FULLUPDATE;

    move(2, 0);
    clrtobot();
    while (1) {
	clear();
	move(1, 0);
	prints("%s(%s) ±z¦n¡A½Ð¾Ú¹ê¶ñ¼g¥H¤Uªº¸ê®Æ:",
	       cuser.userid, cuser.username);
#ifdef FOREIGN_REG
	fore[0] = 'y';
	fore[1] = 0;
	getfield(2, "Y/n", "¬O§_²{¦b¦í¦b¥xÆW", fore, 2);
    	if (fore[0] == 'n')
	    fore[0] |= FOREIGN;
	else
	    fore[0] = 0;
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
	outs("  ½ÐºÉ¶q¸Ô²Óªº¶ñ¼g±zªºªA°È³æ¦ì¡A¤j±M°|®Õ½Ð³Â·Ð"
	     "¥[" ANSI_COLOR(1;33) "¨t©Ò" ANSI_RESET "¡A¤½¥q³æ¦ì½Ð¥[" ANSI_COLOR(1;33) "Â¾ºÙ" ANSI_RESET "¡A\n"
	     "  ¼ÈµL¤u§@½Ð³Â·Ð¶ñ¼g" ANSI_COLOR(1;33) "²¦·~¾Ç®Õ" ANSI_RESET "¡C\n");
	while (1) {
	    getfield(9, "(²¦·~)¾Ç®Õ(§t" ANSI_COLOR(1;33) "¨t©Ò¦~¯Å" ANSI_RESET ")©Î³æ¦ìÂ¾ºÙ",
		     "ªA°È³æ¦ì", career, 40);
	    if( (errcode = isvalidcareer(career)) == NULL )
		break;
	    else
		vmsg(errcode);
	}
	while (1) {
	    getfield(11, "§t" ANSI_COLOR(1;33) "¿¤¥«" ANSI_RESET "¤Îªù¹ì¸¹½X"
		     "(¥x¥_½Ð¥[" ANSI_COLOR(1;33) "¦æ¬F°Ï" ANSI_RESET ")",
		     "¥Ø«e¦í§}", addr, sizeof(addr));
	    if( (errcode = isvalidaddr(addr)) == NULL
#ifdef FOREIGN_REG
                || fore[0] 
#endif
		)
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
		snprintf(birthday, 9, "%02i/%02i/%02i",
			 cuser.month, cuser.day, cuser.year % 100);
		mon = cuser.month;
		day = cuser.day;
		year = cuser.year;
	    } else if (len == 8) {
		mon = (birthday[0] - '0') * 10 + (birthday[1] - '0');
		day = (birthday[3] - '0') * 10 + (birthday[4] - '0');
		year = (birthday[6] - '0') * 10 + (birthday[7] - '0');
	    } else{
		vmsg("±zªº¿é¤J¤£¥¿½T");
		continue;
	    }
	    if (mon > 12 || mon < 1 || day > 31 || day < 1 || 
		year < 40){
		vmsg("±zªº¿é¤J¤£¥¿½T");
		continue;
	    }
	    break;
	}
	getfield(19, "1.¸¯®æ 2.©j±µ ", "©Ê§O", sex_is, 2);
	getdata(20, 0, "¥H¤W¸ê®Æ¬O§_¥¿½T(Y/N)¡H(Q)¨ú®øµù¥U [N] ",
		ans, 3, LCECHO);
	if (ans[0] == 'q')
	    return 0;
	if (ans[0] == 'y')
	    break;
    }
    strlcpy(cuser.realname, rname, sizeof(cuser.realname));
    strlcpy(cuser.address, addr, sizeof(cuser.address));
    strlcpy(cuser.email, email, sizeof(cuser.email));
    cuser.mobile = atoi(mobile);
    cuser.sex = (sex_is[0] - '1') % 8;
    cuser.month = mon;
    cuser.day = day;
    cuser.year = year;
#ifdef FOREIGN_REG
    if (fore[0])
	cuser.uflag2 |= FOREIGN;
    else
	cuser.uflag2 &= ~FOREIGN;
#endif
    trim(career);
    trim(addr);
    trim(phone);

    toregister(email, genbuf, phone, career, ident, rname, addr, mobile);

    clear();
    move(9, 3);
    outs("³Ì«áPost¤@½g" ANSI_COLOR(32) "¦Û§Ú¤¶²Ð¤å³¹" ANSI_RESET "µ¹¤j®a§a¡A"
	   "§i¶D©Ò¦³¦Ñ°©ÀY" ANSI_COLOR(31) "§Ú¨Ó°Õ^$¡C\\n\n\n\n");
    pressanykey();
    cuser.userlevel |= PERM_POST;
    brc_initial_board("WhoAmI");
    set_board();
    do_post();
    cuser.userlevel &= ~PERM_POST;
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
	prints(ANSI_COLOR(7) "  ¨Ï¥ÎªÌ¥N¸¹   %-25s   ¤W¯¸  ¤å³¹  %s  "
	       "³Ìªñ¥úÁ{¤é´Á     " ANSI_COLOR(0) "\n",
	       "ºï¸¹¼ÊºÙ",
	       HasUserPerm(PERM_SEEULEVELS) ? "µ¥¯Å" : "");
	i = 3;
	return 0;
    }
    if (bad_user_id(uentp->userid))
	return 0;

    if ((uentp->userlevel & ~(u_list_special)) == 0)
	return 0;

    if (i == b_lines) {
	prints(ANSI_COLOR(34;46) "  ¤wÅã¥Ü %d/%d ¤H(%d%%)  " ANSI_COLOR(31;47) "  "
	       "(Space)" ANSI_COLOR(30) " ¬Ý¤U¤@­¶  " ANSI_COLOR(31) "(Q)" ANSI_COLOR(30) " Â÷¶}  " ANSI_RESET,
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
    strlcpy(permstr, "----", 8);
    if (level & PERM_SYSOP)
	permstr[0] = 'S';
    else if (level & PERM_ACCOUNTS)
	permstr[0] = 'A';
    else if (level & PERM_SYSOPHIDE)
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
	   HasUserPerm(PERM_SEEULEVELS) ? permstr : "", ptr);
    usercounter++;
    i++;
    return 0;
}

int
u_list(void)
{
    char            genbuf[3];

    setutmpmode(LAUSERS);
    u_list_special = usercounter = 0;
    totalusers = SHM->number;
    if (HasUserPerm(PERM_SEEULEVELS)) {
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
    prints(ANSI_COLOR(34;46) "  ¤wÅã¥Ü %d/%d ªº¨Ï¥ÎªÌ(¨t²Î®e¶qµL¤W­­)  "
	   ANSI_COLOR(31;47) "  (½Ð«ö¥ô·NÁäÄ~Äò)  " ANSI_RESET, usercounter, totalusers);
    igetch();
    return 0;
}

#ifdef DBCSAWARE

/* detect if user is using an evil client that sends double
 * keys for DBCS data.
 * True if client is evil.
 */

int u_detectDBCSAwareEvilClient()
{
    int ret = 0;

    clear();
    move(1, 0);
    outs(ANSI_RESET
	    "* ¥»¯¸¤ä´©¦Û°Ê°»´ú¤¤¤å¦rªº²¾°Ê»P½s¿è¡A¦ý¦³¨Ç³s½uµ{¦¡(¦pxxMan)\n"
	    "  ·|¦Û¦æ³B²z¡B¦h°e«öÁä¡A©ó¬O«K·|³y¦¨" ANSI_COLOR(1;37)
	    "¤@¦¸²¾°Ê¨â­Ó¤¤¤å¦rªº²{¶H¡C" ANSI_RESET "\n\n"
	    "* Åý³s½uµ{¦¡³B²z²¾°Ê®e©ö³y¦¨³\\¦h"
	    "Åã¥Ü¤Î²¾°Ê¤Wªº°ÝÃD¡A©Ò¥H§Ú­Ì«ØÄ³±z\n"
	    "  Ãö³¬¸Óµ{¦¡¤Wªº¦¹¶µ³]©w¡]³q±`¥s¡u°»´ú(¥þ«¬©ÎÂù¦ì¤¸²Õ)¤¤¤å¡v¡^¡A\n"
	    "  Åý BBS ¨t²Î¥i¥H¥¿½Tªº±±¨î§Aªºµe­±¡C\n\n"
	    ANSI_COLOR(1;33) 
	    "* ¦pªG±z¬Ý¤£À´¤W­±ªº»¡©ú¤]µL©Ò¿×¡A§Ú­Ì·|¦Û°Ê°»´ú¾A¦X±zªº³]©w¡C"
	    ANSI_RESET "\n"
	    "  ½Ð¦b³]©w¦n³s½uµ{¦¡¦¨±z°¾¦nªº¼Ò¦¡«á«ö" ANSI_COLOR(1;33)
	    "¤@¤U" ANSI_RESET "±zÁä½L¤Wªº" ANSI_COLOR(1;33)
	    "¡ö" ANSI_RESET "\n" ANSI_COLOR(1;36)
	    "  (¥t¥~¥ª¥k¤è¦VÁä©Î¼g BS/Backspace ªº­Ë°hÁä»P Del §R°£Áä§¡¥i)\n"
	    ANSI_RESET);

    /* clear buffer */
    while(num_in_buf() > 0)
	igetch();

    while (1)
    {
	int ch = 0;

	move(12, 0);
	outs("³o¬O°»´ú°Ï¡A±zªº´å¼Ð·|¥X²{¦b" 
		ANSI_COLOR(7) "³o¸Ì" ANSI_RESET);
	move(12, 15*2);
	ch = igetch();
	if(ch != KEY_LEFT && ch != KEY_RIGHT &&
		ch != Ctrl('H') && ch != '\177')
	{
	    move(14, 0);
	    outs("½Ð«ö¤@¤U¤W­±«ü©wªºÁä¡I §A«ö¨ì§OªºÁä¤F¡I");
	} else {
	    move(16, 0);
	    /* Actually you may also use num_in_buf here.  those clients
	     * usually sends doubled keys together in one packet.
	     * However when I was writing this, a bug (existed for more than 3
	     * years) of num_in_buf forced me to write new wait_input.
	     * Anyway it is fixed now.
	     */
	    if(wait_input(0.1, 1))
	    // if(igetch() == ch)
	    // if (num_in_buf() > 0)
	    {
		/* evil dbcs aware client */
		outs("°»´ú¨ì±zªº³s½uµ{¦¡·|¦Û¦æ³B²z´å¼Ð²¾°Ê¡C\n\n"
			// "­Y¤é«á¦]¦¹³y¦¨ÂsÄý¤Wªº°ÝÃD¥»¯¸®¤¤£³B²z¡C\n\n"
			"¤w³]©w¬°¡uÅý±zªº³s½uµ{¦¡³B²z´å¼Ð²¾°Ê¡v\n");
		ret = 1;
	    } else {
		/* good non-dbcs aware client */
		outs("±zªº³s½uµ{¦¡¦ü¥G¤£·|¦h°e«öÁä¡A"
			"³o¼Ë BBS ¥i¥H§óºë·Çªº±±¨îµe­±¡C\n\n"
			"¤w³]©w¬°¡uÅý BBS ¦øªA¾¹ª½±µ³B²z´å¼Ð²¾°Ê¡v\n");
		ret = 0;
	    }
	    outs(  "\n­Y·Q§ïÅÜ³]©w½Ð¦Ü ­Ó¤H³]©w°Ï ¡÷ ­Ó¤H¤Æ³]©w ¡÷ \n"
		   "    ½Õ¾ã¡u¦Û°Ê°»´úÂù¦ì¤¸¦r¶°(¦p¥þ«¬¤¤¤å)¡v¤§³]©w");
	    while(num_in_buf())
		igetch();
	    break;
	}
    }
    pressanykey();
    return ret;
}
#endif 

/* vim:sw=4
 */
