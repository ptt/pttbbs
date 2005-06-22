/* $Id$ */
#include "bbs.h"

/* ¶i¯¸¤ô²y«Å¶Ç */
int
m_loginmsg(void)
{
  char msg[100];
  move(21,0);
  clrtobot();
  if(SHM->loginmsg.pid && SHM->loginmsg.pid != currutmp->pid)
    {
      outs("¥Ø«e¤w¸g¦³¥H¤Uªº ¶i¯¸¤ô²y³]©w½Ð¥ý¨ó½Õ¦n¦A³]©w..");
      getmessage(SHM->loginmsg);
    }
  getdata(22, 0, 
     "¶i¯¸¤ô²y:¥»¯¸¬¡°Ê,¤£¤zÂZ¨Ï¥ÎªÌ¬°­­,³]©wªÌÂ÷¯¸¦Û°Ê¨ú®ø,½T©w­n³]?(y/N)",
          msg, 3, LCECHO);

  if(msg[0]=='y' &&

     getdata_str(23, 0, "³]©w¶i¯¸¤ô²y:", msg, 56, DOECHO, SHM->loginmsg.last_call_in))
    {
          SHM->loginmsg.pid=currutmp->pid; /*¯¸ªø¤£¦h ´N¤£ºÞrace condition */
          strcpy(SHM->loginmsg.last_call_in, msg);
          strcpy(SHM->loginmsg.userid, cuser.userid);
    }
  return 0;
}

/* ¨Ï¥ÎªÌºÞ²z */
int
m_user(void)
{
    userec_t        xuser;
    int             id;
    char            genbuf[200];

    stand_title("¨Ï¥ÎªÌ³]©w");
    usercomplete(msg_uid, genbuf);
    if (*genbuf) {
	move(2, 0);
	if ((id = getuser(genbuf, &xuser))) {
	    user_display(&xuser, 1);
	    uinfo_query(&xuser, 1, id);
	} else {
	    outs(err_uid);
	    clrtoeol();
	    pressanykey();
	}
    }
    return 0;
}

static int
search_key_user(const char *passwdfile, int mode)
{
    userec_t        user;
    int             ch;
    int             coun = 0;
    FILE            *fp1 = fopen(passwdfile, "r");
    char            friendfile[128]="", key[22], genbuf[8],
                    *keymatch;


    assert(fp1);
    clear();
    getdata(0, 0, mode ? "½Ð¿é¤J¨Ï¥ÎªÌÃöÁä¦r[¹q¸Ü|¦a§}|©m¦W|¨­¥÷ÃÒ|¤W¯¸¦aÂI|"
	    "email|¤pÂûid] :" : "½Ð¿é¤Jid :", key, sizeof(key), DOECHO);
    if(!key[0]) {
	fclose(fp1);
	return 0;
    }
    while ((fread(&user, sizeof(user), 1, fp1)) > 0 && coun < MAX_USERS) {
	if (!(++coun & 15)) {
	    move(1, 0);
	    prints("²Ä [%d] µ§¸ê®Æ\n", coun);
	    refresh();
	}
        keymatch = NULL;
	if (!strcasecmp(user.userid, key))
             keymatch = user.userid; 
        else if(mode) {
             if(strstr(user.realname, key))
                 keymatch = user.realname; 
             else if(strstr(user.username, key))
                 keymatch = user.username; 
             else if(strstr(user.lasthost, key))
                 keymatch = user.lasthost; 
             else if(strcasestr(user.email, key))
                 keymatch = user.email; 
             else if(strstr(user.address, key))
                 keymatch = user.address; 
             else if(strstr(user.justify, key))
                 keymatch = user.justify; 
             else if(strstr(user.mychicken.name, key))
                 keymatch = user.mychicken.name; 
	}
        if(keymatch) {
	    move(1, 0);
	    prints("²Ä [%d] µ§¸ê®Æ\n", coun);
	    refresh();

	    user_display(&user, 1);
	    uinfo_query(&user, 1, coun);
	    outs(ANSI_COLOR(44) "               ªÅ¥ÕÁä" \
		 ANSI_COLOR(37) ":·j´M¤U¤@­Ó          " \
		 ANSI_COLOR(33)" Q" ANSI_COLOR(37)": Â÷¶}");
	    outs(mode ? 
                 "      A: add to namelist " ANSI_RESET " " :
		 "      S: ¨ú¥Î³Æ¥÷¸ê®Æ    " ANSI_RESET " ");
	    while (1) {
		while ((ch = igetch()) == 0);
                if (ch == 'a' || ch=='A' )
                  {
                   if(!friendfile[0])
                    {
                     friend_special();
                     setfriendfile(friendfile, FRIEND_SPECIAL);
                    }
                   friend_add(user.userid, FRIEND_SPECIAL, keymatch);
                   break;
                  }
		if (ch == ' ')
		    break;
		if (ch == 'q' || ch == 'Q') {
		    fclose(fp1);
		    return 0;
		}
		if (ch == 's' && !mode) {
		    if ((ch = searchuser(user.userid, user.userid))) {
			setumoney(ch, user.money);
			passwd_update(ch, &user);
			fclose(fp1);
			return 0;
		    } else {
			getdata(0, 0,
				"¥Ø«eªº PASSWD ÀÉ¨S¦³¦¹ ID¡A·s¼W¶Ü¡H[y/N]",
				genbuf, 3, LCECHO);
			if (genbuf[0] != 'y') {
			    outs("¥Ø«eªºPASSWDSÀÉ¨S¦³¦¹id "
				 "½Ð¥ýnew¤@­Ó³o­Óidªº±b¸¹");
			} else {
			    int             allocid = getnewuserid();

			    if (allocid > MAX_USERS || allocid <= 0) {
				fprintf(stderr, "¥»¯¸¤H¤f¤w¹F¹¡©M¡I\n");
				exit(1);
			    }
			    if (passwd_update(allocid, &user) == -1) {
				fprintf(stderr, "«Èº¡¤F¡A¦A¨£¡I\n");
				exit(1);
			    }
			    setuserid(allocid, user.userid);
			    if (!searchuser(user.userid, NULL)) {
				fprintf(stderr, "µLªk«Ø¥ß±b¸¹\n");
				exit(1);
			    }
			    fclose(fp1);
			    return 0;
			}
		    }
		}
	    }
	}
    }

    fclose(fp1);
    return 0;
}

/* ¥H¥ô·N key ´M§ä¨Ï¥ÎªÌ */
int
search_user_bypwd(void)
{
    search_key_user(FN_PASSWD, 1);
    return 0;
}

/* ´M§ä³Æ¥÷ªº¨Ï¥ÎªÌ¸ê®Æ */
int
search_user_bybakpwd(void)
{
    char           *choice[] = {
	"PASSWDS.NEW1", "PASSWDS.NEW2", "PASSWDS.NEW3",
	"PASSWDS.NEW4", "PASSWDS.NEW5", "PASSWDS.NEW6",
	"PASSWDS.BAK"
    };
    int             ch;

    clear();
    move(1, 1);
    outs("½Ð¿é¤J§A­n¥Î¨Ó´M§ä³Æ¥÷ªºÀÉ®× ©Î«ö 'q' Â÷¶}\n");
    outs(" [" ANSI_COLOR(1;31) "1" ANSI_RESET "]¤@¤Ñ«e,"
	 " [" ANSI_COLOR(1;31) "2" ANSI_RESET "]¨â¤Ñ«e," 
	 " [" ANSI_COLOR(1;31) "3" ANSI_RESET "]¤T¤Ñ«e\n");
    outs(" [" ANSI_COLOR(1;31) "4" ANSI_RESET "]¥|¤Ñ«e,"
	 " [" ANSI_COLOR(1;31) "5" ANSI_RESET "]¤­¤Ñ«e,"
	 " [" ANSI_COLOR(1;31) "6" ANSI_RESET "]¤»¤Ñ«e\n");
    outs(" [7]³Æ¥÷ªº\n");
    do {
	move(5, 1);
	outs("¿ï¾Ü => ");
	ch = igetch();
	if (ch == 'q' || ch == 'Q')
	    return 0;
    } while (ch < '1' || ch > '7');
    ch -= '1';
    if( access(choice[ch], R_OK) != 0 )
	vmsg("ÀÉ®×¤£¦s¦b");
    else
	search_key_user(choice[ch], 0);
    return 0;
}

static void
bperm_msg(const boardheader_t * board)
{
    prints("\n³]©w [%s] ¬ÝªO¤§(%s)Åv­­¡G", board->brdname,
	   board->brdattr & BRD_POSTMASK ? "µoªí" : "¾\\Åª");
}

unsigned int
setperms(unsigned int pbits, const char * const pstring[])
{
    register int    i;

    move(4, 0);
    for (i = 0; i < NUMPERMS / 2; i++) {
	prints("%c. %-20s %-15s %c. %-20s %s\n",
	       'A' + i, pstring[i],
	       ((pbits >> i) & 1 ? "£¾" : "¢æ"),
	       i < 10 ? 'Q' + i : '0' + i - 10,
	       pstring[i + 16],
	       ((pbits >> (i + 16)) & 1 ? "£¾" : "¢æ"));
    }
    clrtobot();
    while (
       (i = getkey("½Ð«ö [A-5] ¤Á´«³]©w¡A«ö [Return] µ²§ô¡G"))!='\r')
    {
	if (isdigit(i))
	    i = i - '0' + 26;
	else if (isalpha(i))
	    i = tolower(i) - 'a';
	else {
	    bell();
	    continue;
	}

	pbits ^= (1 << i);
	move(i % 16 + 4, i <= 15 ? 24 : 64);
	outs((pbits >> i) & 1 ? "£¾" : "¢æ");
    }
    return pbits;
}

#ifdef CHESSCOUNTRY
static void
AddingChessCountryFiles(const char* apath)
{
    char filename[256];
    char symbolicname[256];
    char adir[256];
    FILE* fp;
    fileheader_t fh;

    setadir(adir, apath);

    /* creating chess country regalia */
    snprintf(filename, sizeof(filename), "%s/chess_ensign", apath);
    close(open(filename, O_CREAT | O_WRONLY, 0644));

    strlcpy(symbolicname, apath, sizeof(symbolicname));
    stampfile(symbolicname, &fh);
    symlink("chess_ensign", symbolicname);

    strcpy(fh.title, "¡º ´Ñ°ê°êÀ² (¤£¯à§R°£¡A¨t²Î»Ý­n)");
    strcpy(fh.owner, str_sysop);
    append_record(adir, &fh, sizeof(fileheader_t));

    /* creating member list */
    snprintf(filename, sizeof(filename), "%s/chess_list", apath);
    if (!dashf(filename)) {
	fp = fopen(filename, "w");
	assert(fp);
	fputs("´Ñ°ê°ê¦W\n"
		"±b¸¹            ¶¥¯Å    ¥[¤J¤é´Á        µ¥¯Å©Î³Q½Ö«R¸¸\n"
		"¢w¢w¢w¢w¢w¢w    ¢w¢w¢w  ¢w¢w¢w¢w¢w      ¢w¢w¢w¢w¢w¢w¢w\n",
		fp);
	fclose(fp);
    }

    strlcpy(symbolicname, apath, sizeof(symbolicname));
    stampfile(symbolicname, &fh);
    symlink("chess_list", symbolicname);

    strcpy(fh.title, "¡º ´Ñ°ê¦¨­ûªí (¤£¯à§R°£¡A¨t²Î»Ý­n)");
    strcpy(fh.owner, str_sysop);
    append_record(adir, &fh, sizeof(fileheader_t));

    /* creating profession photos' dir */
    snprintf(filename, sizeof(filename), "%s/chess_photo", apath);
    mkdir(filename, 0755);

    strlcpy(symbolicname, apath, sizeof(symbolicname));
    stampfile(symbolicname, &fh);
    symlink("chess_photo", symbolicname);

    strcpy(fh.title, "¡» ´Ñ°ê·Ó¤ùÀÉ (¤£¯à§R°£¡A¨t²Î»Ý­n)");
    strcpy(fh.owner, str_sysop);
    append_record(adir, &fh, sizeof(fileheader_t));
}
#endif /* defined(CHESSCOUNTRY) */

/* ¦Û°Ê³]¥ßºëµØ°Ï */
void
setup_man(const boardheader_t * board, const boardheader_t * oldboard)
{
    char            genbuf[200];

    setapath(genbuf, board->brdname);
    mkdir(genbuf, 0755);

#ifdef CHESSCOUNTRY
    if (oldboard == NULL || oldboard->chesscountry != board->chesscountry)
	if (board->chesscountry != CHESSCODE_NONE)
	    AddingChessCountryFiles(genbuf);
	// else // doesn't remove files..
#endif
}

void delete_symbolic_link(boardheader_t *bh, int bid)
{
    memset(bh, 0, sizeof(boardheader_t));
    substitute_record(fn_board, bh, sizeof(boardheader_t), bid);
    reset_board(bid);
    sort_bcache(); 
    log_usies("DelLink", bh->brdname);
}

int dir_cmp(const void *a, const void *b)
{
  return (atoi( &((fileheader_t *)a)->filename[2] ) -
          atoi( &((fileheader_t *)b)->filename[2] ));
}

void merge_dir(const char *dir1, const char *dir2, int isoutter)
{
     int i, pn, sn;
     fileheader_t *fh;
     char *p1, *p2, bakdir[128], file1[128], file2[128];
     strcpy(file1,dir1);
     strcpy(file2,dir2);
     if((p1=strrchr(file1,'/')))
	 p1 ++;
     else
	 p1 = file1;
     if((p2=strrchr(file2,'/')))
	 p2 ++;
     else
	 p2 = file2;

     pn=get_num_records(dir1, sizeof(fileheader_t));
     sn=get_num_records(dir2, sizeof(fileheader_t));
     if(!sn) return;
     fh= (fileheader_t *)malloc( (pn+sn)*sizeof(fileheader_t));
     get_records(dir1, fh, sizeof(fileheader_t), 1, pn);
     get_records(dir2, fh+pn, sizeof(fileheader_t), 1, sn);
     if(isoutter)
         {
             for(i=0; i<sn; i++)
               if(fh[pn+i].owner[0])
                   strcat(fh[pn+i].owner, "."); 
         }
     qsort(fh, pn+sn, sizeof(fileheader_t), dir_cmp);
     sprintf(bakdir,"%s.bak", dir1);
     Rename(dir1, bakdir);
     for(i=1; i<=pn+sn; i++ )
        {
         if(!fh[i-1].filename[0]) continue;
         if(i == pn+sn ||  strcmp(fh[i-1].filename, fh[i].filename))
	 {
                fh[i-1].recommend =0;
		fh[i-1].filemode |= 1;
                append_record(dir1, &fh[i-1], sizeof(fileheader_t));
		strcpy(p1, fh[i-1].filename);
                if(!dashf(file1))
		      {
			  strcpy(p2, fh[i-1].filename);
			  Copy(file2, file1);
		      } 
	 }
         else
                fh[i].filemode |= fh[i-1].filemode;
        }
     
     free(fh);
}

int
m_mod_board(char *bname)
{
    boardheader_t   bh, newbh;
    int             bid;
    char            genbuf[256], ans[4];

    bid = getbnum(bname);
    if (!bid || !bname[0] || get_record(fn_board, &bh, sizeof(bh), bid) == -1) {
	vmsg(err_bid);
	return -1;
    }
    prints("¬ÝªO¦WºÙ¡G%s\n¬ÝªO»¡©ú¡G%s\n¬ÝªObid¡G%d\n¬ÝªOGID¡G%d\n"
	   "ªO¥D¦W³æ¡G%s", bh.brdname, bh.title, bid, bh.gid, bh.BM);
    bperm_msg(&bh);

    /* Ptt ³oÃäÂ_¦æ·|ÀÉ¨ì¤U­± */
    move(9, 0);
    snprintf(genbuf, sizeof(genbuf), "(E)³]©w (V)¹Hªk/¸Ñ°£%s%s [Q]¨ú®ø¡H",
	    HasUserPerm(PERM_SYSOP |
		     PERM_BOARD) ? " (B)Vote (S)±Ï¦^ (C)¦X¨Ö (G)½ä½L¸Ñ¥d" : "",
	    HasUserPerm(PERM_SYSSUBOP | PERM_BOARD) ? " (D)§R°£" : "");
    getdata(10, 0, genbuf, ans, 3, LCECHO);

    switch (*ans) {
    case 'g':
	if (HasUserPerm(PERM_SYSOP | PERM_BOARD)) {
	    char            path[256];
	    setbfile(genbuf, bname, FN_TICKET_LOCK);
	    setbfile(path, bname, FN_TICKET_END);
	    rename(genbuf, path);
	}
	break;
    case 's':
	if (HasUserPerm(PERM_SYSOP | PERM_BOARD)) {
	  snprintf(genbuf, sizeof(genbuf),
		   BBSHOME "/bin/buildir boards/%c/%s &",
		   bh.brdname[0], bh.brdname);
	    system(genbuf);
	}
	break;
    case 'c':
	if (HasUserPerm(PERM_SYSOP)) {
	   char frombname[20], fromdir[256];
#ifdef MERGEBBS
	   if(getans("¬O§_¶×¤JSOB¬ÝªO? (y/N)")=='y')
	   { 
                 setbdir(genbuf, bname);
	         m_sob_brd(bname, fromdir);
		 if(!fromdir[0]) break;
                 merge_dir(genbuf, fromdir, 1);
           }
	   else{
#endif
	    CompleteBoard(MSG_SELECT_BOARD, frombname);
            if (frombname[0] == '\0' || !getbnum(frombname) ||
		!strcmp(frombname,bname))
	                     break;
            setbdir(genbuf, bname);
            setbdir(fromdir, frombname);
            merge_dir(genbuf, fromdir, 0);
#ifdef MERGEBBS
	   }
#endif
	    touchbtotal(bid);
	}
	break;
    case 'b':
	if (HasUserPerm(PERM_SYSOP | PERM_BOARD)) {
	    char            bvotebuf[10];

	    memcpy(&newbh, &bh, sizeof(bh));
	    snprintf(bvotebuf, sizeof(bvotebuf), "%d", newbh.bvote);
	    move(20, 0);
	    prints("¬ÝªO %s ­ì¨Óªº BVote¡G%d", bh.brdname, bh.bvote);
	    getdata_str(21, 0, "·sªº Bvote¡G", genbuf, 5, LCECHO, bvotebuf);
	    newbh.bvote = atoi(genbuf);
	    substitute_record(fn_board, &newbh, sizeof(newbh), bid);
	    reset_board(bid);
	    log_usies("SetBoardBvote", newbh.brdname);
	    break;
	} else
	    break;
    case 'v':
	memcpy(&newbh, &bh, sizeof(bh));
	outs("¬ÝªO¥Ø«e¬°");
	outs((bh.brdattr & BRD_BAD) ? "¹Hªk" : "¥¿±`");
	getdata(21, 0, "½T©w§ó§ï¡H", genbuf, 5, LCECHO);
	if (genbuf[0] == 'y') {
	    if (newbh.brdattr & BRD_BAD)
		newbh.brdattr = newbh.brdattr & (!BRD_BAD);
	    else
		newbh.brdattr = newbh.brdattr | BRD_BAD;
	    substitute_record(fn_board, &newbh, sizeof(newbh), bid);
	    reset_board(bid);
	    log_usies("ViolateLawSet", newbh.brdname);
	}
	break;
    case 'd':
	if (!HasUserPerm(PERM_SYSOP | PERM_BOARD))
	    break;
	getdata_str(9, 0, msg_sure_ny, genbuf, 3, LCECHO, "N");
	if (genbuf[0] != 'y' || !bname[0])
	    outs(MSG_DEL_CANCEL);
	else if (bh.brdattr & BRD_SYMBOLIC) {
	    delete_symbolic_link(&bh, bid);
	}
	else {
	    strlcpy(bname, bh.brdname, sizeof(bh.brdname));
	    snprintf(genbuf, sizeof(genbuf),
		    "/bin/tar zcvf tmp/board_%s.tgz boards/%c/%s man/boards/%c/%s >/dev/null 2>&1;"
		    "/bin/rm -fr boards/%c/%s man/boards/%c/%s",
		    bname, bname[0], bname, bname[0],
		    bname, bname[0], bname, bname[0], bname);
	    system(genbuf);
	    memset(&bh, 0, sizeof(bh));
	    snprintf(bh.title, sizeof(bh.title),
		     "%s ¬ÝªO %s §R°£", bname, cuser.userid);
	    post_msg("Security", bh.title, "½Ðª`·N§R°£ªº¦Xªk©Ê", "[¨t²Î¦w¥þ§½]");
	    substitute_record(fn_board, &bh, sizeof(bh), bid);
	    reset_board(bid);
            sort_bcache(); 
	    log_usies("DelBoard", bh.title);
	    outs("§RªO§¹²¦");
	}
	break;
    case 'e':
	move(8, 0);
	outs("ª½±µ«ö [Return] ¤£­×§ï¸Ó¶µ³]©w");
	memcpy(&newbh, &bh, sizeof(bh));

	while (getdata(9, 0, "·s¬ÝªO¦WºÙ¡G", genbuf, IDLEN + 1, DOECHO)) {
	    if (getbnum(genbuf)) {
		move(3, 0);
		outs("¿ù»~! ªO¦W¹p¦P");
	    } else if ( !invalid_brdname(genbuf) ){
		strlcpy(newbh.brdname, genbuf, sizeof(newbh.brdname));
		break;
	    }
	}

	do {
	    getdata_str(12, 0, "¬ÝªOÃþ§O¡G", genbuf, 5, DOECHO, bh.title);
	    if (strlen(genbuf) == 4)
		break;
	} while (1);

	if (strlen(genbuf) >= 4)
	    strncpy(newbh.title, genbuf, 4);

	newbh.title[4] = ' ';

	getdata_str(14, 0, "¬ÝªO¥DÃD¡G", genbuf, BTLEN + 1, DOECHO,
		    bh.title + 7);
	if (genbuf[0])
	    strlcpy(newbh.title + 7, genbuf, sizeof(newbh.title) - 7);
	if (getdata_str(15, 0, "·sªO¥D¦W³æ¡G", genbuf, IDLEN * 3 + 3, DOECHO,
			bh.BM)) {
	    trim(genbuf);
	    strlcpy(newbh.BM, genbuf, sizeof(newbh.BM));
	}
#ifdef CHESSCOUNTRY
	if (HasUserPerm(PERM_SYSOP)) {
	    snprintf(genbuf, sizeof(genbuf), "%d", bh.chesscountry);
	    if (getdata_str(16, 0, "³]©w´Ñ°ê (0)µL (1)¤­¤l´Ñ (2)¶H´Ñ", ans,
			sizeof(ans), LCECHO, genbuf)){
		newbh.chesscountry = atoi(ans);
		if (newbh.chesscountry > CHESSCODE_MAX ||
			newbh.chesscountry < CHESSCODE_NONE)
		    newbh.chesscountry = bh.chesscountry;
	    }
	}
#endif /* defined(CHESSCOUNTRY) */
	if (HasUserPerm(PERM_SYSOP|PERM_BOARD)) {
	    move(1, 0);
	    clrtobot();
	    newbh.brdattr = setperms(newbh.brdattr, str_permboard);
	    move(1, 0);
	    clrtobot();
	}
	if (newbh.brdattr & BRD_GROUPBOARD)
	    strncpy(newbh.title + 5, "£U", 2);
	else if (newbh.brdattr & BRD_NOTRAN)
	    strncpy(newbh.title + 5, "¡·", 2);
	else
	    strncpy(newbh.title + 5, "¡´", 2);

	if (HasUserPerm(PERM_SYSOP|PERM_BOARD) && !(newbh.brdattr & BRD_HIDE)) {
	    getdata_str(14, 0, "³]©wÅª¼gÅv­­(Y/N)¡H", ans, sizeof(ans), LCECHO, "N");
	    if (*ans == 'y') {
		getdata_str(15, 0, "­­¨î [R]¾\\Åª (P)µoªí¡H", ans, sizeof(ans), LCECHO,
			    "R");
		if (*ans == 'p')
		    newbh.brdattr |= BRD_POSTMASK;
		else
		    newbh.brdattr &= ~BRD_POSTMASK;

		move(1, 0);
		clrtobot();
		bperm_msg(&newbh);
		newbh.level = setperms(newbh.level, str_permid);
		clear();
	    }
	}

	getdata(b_lines - 1, 0, "½Ð±z½T©w(Y/N)¡H[Y]", genbuf, 4, LCECHO);

	if ((*genbuf != 'n') && memcmp(&newbh, &bh, sizeof(bh))) {
	    if (strcmp(bh.brdname, newbh.brdname)) {
		char            src[60], tar[60];

		setbpath(src, bh.brdname);
		setbpath(tar, newbh.brdname);
		Rename(src, tar);

		setapath(src, bh.brdname);
		setapath(tar, newbh.brdname);
		Rename(src, tar);
	    }
	    setup_man(&newbh, &bh);
	    substitute_record(fn_board, &newbh, sizeof(newbh), bid);
	    reset_board(bid);
            sort_bcache(); 
	    log_usies("SetBoard", newbh.brdname);
	}
    }
    return 0;
}

/* ³]©w¬ÝªO */
int
m_board(void)
{
    char            bname[32];

    stand_title("¬ÝªO³]©w");
    CompleteBoardAndGroup(msg_bid, bname);
    if (!*bname)
	return 0;
    m_mod_board(bname);
    return 0;
}

/* ³]©w¨t²ÎÀÉ®× */
int
x_file(void)
{
    int             aborted;
    char            ans[4], *fpath;

    move(b_lines - 7, 0);
    /* Ptt */
    outs("³]©w (1)¨­¥÷½T»{«H (4)postª`·N¨Æ¶µ (5)¿ù»~µn¤J°T®§ (6)µù¥U½d¨Ò (7)³q¹L½T»{³qª¾\n");
    outs("     (8)email post³qª¾ (9)¨t²Î¥\\¯àºëÆF (A)¯ù¼Ó (B)¯¸ªø¦W³æ (C)email³q¹L½T»{\n");
    outs("     (D)·s¨Ï¥ÎªÌ»Ýª¾ (E)¨­¥÷½T»{¤èªk (F)Åwªïµe­± (G)¶i¯¸µe­±"
#ifdef MULTI_WELCOME_LOGIN
	 "(X)§R°£¶i¯¸µe­±"
#endif
	 "\n");
    outs("     (H)¬ÝªO´Á­­ (I)¬G¶m (J)¥X¯¸µe­± (K)¥Í¤é¥d (L)¸`¤é (M)¥~Äy¨Ï¥ÎªÌ»{ÃÒ³qª¾\n");
    outs("     (N)¥~Äy¨Ï¥ÎªÌ¹L´ÁÄµ§i³qª¾ (O)¬ÝªO¦Cªí help (P)¤å³¹¦Cªí help\n");
#ifdef PLAY_ANGEL
    outs(" (Y)¤p¤Ñ¨Ï»{ÃÒ³qª¾\n");
#endif
    getdata(b_lines - 1, 0, "[Q]¨ú®ø[1-9 A-P]¡H", ans, sizeof(ans), LCECHO);

    switch (ans[0]) {
    case '1':
	fpath = "etc/confirm";
	break;
    case '4':
	fpath = "etc/post.note";
	break;
    case '5':
	fpath = "etc/goodbye";
	break;
    case '6':
	fpath = "etc/register";
	break;
    case '7':
	fpath = "etc/registered";
	break;
    case '8':
	fpath = "etc/emailpost";
	break;
    case '9':
	fpath = "etc/hint";
	break;
    case 'b':
	fpath = "etc/sysop";
	break;
    case 'c':
	fpath = "etc/bademail";
	break;
    case 'd':
	fpath = "etc/newuser";
	break;
    case 'e':
	fpath = "etc/justify";
	break;
    case 'f':
	fpath = "etc/Welcome";
	break;
    case 'g':
#ifdef MULTI_WELCOME_LOGIN
	getdata(b_lines - 1, 0, "²Ä´X­Ó¶i¯¸µe­±[0-4]", ans, sizeof(ans), LCECHO);
	if (ans[0] == '1') {
	    fpath = "etc/Welcome_login.1";
	} else if (ans[0] == '2') {
	    fpath = "etc/Welcome_login.2";
	} else if (ans[0] == '3') {
	    fpath = "etc/Welcome_login.3";
	} else if (ans[0] == '4') {
	    fpath = "etc/Welcome_login.4";
	} else {
	    fpath = "etc/Welcome_login.0";
	}
#else
	fpath = "etc/Welcome_login";
#endif
	break;

#ifdef MULTI_WELCOME_LOGIN
    case 'x':
	getdata(b_lines - 1, 0, "²Ä´X­Ó¶i¯¸µe­±[1-4]", ans, sizeof(ans), LCECHO);
	if (ans[0] == '1') {
	    unlink("etc/Welcome_login.1");
	    vmsg("ok");
	} else if (ans[0] == '2') {
	    unlink("etc/Welcome_login.2");
	    vmsg("ok");
	} else if (ans[0] == '3') {
	    unlink("etc/Welcome_login.3");
	    vmsg("ok");
	} else if (ans[0] == '4') {
	    unlink("etc/Welcome_login.4");
	    vmsg("ok");
	} else {
	    vmsg("©Ò«ü©wªº¶i¯¸µe­±µLªk§R°£");
	}
	return FULLUPDATE;

#endif

    case 'h':
	fpath = "etc/expire.conf";
	break;
    case 'i':
	fpath = "etc/domain_name_query.cidr";
	break;
    case 'j':
	fpath = "etc/Logout";
	break;
    case 'k':
	fpath = "etc/Welcome_birth";
	break;
    case 'l':
	fpath = "etc/feast";
	break;
    case 'm':
	fpath = "etc/foreign_welcome";
	break;
    case 'n':
	fpath = "etc/foreign_expired_warn";
	break;
    case 'o':
	fpath = "etc/boardlist.help";
	break;
    case 'p':
	fpath = "etc/board.help";
	break;

#ifdef PLAY_ANGEL
    case 'y':
	fpath = "etc/angel_notify";
	break;
#endif

    default:
	return FULLUPDATE;
    }
    aborted = vedit(fpath, NA, NULL);
    vmsg("\n\n¨t²ÎÀÉ®×[%s]¡G%s", fpath,
	 (aborted == -1) ? "¥¼§ïÅÜ" : "§ó·s§¹²¦");
    return FULLUPDATE;
}

static int add_board_record(const boardheader_t *board)
{
    int bid;
    if ((bid = getbnum("")) > 0) {
	substitute_record(fn_board, board, sizeof(boardheader_t), bid);
	reset_board(bid);
        sort_bcache(); 
    } else if (append_record(fn_board, (fileheader_t *)board, sizeof(boardheader_t)) == -1) {
	return -1;
    } else {
	addbrd_touchcache();
    }
    return 0;
}

/**
 * open a new board
 * @param whatclass In which sub class
 * @param recover   Forcely open a new board, often used for recovery.
 * @return -1 if failed
 */
int
m_newbrd(int whatclass, int recover)
{
    boardheader_t   newboard;
    char            ans[4];
    char            genbuf[200];

    stand_title("«Ø¥ß·sªO");
    memset(&newboard, 0, sizeof(newboard));

    newboard.gid = whatclass;
    if (newboard.gid == 0) {
	vmsg("½Ð¥ý¿ï¾Ü¤@­ÓÃþ§O¦A¶}ªO!");
	return -1;
    }
    do {
	if (!getdata(3, 0, msg_bid, newboard.brdname,
		     sizeof(newboard.brdname), DOECHO))
	    return -1;
    } while (invalid_brdname(newboard.brdname));

    do {
	getdata(6, 0, "¬ÝªOÃþ§O¡G", genbuf, 5, DOECHO);
	if (strlen(genbuf) == 4)
	    break;
    } while (1);

    strncpy(newboard.title, genbuf, 4);

    newboard.title[4] = ' ';

    getdata(8, 0, "¬ÝªO¥DÃD¡G", genbuf, BTLEN + 1, DOECHO);
    if (genbuf[0])
	strlcpy(newboard.title + 7, genbuf, sizeof(newboard.title) - 7);
    setbpath(genbuf, newboard.brdname);

    if (!recover && 
        (getbnum(newboard.brdname) > 0 || mkdir(genbuf, 0755) == -1)) {
	vmsg("¦¹¬ÝªO¤w¸g¦s¦b! ½Ð¨ú¤£¦P­^¤åªO¦W");
	return -1;
    }
    newboard.brdattr = BRD_NOTRAN;

    if (HasUserPerm(PERM_SYSOP)) {
	move(1, 0);
	clrtobot();
	newboard.brdattr = setperms(newboard.brdattr, str_permboard);
	move(1, 0);
	clrtobot();
    }
    getdata(9, 0, "¬O¬ÝªO? (N:¥Ø¿ý) (Y/n)¡G", genbuf, 3, LCECHO);
    if (genbuf[0] == 'n')
	newboard.brdattr |= BRD_GROUPBOARD;

    if (newboard.brdattr & BRD_GROUPBOARD)
	strncpy(newboard.title + 5, "£U", 2);
    else if (newboard.brdattr & BRD_NOTRAN)
	strncpy(newboard.title + 5, "¡·", 2);
    else
	strncpy(newboard.title + 5, "¡´", 2);

    newboard.level = 0;
    getdata(11, 0, "ªO¥D¦W³æ¡G", newboard.BM, sizeof(newboard.BM), DOECHO);
#ifdef CHESSCOUNTRY
    if (getdata_str(12, 0, "³]©w´Ñ°ê (0)µL (1)¤­¤l´Ñ (2)¶H´Ñ", ans,
		sizeof(ans), LCECHO, "0")){
	newboard.chesscountry = atoi(ans);
	if (newboard.chesscountry > CHESSCODE_MAX ||
		newboard.chesscountry < CHESSCODE_NONE)
	    newboard.chesscountry = CHESSCODE_NONE;
    }
#endif /* defined(CHESSCOUNTRY) */

    if (HasUserPerm(PERM_SYSOP) && !(newboard.brdattr & BRD_HIDE)) {
	getdata_str(14, 0, "³]©wÅª¼gÅv­­(Y/N)¡H", ans, sizeof(ans), LCECHO, "N");
	if (*ans == 'y') {
	    getdata_str(15, 0, "­­¨î [R]¾\\Åª (P)µoªí¡H", ans, sizeof(ans), LCECHO, "R");
	    if (*ans == 'p')
		newboard.brdattr |= BRD_POSTMASK;
	    else
		newboard.brdattr &= (~BRD_POSTMASK);

	    move(1, 0);
	    clrtobot();
	    bperm_msg(&newboard);
	    newboard.level = setperms(newboard.level, str_permid);
	    clear();
	}
    }

    add_board_record(&newboard);
    getbcache(whatclass)->childcount = 0;
    pressanykey();
    setup_man(&newboard, NULL);
    outs("\n·sªO¦¨¥ß");
    post_newboard(newboard.title, newboard.brdname, newboard.BM);
    log_usies("NewBoard", newboard.title);
    pressanykey();
    return 0;
}

int make_symbolic_link(const char *bname, int gid)
{
    boardheader_t   newboard;
    int bid;
    
    bid = getbnum(bname);
    memset(&newboard, 0, sizeof(newboard));

    /*
     * known issue:
     *   These two stuff will be used for sorting.  But duplicated brdnames
     *   may cause wrong binary-search result.  So I replace the last 
     *   letters of brdname to '~'(ascii code 126) in order to correct the
     *   resuilt, thought I think it's a dirty solution.
     *
     *   Duplicate entry with same brdname may cause wrong result, if
     *   searching by key brdname.  But people don't need to know a board
     *   is symbolic, so just let SYSOP know it. You may want to read
     *   board.c:load_boards().
     */

    strlcpy(newboard.brdname, bname, sizeof(newboard.brdname));
    newboard.brdname[strlen(bname) - 1] = '~';
    strlcpy(newboard.title, bcache[bid - 1].title, sizeof(newboard.title));
    strcpy(newboard.title + 5, "¢I¬ÝªO³sµ²");

    newboard.gid = gid;
    BRD_LINK_TARGET(&newboard) = bid;
    newboard.brdattr = BRD_NOTRAN | BRD_SYMBOLIC;

    if (add_board_record(&newboard) < 0)
	return -1;
    return bid;
}

int make_symbolic_link_interactively(int gid)
{
    char buf[32];

    CompleteBoard(msg_bid, buf);
    if (!buf[0])
	return -1;

    stand_title("«Ø¥ß¬ÝªO³sµ²");

    if (make_symbolic_link(buf, gid) < 0) {
	vmsg("¬ÝªO³sµ²«Ø¥ß¥¢±Ñ");
	return -1;
    }
    log_usies("NewSymbolic", buf);
    return 0;
}

static int
auto_scan(char fdata[][STRLEN], char ans[])
{
    int             good = 0;
    int             count = 0;
    int             i;
    char            temp[10];

    if (!strncmp(fdata[2], "¤p", 2) || strstr(fdata[2], "¤X")
	|| strstr(fdata[2], "½Ö") || strstr(fdata[2], "¤£")) {
	ans[0] = '0';
	return 1;
    }
    strncpy(temp, fdata[2], 2);
    temp[2] = '\0';

    /* Å|¦r */
    if (!strncmp(temp, &(fdata[2][2]), 2)) {
	ans[0] = '0';
	return 1;
    }
    if (strlen(fdata[2]) >= 6) {
	if (strstr(fdata[2], "³¯¤ô«ó")) {
	    ans[0] = '0';
	    return 1;
	}
	if (strstr("»¯¿ú®]§õ©P§d¾G¤ý", temp))
	    good++;
	else if (strstr("§ùÃC¶ÀªL³¯©x§E¨¯¼B", temp))
	    good++;
	else if (strstr("Ä¬¤è§d§f§õªò±i¹ùÀ³Ä¬", temp))
	    good++;
	else if (strstr("®}ÁÂ¥Û¿c¬IÀ¹¯Î­ð", temp))
	    good++;
    }
    if (!good)
	return 0;

    if (!strcmp(fdata[3], fdata[4]) ||
	!strcmp(fdata[3], fdata[5]) ||
	!strcmp(fdata[4], fdata[5])) {
	ans[0] = '4';
	return 5;
    }
    if (strstr(fdata[3], "¤j")) {
	if (strstr(fdata[3], "¥x") || strstr(fdata[3], "²H") ||
	    strstr(fdata[3], "¥æ") || strstr(fdata[3], "¬F") ||
	    strstr(fdata[3], "²M") || strstr(fdata[3], "Äµ") ||
	    strstr(fdata[3], "®v") || strstr(fdata[3], "»Ê¶Ç") ||
	    strstr(fdata[3], "¤¤¥¡") || strstr(fdata[3], "¦¨") ||
	    strstr(fdata[3], "»²") || strstr(fdata[3], "ªF§d"))
	    good++;
    } else if (strstr(fdata[3], "¤k¤¤"))
	good++;

    if (strstr(fdata[4], "¦a²y") || strstr(fdata[4], "¦t©z") ||
	strstr(fdata[4], "«H½c")) {
	ans[0] = '2';
	return 3;
    }
    if (strstr(fdata[4], "¥«") || strstr(fdata[4], "¿¤")) {
	if (strstr(fdata[4], "¸ô") || strstr(fdata[4], "µó")) {
	    if (strstr(fdata[4], "¸¹"))
		good++;
	}
    }
    for (i = 0; fdata[5][i]; i++) {
	if (isdigit((int)fdata[5][i]))
	    count++;
    }

    if (count <= 4) {
	ans[0] = '3';
	return 4;
    } else if (count >= 7)
	good++;

    if (good >= 3) {
	ans[0] = 'y';
	return -1;
    } else
	return 0;
}

/* ³B²z Register Form */
int
scan_register_form(const char *regfile, int automode, int neednum)
{
    char            genbuf[200];
    char    *logfile = "register.log";
    char    *field[] = {
	"uid", "ident", "name", "career", "addr", "phone", "email", NULL
    };
    char    *finfo[] = {
	"±b¸¹", "¨­¤ÀÃÒ¸¹", "¯u¹ê©m¦W", "ªA°È³æ¦ì", "¥Ø«e¦í§}",
	"³sµ¸¹q¸Ü", "¹q¤l¶l¥ó«H½c", NULL
    };
    char    *reason[] = {
	"¿é¤J¯u¹ê©m¦W",
	"¸Ô¶ñ¡u(²¦·~)¾Ç®Õ¤Î¡y¨t¡z¡y¯Å¡z¡v©Î¡uªA°È³æ¦ì(§t©ÒÄÝ¿¤¥«¤ÎÂ¾ºÙ)¡v",
	"¶ñ¼g§¹¾ãªº¦í§}¸ê®Æ (§t¿¤¥«¦WºÙ, ¥x¥_¥«½Ð§t¦æ¬F°Ï°ì¡^",
	"¸Ô¶ñ³sµ¸¹q¸Ü (§t°Ï°ì½X, ¤¤¶¡¤£¥Î¥[ \"-\", \"(\", \")\"µ¥²Å¸¹",
	"½T¹ê¶ñ¼gµù¥U¥Ó½Ðªí",
	"¥Î¤¤¤å¶ñ¼g¥Ó½Ð³æ",
	"¿é¤J¯u¹ê¨­¤ÀÃÒ¦r¸¹",
	NULL
    };
    char    *autoid = "AutoScan";
    userec_t        muser;
    FILE           *fn, *fout, *freg;
    char            fdata[7][STRLEN];
    char            fname[STRLEN], buf[STRLEN];
    char            ans[4], *ptr, *uid;
    int             n = 0, unum = 0;
    int             nSelf = 0, nAuto = 0;

    uid = cuser.userid;
    snprintf(fname, sizeof(fname), "%s.tmp", regfile);
    move(2, 0);
    if (dashf(fname)) {
	if (neednum == 0) {	/* ¦Û¤v¶i Admin ¨Ó¼fªº */
	    vmsg("¨ä¥L SYSOP ¤]¦b¼f®Öµù¥U¥Ó½Ð³æ");
	}
	return -1;
    }
    Rename(regfile, fname);
    if ((fn = fopen(fname, "r")) == NULL) {
	vmsg("¨t²Î¿ù»~¡AµLªkÅª¨úµù¥U¸ê®ÆÀÉ: %s", fname);
	return -1;
    }
    if (neednum) {		/* ³Q±j­¢¼fªº */
	move(1, 0);
	clrtobot();
	prints("¦U¦ì¨ã¦³¯¸ªøÅv­­ªº¤H¡Aµù¥U³æ²Ö¿n¶W¹L¤@¦Ê¥÷¤F¡A³Â·Ð±zÀ°¦£¼f %d ¥÷\n", neednum);
	outs("¤]´N¬O¤j·§¤G¤Q¤À¤§¤@ªº¼Æ¶q¡A·íµM¡A±z¤]¥i¥H¦h¼f\n¨S¼f§¹¤§«e¡A¨t²Î¤£·|Åý§A¸õ¥X³é¡IÁÂÁÂ");
	pressanykey();
    }
    while( fgets(genbuf, STRLEN, fn) ){
	memset(fdata, 0, sizeof(fdata));
	do {
	    if( genbuf[0] == '-' )
		break;
	    if ((ptr = (char *)strstr(genbuf, ": "))) {
		*ptr = '\0';
		for (n = 0; field[n]; n++) {
		    if (strcmp(genbuf, field[n]) == 0) {
			strlcpy(fdata[n], ptr + 2, sizeof(fdata[n]));
			if ((ptr = (char *)strchr(fdata[n], '\n')))
			    *ptr = '\0';
		    }
		}
	    }
	} while( fgets(genbuf, STRLEN, fn) );

	if ((unum = getuser(fdata[0], &muser)) == 0) {
	    move(2, 0);
	    clrtobot();
	    outs("¨t²Î¿ù»~¡A¬dµL¦¹¤H\n\n");
	    for (n = 0; field[n]; n++)
		prints("%s     : %s\n", finfo[n], fdata[n]);
	    pressanykey();
	    neednum--;
	} else {
	    neednum--;
	    if (automode)
		uid = autoid;

	    if ((!automode || !auto_scan(fdata, ans))) {
		uid = cuser.userid;

		move(1, 0);
		prints("±b¸¹¦ì¸m    ¡G%d\n", unum);
		user_display(&muser, 1);
		move(14, 0);
		prints(ANSI_COLOR(1;32) "------------- "
			"½Ð¯¸ªøÄY®æ¼f®Ö¨Ï¥ÎªÌ¸ê®Æ¡A±zÁÙ¦³ %d ¥÷"
			"---------------" ANSI_RESET "\n", neednum);
	    	prints("  %-12s¡G%s\n", finfo[0], fdata[0]);
#ifdef FOREIGN_REG
		prints("0.%-12s¡G%s%s\n", finfo[2], fdata[2],
		       muser.uflag2 & FOREIGN ? " (¥~Äy)" : "");
#else
		prints("0.%-12s¡G%s\n", finfo[2], fdata[2]);
#endif
		for (n = 3; field[n]; n++) {
		    prints("%d.%-12s¡G%s\n", n - 2, finfo[n], fdata[n]);
		}
		if (muser.userlevel & PERM_LOGINOK) {
		    ans[0] = getkey("¦¹±b¸¹¤w¸g§¹¦¨µù¥U, "
				    "§ó·s(Y/N/Skip)¡H[N] ");
		    if (ans[0] != 'y' && ans[0] != 's')
			ans[0] = 'd';
		} else {
		    if (search_ulist(unum) == NULL)
		        ans[0] = vmsg_lines(22, "¬O§_±µ¨ü¦¹¸ê®Æ(Y/N/Q/Del/Skip)¡H[S])");
		    else
			ans[0] = 's';
		    if ('A' <= ans[0] && ans[0] <= 'Z')
			ans[0] += 32;
		    if (ans[0] != 'y' && ans[0] != 'n' && ans[0] != 'q' &&
			ans[0] != 'd' && !('0' <= ans[0] && ans[0] <= '4'))
			ans[0] = 's';
		    ans[1] = 0;
		}
		nSelf++;
	    } else
		nAuto++;
	    if (neednum > 0 && ans[0] == 'q') {
		move(2, 0);
		clrtobot();
		vmsg("¨S¼f§¹¤£¯à°h¥X");
		ans[0] = 's';
	    }
	    switch (ans[0]) {
	    case 'q':
		if ((freg = fopen(regfile, "a"))) {
		    for (n = 0; field[n]; n++)
			fprintf(freg, "%s: %s\n", field[n], fdata[n]);
		    fprintf(freg, "----\n");
		    while (fgets(genbuf, STRLEN, fn))
			fputs(genbuf, freg);
		    fclose(freg);
		}
	    case 'd':
		break;
	    case '0':
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case 'n':
		if (ans[0] == 'n') {
		    for (n = 0; field[n]; n++)
			prints("%s: %s\n", finfo[n], fdata[n]);
		    move(9, 0);
		    outs("½Ð´£¥X°h¦^¥Ó½Ðªí­ì¦]¡A«ö <enter> ¨ú®ø\n");
		    for (n = 0; reason[n]; n++)
			prints("%d) ½Ð%s\n", n, reason[n]);
		} else
		    buf[0] = ans[0];
		if (ans[0] != 'n' ||
		    getdata(10 + n, 0, "°h¦^­ì¦]¡G", buf, 60, DOECHO))
		    if ((buf[0] - '0') >= 0 && (buf[0] - '0') < n) {
			int             i;
			fileheader_t    mhdr;
			char            title[128], buf1[80];
			FILE           *fp;

			sethomepath(buf1, muser.userid);
			stampfile(buf1, &mhdr);
			strlcpy(mhdr.owner, cuser.userid, sizeof(mhdr.owner));
			strlcpy(mhdr.title, "[µù¥U¥¢±Ñ]", TTLEN);
			mhdr.filemode = 0;
			sethomedir(title, muser.userid);
			if (append_record(title, &mhdr, sizeof(mhdr)) != -1) {
			    fp = fopen(buf1, "w");
			    
			    for(i = 0; buf[i] && i < sizeof(buf); i++){
				if (!isdigit((int)buf[i]))
				    continue;
				fprintf(fp, "[°h¦^­ì¦]] ½Ð%s\n",
					reason[buf[i] - '0']);
			    }

			    fclose(fp);
			}
			if ((fout = fopen(logfile, "a"))) {
			    for (n = 0; field[n]; n++)
				fprintf(fout, "%s: %s\n", field[n], fdata[n]);
			    fprintf(fout, "Date: %s\n", Cdate(&now));
			    fprintf(fout, "Rejected: %s [%s]\n----\n",
				    uid, buf);
			    fclose(fout);
			}
			break;
		    }
		move(10, 0);
		clrtobot();
		outs("¨ú®ø°h¦^¦¹µù¥U¥Ó½Ðªí");
	    case 's':
		if ((freg = fopen(regfile, "a"))) {
		    for (n = 0; field[n]; n++)
			fprintf(freg, "%s: %s\n", field[n], fdata[n]);
		    fprintf(freg, "----\n");
		    fclose(freg);
		}
		break;
	    default:
		outs("¥H¤U¨Ï¥ÎªÌ¸ê®Æ¤w¸g§ó·s:\n");
		mail_muser(muser, "[µù¥U¦¨¥\\Åo]", "etc/registered");
#if FOREIGN_REG_DAY > 0
		if(muser.uflag2 & FOREIGN)
		    mail_muser(muser, "[¥X¤J¹ÒºÞ²z§½]", "etc/foreign_welcome");
#endif
		muser.userlevel |= (PERM_LOGINOK | PERM_POST);
		strlcpy(muser.realname, fdata[2], sizeof(muser.realname));
		strlcpy(muser.address, fdata[4], sizeof(muser.address));
		strlcpy(muser.email, fdata[6], sizeof(muser.email));
		snprintf(genbuf, sizeof(genbuf), "%s:%s:%s",
			 fdata[5], fdata[3], uid);
		strlcpy(muser.justify, genbuf, sizeof(muser.justify));
		passwd_update(unum, &muser);

		sethomefile(buf, muser.userid, "justify");
		log_file(buf, LOG_CREAT, genbuf);

		if ((fout = fopen(logfile, "a"))) {
		    for (n = 0; field[n]; n++)
			fprintf(fout, "%s: %s\n", field[n], fdata[n]);
		    fprintf(fout, "Date: %s\n", Cdate(&now));
		    fprintf(fout, "Approved: %s\n", uid);
		    fprintf(fout, "----\n");
		    fclose(fout);
		}
		sethomefile(genbuf, muser.userid, "justify.wait");
		unlink(genbuf);
		break;
	    }
	}
    }
    fclose(fn);
    unlink(fname);

    move(0, 0);
    clrtobot();

    move(5, 0);
    prints("±z¼f¤F %d ¥÷µù¥U³æ¡AAutoScan ¼f¤F %d ¥÷", nSelf, nAuto);

    pressanykey();
    return (0);
}

int
m_register(void)
{
    FILE           *fn;
    int             x, y, wid, len;
    char            ans[4];
    char            genbuf[200];

    if ((fn = fopen(fn_register, "r")) == NULL) {
	outs("¥Ø«e¨ÃµL·sµù¥U¸ê®Æ");
	return XEASY;
    }
    stand_title("¼f®Ö¨Ï¥ÎªÌµù¥U¸ê®Æ");
    y = 2;
    x = wid = 0;

    while (fgets(genbuf, STRLEN, fn) && x < 65) {
	if (strncmp(genbuf, "uid: ", 5) == 0) {
	    move(y++, x);
	    outs(genbuf + 5);
	    len = strlen(genbuf + 5);
	    if (len > wid)
		wid = len;
	    if (y >= t_lines - 3) {
		y = 2;
		x += wid + 2;
	    }
	}
    }
    fclose(fn);
    getdata(b_lines - 1, 0, "¶}©l¼f®Ö¶Ü(Auto/Yes/No)¡H[N] ", ans, sizeof(ans), LCECHO);
    if (ans[0] == 'a')
	scan_register_form(fn_register, 1, 0);
    else if (ans[0] == 'y')
	scan_register_form(fn_register, 0, 0);

    return 0;
}

int
cat_register(void)
{
    if (system("cat register.new.tmp >> register.new") == 0 &&
	unlink("register.new.tmp") == 0)
	vmsg("OK ÂP~~ Ä~Äò¥h¾Ä°«§a!!");
    else
	vmsg("¨S¿ìªkCAT¹L¥h©O ¥hÀË¬d¤@¤U¨t²Î§a!!");
    return 0;
}

static void
give_id_money(const char *user_id, int money, const char *mail_title)
{
    char            tt[TTLEN + 1] = {0};

    if (deumoney(searchuser(user_id, NULL), money) < 0) { // TODO if searchuser() return 0
	move(12, 0);
	clrtoeol();
	prints("id:%s money:%d ¤£¹ï§a!!", user_id, money);
	pressanykey();
    } else {
	snprintf(tt, sizeof(tt), "%s : %d ptt ¹ô", mail_title, money);
	mail_id(user_id, tt, "etc/givemoney.why", "[PTT »È¦æ]");
    }
}

int
give_money(void)
{
    FILE           *fp, *fp2;
    char           *ptr, *id, *mn;
    char            buf[200] = "", reason[100], tt[TTLEN + 1] = "";
    int             to_all = 0, money = 0;
    int             total_money=0, count=0;

    getdata(0, 0, "«ü©w¨Ï¥ÎªÌ(S) ¥þ¯¸¨Ï¥ÎªÌ(A) ¨ú®ø(Q)¡H[S]", buf, sizeof(buf), LCECHO);
    if (buf[0] == 'q')
	return 1;
    else if (buf[0] == 'a') {
	to_all = 1;
	getdata(1, 0, "µo¦h¤Ö¿ú©O?", buf, 20, DOECHO);
	money = atoi(buf);
	if (money <= 0) {
	    move(2, 0);
	    vmsg("¿é¤J¿ù»~!!");
	    return 1;
	}
    } else {
	if (vedit("etc/givemoney.txt", NA, NULL) < 0)
	    return 1;
    }

    clear();

    unlink("etc/givemoney.log");
    if (!(fp2 = fopen("etc/givemoney.log", "w")))
	return 1;

    getdata(0, 0, "°Ê¥Î°ê®w!½Ð¿é¤J¥¿·í²z¥Ñ(¦p¬¡°Ê¦WºÙ):", reason, 40, LCECHO);
    fprintf(fp2,"\n¨Ï¥Î²z¥Ñ: %s\n", reason);

    getdata(1, 0, "­nµo¿ú¤F¶Ü(Y/N)[N]", buf, 3, LCECHO);
    if (buf[0] != 'y')
       {
        fclose(fp2);
	return 1;
       }

    getdata(1, 0, "¬õ¥]³U¼ÐÃD ¡G", tt, TTLEN, DOECHO);
    fprintf(fp2,"\n¬õ¥]³U¼ÐÃD: %s\n", tt);
    move(2, 0);

    vmsg("½s¬õ¥]³U¤º®e");
    if (vedit("etc/givemoney.why", NA, NULL) < 0) {
        fclose(fp2);
	return 1;
    }

    stand_title("µo¿ú¤¤...");
    if (to_all) {
	int             i, unum;
	for (unum = SHM->number, i = 0; i < unum; i++) {
	    if (bad_user_id(SHM->userid[i]))
		continue;
	    id = SHM->userid[i];
	    give_id_money(id, money, tt);
            fprintf(fp2,"µ¹ %s : %d\n", id, money);
            count++;
	}
        sprintf(buf, "(%d¤H:%dP¹ô)", count, count*money);
        strcat(reason, buf);
    } else {
	if (!(fp = fopen("etc/givemoney.txt", "r+"))) {
	    fclose(fp2);
	    return 1;
	}
	while (fgets(buf, sizeof(buf), fp)) {
	    clear();
	    if (!(ptr = strchr(buf, ':')))
		continue;
	    *ptr = '\0';
	    id = buf;
	    mn = ptr + 1;
            money = atoi(mn);
	    give_id_money(id, money, tt);
            fprintf(fp2,"µ¹ %s : %d\n", id, money);
            total_money += money;
            count++;
	}
	fclose(fp);
        sprintf(buf, "(%d¤H:%dP¹ô)", count, total_money);
        strcat(reason, buf);
    
    }

    fclose(fp2);

    sprintf(buf, "%s ¬õ¥]¾÷: %s", cuser.userid, reason);
    post_file("Security", buf, "etc/givemoney.log", "[¬õ¥]¾÷³ø§iø]");
    pressanykey();
    return FULLUPDATE;
}
