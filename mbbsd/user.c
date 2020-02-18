#define PWCU_IMPL
#include "bbs.h"

#ifdef CHESSCOUNTRY
static const char * const chess_photo_name[4] = {
    "photo_fivechess",
    "photo_cchess",
    "photo_connect6",
    "photo_go",
};

static const char * const chess_type[4] = {
    "���l��",
    "�H��",
    "���l�X",
    "���",
};
#endif

void
ban_usermail(const userec_t *u, const char *reason) {
    assert(u);
    if (!(u->userlevel & PERM_LOGINOK))
        return;
    if (!strchr(u->email, '@'))
        return;
    if (!reason || !*reason)
        reason = "(�����ѤF��)";
    log_filef("etc/banemail", LOG_CREAT,
              "# %s: %s (by %s)\nA%s\n",
              u->userid, reason, cuser.userid, u->email);
}

int
kill_user(int num, const char *userid)
{
    userec_t u;
    char src[256], dst[256];

    if(!userid || num<=0 ) return -1;
    sethomepath(src, userid);
    snprintf(dst, sizeof(dst), "tmp/%s", userid);
    friend_delete_all(userid, FRIEND_ALOHA);
    if (dashd(src) && Rename(src, dst) == 0) {
	snprintf(src, sizeof(src), "/bin/rm -fr home/%c/%s >/dev/null 2>&1", userid[0], userid);
	system(src);
    }

    memset(&u, 0, sizeof(userec_t));
    log_usies("KILL", getuserid(num));
    setuserid(num, "");
    passwd_sync_update(num, &u);
    return 0;
}

int
u_loginview(void)
{
    int             i, in;
    unsigned int    pbits = cuser.loginview;

    do {
        vs_hdr("�]�w�i���e��");
        move(4, 0);
        for (i = 0; i < NUMVIEWFILE && loginview_file[i][0]; i++) {
            // ignore those without file name
            if (!*loginview_file[i][0])
                continue;
            prints("    %c. %-20s %-15s \n", 'A' + i,
                   loginview_file[i][1], ((pbits >> i) & 1 ? "��" : "��"));
        }

        in = i; // max i
        i = vmsgf("�Ы� [A-%c] �����]�w�A�� [Return] �����G", 'A'+in-1);
        if (i == '\r')
            break;

        // process i
        i = tolower(i) - 'a';
        if (i >= in || i < 0 || !*loginview_file[i][0]) {
            bell();
            continue;
        }
        pbits ^= (1 << i);
    } while (1);

    if (pbits != cuser.loginview) {
	pwcuSetLoginView(pbits);
    }
    return 0;
}

/* TODO(piaip) ��o�۰ʤơH */
int u_cancelbadpost(void)
{
   int day, prev = cuser.badpost;
   char ans[3];
   int pass_verify = 1;

   // early check.
   if(cuser.badpost == 0) {
       vmsg("�A�èS���h��.");
       return 0;
   }

   // early check for race condition
   // XXX �ѩ�b��API�w�P�B�� (pwcuAPI*) �ҥH�o�� check �i���i�L
   if(search_ulistn(usernum,2)) {
       vmsg("�еn�X��L����, �_�h�����z.");
       return 0;
   }

   // XXX reload account here? (also simply optional)
   pwcuReload();
   prev = cuser.badpost; // since we reloaded, update cache again.
   if (prev <= 0) return 0;

   // early check for time (must do again later)
   day = (now - cuser.timeremovebadpost ) / DAY_SECONDS;
   if (day < BADPOST_CLEAR_DURATION) {
       vmsgf("���U���i�H�ӽиѰ��|�� %d �ѡC", BADPOST_CLEAR_DURATION - day);
       return 0;
   }

   // �Y�� user �|�@�����ѡA��]�����F�� vmsg �אּ getdata.
   clear();
   // �L�᪺ disclaimer...
   mvprints(1, 0, "�w�p�h��N�� %d �g�ܬ� %d �g�A�T�w��[y/N]? ", prev, prev-1);
   do {
       if (vgets(ans, sizeof(ans), VGET_LOWERCASE | VGET_ASCII_ONLY) < 1 ||
           ans[0] != 'y') { pass_verify = 0; break; }
       mvprints(3, 0, "���@�N��u����W�w,�ճW,�H�ΪO�W[y/N]? ");
       if (vgets(ans, sizeof(ans), VGET_LOWERCASE | VGET_ASCII_ONLY) < 1 ||
           ans[0] != 'y') { pass_verify = 0; break; }
       mvprints(5, 0, "���@�N�L���P���[���ڸs,���x�O,�L���U�O�D�v�O[y/N]?");
       if (vgets(ans, sizeof(ans), VGET_LOWERCASE | VGET_ASCII_ONLY) < 1 ||
           ans[0] != 'y') { pass_verify = 0; break; }
       mvprints(7, 0, "���@�N�ԷV�o���N�q����,����|����,����O�s�i[y/N]?");
       if (vgets(ans, sizeof(ans), VGET_LOWERCASE | VGET_ASCII_ONLY) < 1 ||
           ans[0] != 'y') { pass_verify = 0; break; }
   } while (0);

   if(!pass_verify)
   {
       vmsg("�^�����~�A�R�����ѡC�ХJ�ӬݲM���W�P�t�ΰT����A�ӥӽЧR��.");
       return 0;
   }

   if (pwcuCancelBadpost() != 0) {
       vmsg("�R�����ѡA�Ь����ȤH���C");
       return 0;
   }

   log_filef("log/cancelbadpost.log", LOG_CREAT,
	   "%s %s �R���@�g�h�� (%d -> %d �g)\n",
	   Cdate(&now), cuser.userid, prev, cuser.badpost);

   vmsgf("���߱z�w���\\�R���@�g�h�� (�� %d �ܬ� %d �g)",
	   prev, cuser.badpost);
   return 0;
}

void
user_display(const userec_t * u, int adminmode)
{
    int             diff = 0;
    char            genbuf[200];

    // Many fields are available (and have sync issue) in user->query,
    // so let's minimize fields here.

    clrtobot();
    prints(
	   "        " ANSI_COLOR(30;41) "�r�s�r�s�r�s" ANSI_RESET
           "  " ANSI_COLOR(1;30;45) "    �� �� ��" " �� ��        "
	   "     " ANSI_RESET "  " ANSI_COLOR(30;41) "�r�s�r�s�r�s" ANSI_RESET
           "\n");
    prints("\t�N���ʺ�: %s (%s)\n", u->userid, u->nickname);
    prints("\t�u��m�W: %s", u->realname);
#if FOREIGN_REG_DAY > 0
    prints(" %s%s",
	   u->uflag & UF_FOREIGN ? "(�~�y: " : "",
	   u->uflag & UF_FOREIGN ?
		(u->uflag & UF_LIVERIGHT) ? "�ä[�~�d)" : "�����o�~�d�v)"
		: "");
#elif defined(FOREIGN_REG)
    prints(" %s", u->uflag & UF_FOREIGN ? "(�~�y)" : "");
#endif
    outs("\n"); // end of realname
    prints("\t¾�~�Ǿ�: %s\n", u->career);
    prints("\t�~��a�}: %s\n", u->address);

    prints("\t�q�l�H�c: %s\n", u->email);
    prints("\t%6s��: %d " MONEYNAME "\n", BBSMNAME, u->money);
    prints("\t�O�_���~: %s��18��\n", u->over_18 ? "�w" : "��");

    prints("\t���U���: %s (�w�� %d ��)\n",
	    Cdate(&u->firstlogin), (int)((now - u->firstlogin)/DAY_SECONDS));

    if (adminmode) {
	strcpy(genbuf, "bTCPRp#@XWBA#VSM0123456789ABCDEF");
	for (diff = 0; diff < 32; diff++)
	    if (!(u->userlevel & (1 << diff)))
		genbuf[diff] = '-';
	prints("\t�b���v��: %s\n", genbuf);
	prints("\t�{�Ҹ��: %s\n", u->justify);
    }


    sethomedir(genbuf, u->userid);
    prints("\t�p�H�H�c: %d ��  (�ʶR�H�c: %d ��)\n",
	   get_num_records(genbuf, sizeof(fileheader_t)),
	   u->exmailbox);
    prints("\t�ϥΰO��: " STR_LOGINDAYS " %d " STR_LOGINDAYS_QTY
           ,u->numlogindays);
    prints(" / �峹 %d �g\n", u->numposts);

    if (adminmode) {
        prints("\t�̫�W�u: %s (�����ɨC��W�[) / %s\n",
               Cdate(&u->lastlogin), u->lasthost);
    } else {
	diff = (now - login_start_time) / 60;
	prints("\t���d����: %d �p�� %2d ��\n",
	       diff / 60, diff % 60);
    }

    /* Thor: �Q�ݬݳo�� user �O���ǪO���O�D */
    if (u->userlevel >= PERM_BM) {
	int             i;
	boardheader_t  *bhdr;

	outs("\t����O�D: ");

	for (i = num_boards(), bhdr = bcache; i > 0; i--, bhdr++) {
	    if ( is_uBM(bhdr->BM, u->userid)) {
		outs(bhdr->brdname);
		outc(' ');
	    }
	}
	outc('\n');
    }

    // conditional fields
#ifdef ASSESS
    prints("\t�h��ƥ�: %u\n", (unsigned int)u->badpost);
#endif // ASSESS

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
		prints("%12s�Ѱ�ۧڴy�z: %s", chess_type[i], genbuf + 11);
		fclose(fp);
	    }
	}
    }
#endif

#ifdef PLAY_ANGEL
    if (adminmode)
	prints("\t�p �� ��: %s\n",
		u->myangel[0] ? u->myangel : "�L");
#endif

    outs("        " ANSI_COLOR(30;41) "�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r�s�r"
	 "�s�r�s�r�s�r�s" ANSI_RESET);
    if (!adminmode)
    {
	outs((u->userlevel & PERM_LOGINOK) ?
		"\n�z�����U�{�Ǥw�g�����A�w��[�J����" :
		"\n�p�G�n���@�v���A�аѦҥ������G���z���U");
    } else {
	// XXX list user pref here
	int i;
	static const char *uflag_desc[] = {
	    "�ڦ��~�H",
	    "�s�O�[�̷R",
	    "�~��",
	    "�~�d�v",
	};
	static uint32_t uflag_mask[] = {
	    UF_REJ_OUTTAMAIL,
	    UF_FAV_ADDNEW,
	    UF_FOREIGN,
	    UF_LIVERIGHT,
	};
	char buf[PATHLEN];

	prints("\n�䥦��T: [%s]", (u->userlevel & PERM_LOGINOK) ?
		"�w���U" : "�����U");
	sethomefile(buf, u->userid, FN_FORWARD);
	if (dashs(buf) > 0)
	    outs("[�۰���H]");

	for (i = 0; (size_t)i < sizeof(uflag_mask)/sizeof(uflag_mask[0]); i++)
	{
	    if (!(u->uflag & uflag_mask[i]))
		continue;
	    prints("[%s]", uflag_desc[i]);
	}
	prints("\n");
    }
}

void
mail_violatelaw(const char *crime, const char *police, const char *reason, const char *result)
{
    char            genbuf[200];
    fileheader_t    fhdr;
    FILE           *fp;

    sendalert(crime,  ALERT_PWD_PERM);

    sethomepath(genbuf, crime);
    stampfile(genbuf, &fhdr);
    if (!(fp = fopen(genbuf, "w")))
	return;
    fprintf(fp, "�@��: [" BBSMNAME "ĵ�]\n"
	    "���D: [���i] �H�k���i\n"
	    "�ɶ�: %s\n", ctime4(&now));
    fprintf(fp,
	    ANSI_COLOR(1;32) "%s" ANSI_RESET "�P�M�G\n     " ANSI_COLOR(1;32) "%s" ANSI_RESET
	    "�]" ANSI_COLOR(1;35) "%s" ANSI_RESET "�欰�A\n"
	    "�H�ϥ������W�A�B�H" ANSI_COLOR(1;35) "%s" ANSI_RESET "�A�S���q��\n\n"
	    "�Ш� " BN_LAW " �d�߬����k�W��T�A�ñq�D���i�J:\n"
	    "(P)lay�i�T�ֻP�𶢡j=>(P)ay�i" BBSMNAME2 "�q�c�� �j=> (1)ViolateLaw ú�@��\n"
	    "�Hú��@��C\n",
	    police, crime, reason, result);
    fclose(fp);
    strcpy(fhdr.title, "[���i] �H�k�P�M���i");
    strcpy(fhdr.owner, "[" BBSMNAME "ĵ�]");
    sethomedir(genbuf, crime);
    append_record(genbuf, &fhdr, sizeof(fhdr));
}

void
kick_all(const char *user)
{
   userinfo_t *ui;
   int num = searchuser(user, NULL), i=1;
   while((ui = (userinfo_t *) search_ulistn(num, i)) != NULL)
       {
         if(ui == currutmp) i++;
         if ((ui->pid <= 0 || kill(ui->pid, SIGHUP) == -1))
                         purge_utmp(ui);
         log_usies("KICK ALL", user);
       }
}

void
violate_law(userec_t * u, int unum)
{
    char            ans[4], ans2[4];
    char            reason[128];
    move(1, 0);
    clrtobot();
    move(2, 0);
    outs("(1)Cross-post (2)�õo�s�i�H (3)�õo�s��H\n");
    outs("(4)���Z���W�ϥΪ� (8)��L�H�@��B�m�欰\n(9)�� id �欰\n");
    getdata(5, 0, "(0)����", ans, 3, DOECHO);
    switch (ans[0]) {
    case '1':
	strcpy(reason, "Cross-post");
	break;
    case '2':
	strcpy(reason, "�õo�s�i�H");
	break;
    case '3':
	strcpy(reason, "�õo�s��H");
	break;
    case '4':
	while (!getdata(7, 0, "�п�J�Q���|�z�ѥH�ܭt�d�G", reason, 50, DOECHO));
	strcat(reason, "[���Z���W�ϥΪ�]");
	break;
    case '8':
    case '9':
	while (!getdata(6, 0, "�п�J�z�ѥH�ܭt�d�G", reason, 50, DOECHO));
	break;
    default:
	return;
    }
    getdata(7, 0, msg_sure_ny, ans2, 3, LCECHO);
    if (*ans2 != 'y')
	return;
    if (ans[0] == '9') {
	if (HasUserPerm(PERM_POLICE) && u->numlogindays > 60)
	{
	    vmsg("�ϥΪ�" STR_LOGINDAYS "�W�L 60�A�L�k�尣�C");
	    return;
	}

        kick_all(u->userid);
	delete_allpost(u->userid);
        ban_usermail(u, reason);
        kill_user(unum, u->userid);
	post_violatelaw(u->userid, cuser.userid, reason, "�尣 ID");
    } else {
        kick_all(u->userid);
	u->userlevel |= PERM_VIOLATELAW;
	u->timeviolatelaw = now;
	u->vl_count++;
	passwd_sync_update(unum, u);
        if (*ans == '1' || *ans == '2')
            delete_allpost(u->userid);
	post_violatelaw(u->userid, cuser.userid, reason, "�@��B��");
	mail_violatelaw(u->userid, "����ĵ��", reason, "�@��B��");
    }
    pressanykey();
}

void Customize(void)
{
    char    done = 0;
    int     dirty = 0;
    int     key;

    const int col_opt = 54;

    /* cuser.uflag settings */
    static const unsigned int masks1[] = {
	UF_ADBANNER,
	UF_ADBANNER_USONG,
	UF_REJ_OUTTAMAIL,
	UF_DEFBACKUP,
        UF_SECURE_LOGIN,
	UF_FAV_ADDNEW,
	UF_FAV_NOHILIGHT,
	UF_NO_MODMARK	,
	UF_COLORED_MODMARK,
#ifdef DBCSAWARE
	UF_DBCS_AWARE,
	UF_DBCS_DROP_REPEAT,
	UF_DBCS_NOINTRESC,
#endif
        UF_CURSOR_ASCII,
#ifdef USE_PFTERM
        UF_MENU_LIGHTBAR,
#endif
#ifdef PLAY_ANGEL
        UF_NEW_ANGEL_PAGER,
#endif
	0,
    };

    static const char* desc1[] = {
	"ADBANNER   ��ܰʺA�ݪO",
	"ADBANNER   ��ܨϥΪ̤߱��I��(�ݶ}�ҰʺA�ݪO)",
	"MAIL       �ڦ����~�H",
	"BACKUP     �w�]�ƥ��H��P�䥦�O��", //"�P��ѰO��",
        "LOGIN      �u���\\�ϥΦw���s�u(ex, ssh)�n�J",
	"MYFAV      �s�O�۰ʶi�ڪ��̷R",
	"MYFAV      �����ܧڪ��̷R",
	"MODMARK    ���ä峹�ק�Ÿ�(����/�פ�) (~)",
	"MODMARK    �ϥΦ�m�N���ק�Ÿ� (+)",
#ifdef DBCSAWARE
	"DBCS       �۰ʰ������줸�r��(�p��������)",
	"DBCS       �����s�u�{�������줸�r���e�X�����ƫ���",
	"DBCS       �T��b���줸���ϥΦ�X(�h���@�r����)",
#endif
        "CURSOR     �ϥηs��²�ƴ��",
#ifdef USE_PFTERM
        "CURSOR     (�����)�ҥΥ��ο��t��",
#endif
#ifdef PLAY_ANGEL
        "ANGEL      (�p�Ѩ�)�ҥηs�����٩I�s���]�w�ɭ�",
#endif
	0,
    };

    while ( !done ) {
	int i = 0, ia = 0; /* general uflags */
	int iax = 0; /* extended flags */

	clear();
	showtitle("�ӤH�Ƴ]�w", "�ӤH�Ƴ]�w");
	move(2, 0);
	outs("�z�ثe���ӤH�Ƴ]�w: \n");
	prints(ANSI_COLOR(32)"   %-11s%-*s%s" ANSI_RESET "\n",
		"����", col_opt-11,
		"�y�z", "�]�w��");
	move(4, 0);

	/* print uflag options */
	for (i = 0; masks1[i]; i++, ia++)
	{
#ifdef PLAY_ANGEL
            // XXX dirty hack: ANGEL must be in end of list.
            if (strstr(desc1[i], "ANGEL ") == desc1[i] &&
                !HasUserPerm(PERM_ANGEL)) {
                ia--;
                continue;
            }
#endif
	    clrtoeol();
	    prints( ANSI_COLOR(1;36) "%c" ANSI_RESET
		    ". %-*s%s\n",
		    'a' + ia,
		    col_opt,
		    desc1[i],
		    HasUserFlag(masks1[i]) ?
		    ANSI_COLOR(1;36) "�O" ANSI_RESET : "�_");
	}
	/* extended stuff */
	{
	    static const char *wm[PAGER_UI_TYPES+1] =
		{"�@��", "�i��", "����", ""};

	    prints("%c. %-*s%s\n",
		    '1' + iax++,
		    col_opt,
		    "PAGER      ���y�Ҧ�",
		    wm[cuser.pager_ui_type % PAGER_UI_TYPES]);
#ifdef PLAY_ANGEL
            // TODO move this to Ctrl-U Ctrl-P.
	    if (HasUserPerm(PERM_ANGEL))
	    {
		static const char *msgs[ANGELPAUSE_MODES] = {
		    "�}�� (�����Ҧ��p�D�H�o��)",
		    "���� (�u�����w�^���L���p�D�H�����D)",
		    "���� (������Ҧ��p�D�H�����D)",
		};
		prints("%c. %s%s\n",
			'1' + iax++,
			"ANGEL      �p�Ѩϯ��٩I�s��: ",
			msgs[currutmp->angelpause % ANGELPAUSE_MODES]);
	    }
#endif // PLAY_ANGEL
	}

	/* input */
	key = vmsgf("�Ы� [a-%c,1-%c] �����]�w�A�䥦���N�䵲��: ",
		'a' + ia-1, '1' + iax -1);

	if (key >= 'a' && key < 'a' + ia)
	{
	    /* normal pref */
	    key -= 'a';

            if (masks1[key] == UF_SECURE_LOGIN && !mbbsd_is_secure_connection()) {
                vmsg("�z�����ϥΦw���s�u�~��ק惡�]�w");
                continue;
            }

	    dirty = 1;
	    pwcuToggleUserFlag(masks1[key]);
	    continue;
	}

	if (key < '1' || key >= '1' + iax)
	{
	    done = 1; continue;
	}
	/* extended keys */
	key -= '1';

	switch(key)
	{
	    case 0:
		{
		    pwcuSetPagerUIType((cuser.pager_ui_type +1) % PAGER_UI_TYPES_USER);
		    vmsg("�ק���y�Ҧ���Х��`���u�A���s�W�u");
		    dirty = 1;
		}
		continue;
	}
#ifdef PLAY_ANGEL
	if( HasUserPerm(PERM_ANGEL) ){
	    if (key == iax-1)
	    {
		angel_toggle_pause();
		// dirty = 1; // pure utmp change does not need pw dirty
		continue;
	    }
	}
#endif //PLAY_ANGEL
    }

    grayout(1, b_lines-2, GRAYOUT_DARK);
    move(b_lines-1, 0); clrtoeol();

    if(dirty)
    {
	outs("�]�w�w�x�s�C\n");
    } else {
	outs("�����]�w�C\n");
    }

    redrawwin(); // in case we changed output pref (like DBCS)
    vmsg("�]�w����");
}

static void set_chess(const char *name, int y,
                      uint16_t *p_win, uint16_t *p_lose, uint16_t *p_tie) {
    char buf[STRLEN];
    char prompt[STRLEN];
    char *p;
    char *strtok_pos;
    snprintf(buf, sizeof(buf), "%d/%d/%d", *p_win, *p_lose, *p_tie);
    snprintf(prompt, sizeof(prompt), "%s ���Z ��/��/�M:", name);
    if (!getdata_str(y, 0, prompt, buf, 5 * 3 + 3, DOECHO, buf))
        return;
    p = strtok_r(buf, "/\r\n", &strtok_pos);
    if (!p) return;
    *p_win = atoi(p);
    p = strtok_r(NULL, "/\r\n", &strtok_pos);
    if (!p) return;
    *p_lose = atoi(p);
    p = strtok_r(NULL, "/\r\n", &strtok_pos);
    if (!p) return;
    *p_tie = atoi(p);
}

void
uinfo_query(const char *orig_uid, int adminmode, int unum)
{
    userec_t        x;
    int    i = 0, fail;
    int             ans;
    char            buf[STRLEN];
    char            genbuf[200];
    char	    pre_confirmed = 0;
    int y = 0;
    int perm_changed;
    int mail_changed;
    int money_changed;
    int tokill = 0;
    int changefrom = 0;
    int xuid;

    fail = 0;
    mail_changed = money_changed = perm_changed = 0;

    // verify unum
    xuid = getuser(orig_uid, &x);
    if (xuid == 0)
    {
	vmsgf("�䤣��ϥΪ� %s�C", orig_uid);
	return;
    }
    if (xuid != unum)
    {
	move(b_lines-1, 0); clrtobot();
	prints(ANSI_COLOR(1;31) "���~��T: unum=%d (lookup xuid=%d)"
		ANSI_RESET "\n", unum, xuid);
	vmsg("�t�ο��~: �ϥΪ̸�Ƹ��X (unum) ���X�C�Ц� " BN_BUGREPORT "���i�C");
	return;
    }
    if (strcmp(orig_uid, x.userid) != 0)
    {
	move(b_lines-1, 0); clrtobot();
	prints(ANSI_COLOR(1;31) "���~��T: userid=%s (lookup userid=%s)"
		ANSI_RESET "\n", orig_uid, x.userid);
	vmsg("�t�ο��~: �ϥΪ� ID �O�������X�C�Ц� " BN_BUGREPORT "���i�C");
	return;
    }

    ans = vans(adminmode ?
    "(1)����(2)�K�X(3)�v��(4)��b(5)��ID(6)�d��(7)�f�P(8)�h��(M)�H�c [0]���� " :
    "�п�� (1)�ק��� (2)�]�w�K�X (C)�ӤH�Ƴ]�w [0]���� ");

    if (ans > '2' && ans != 'c' && !adminmode)
	ans = '0';

    if (ans == '1' || ans == '3' || ans == 'm') {
	clear();
	y = 1;
	move(y++, 0);
	outs(msg_uid);
	outs(x.userid);
    }

    if (adminmode && ((ans >= '1' && ans <= '8') || ans == 'm') &&
	search_ulist(unum))
    {
	if (vans("�ϥΪ̥ثe���b�u�W�A�ק��Ʒ|����U�u�C�T�w�n�~��ܡH (y/N): ")
		!= 'y')
		return;
	if (unum == usernum &&
	    vans("�z���չϭק�ۤv���b���F�o�i��|�y���b���l���A�T�w�n�~��ܡH (y/N): ")
	    != 'y')
		return;
    }
    switch (ans) {
    case 'c':
	// Customize can only apply to self.
	if (!adminmode)
	    Customize();
	return;

    case 'm':
	while (1) {
	    getdata_str(y, 0,
                    adminmode ? "E-Mail (�����ܧ󤣻ݻ{��): " :
                                "�q�l�H�c [�ܰʭn���s�{��]�G",
                    buf, sizeof(x.email), DOECHO, x.email);

	    strip_blank(buf, buf);

	    // fast break
	    if (!buf[0] || strcasecmp(buf, "x") == 0)
		break;

	    if (!check_regmail(buf))
		continue;

	    // XXX �o�̤]�n emaildb_check
#ifdef USE_EMAILDB
	    {
		int email_count = emaildb_check_email(cuser.userid, buf);

		if (email_count < 0)
		    vmsg("�Ȯɤ����\\ email �{��, �еy��A��");
		else if (email_count >= EMAILDB_LIMIT)
		    vmsg("���w�� E-Mail �w���U�L�h�b��, �ШϥΨ�L E-Mail");
		else
		    break; // valid mail
		// invalid mail
		continue;
	    }
#endif
	    // valid mail.
	    break;
	}
	y++;

	// admins may want to use special names
	if (buf[0] &&
		strcmp(buf, x.email) &&
		(strchr(buf, '@') || adminmode)) {

	    // TODO �o�̤]�n emaildb_check
#ifdef USE_EMAILDB
	    if (emaildb_update_email(x.userid, buf) < 0) {
		vmsg("�Ȯɤ����\\ email �{��, �еy��A��");
		break;
	    }
#endif
	    strlcpy(x.email, buf, sizeof(x.email));
	    mail_changed = 1;

            //  XXX delregcodefile �|�� cuser.userid...
            if (!adminmode)
                delregcodefile();
	}
	break;

    case '1':
	move(0, 0);
	outs("�гv���ק�C");

	getdata_buf(y++, 0, " �� ��  �G", x.nickname,
		    sizeof(x.nickname), DOECHO);
	if (adminmode) {
	    getdata_buf(y++, 0, "�u��m�W�G",
			x.realname, sizeof(x.realname), DOECHO);
	    getdata_buf(y++, 0, "�~��a�}�G",
			x.address, sizeof(x.address), DOECHO);
	    getdata_buf(y++, 0, "�Ǿ�¾�~�G", x.career,
		    sizeof(x.career), DOECHO);
	}

        do {
            snprintf(buf, sizeof(buf), x.over_18 ? "y" : "n");
            mvouts(y, 0, "���������ݪO�i�঳����Ť��e�u�A�X���~�H�h�\\Ū�C");
            getdata_buf(y+1, 0,"�z�O�_�~���Q�K���æP�N�[�ݦ����ݪO"
                        "(�Y�_�п�Jn)[y/n]: ", buf, 3, LCECHO);
        } while (buf[0] != 'y' && buf[0] != 'n');
        x.over_18 = buf[0] == 'y' ? 1 : 0;
        mvprints(y, 0, "�O�_�~���Q�K��: %s\n\n", x.over_18 ? "�O" : "�_");
        y++;

#ifdef PLAY_ANGEL
	if (adminmode) {
	    const char* prompt;
	    userec_t the_angel;
	    if (x.myangel[0] == 0 || x.myangel[0] == '-' ||
		    (getuser(x.myangel, &the_angel) &&
		     the_angel.userlevel & PERM_ANGEL))
		prompt = "�p �� �ϡG";
	    else
		prompt = "�p�Ѩϡ]���b���w�L�p�Ѩϸ��^�G";
	    while (1) {
	        userec_t xuser;
		getdata_str(y, 0, prompt, buf, IDLEN + 1, DOECHO,
			x.myangel);

		if (buf[0] == 0 || strcmp(buf, "-") == 0) {
		    strlcpy(x.myangel, buf, IDLEN + 1);
                    break;
                }

                if (strcmp(x.myangel, buf) == 0)
                    break;

                if (getuser(buf, &xuser) && (xuser.userlevel & PERM_ANGEL)) {
		    strlcpy(x.myangel, xuser.userid, IDLEN + 1);
                    x.timesetangel = now;
                    log_filef(BBSHOME "/log/changeangel.log",LOG_CREAT,
                              "%s ���� %s �ק� %s ���p�ѨϬ� %s\n",
                              Cdatelite(&now), cuser.userid, x.userid, x.myangel);
		    break;
		}

		prompt = "�p �� �ϡG";
	    }
            ++y;
	}
#endif

#ifdef CHESSCOUNTRY
	{
	    int j, k;
	    FILE* fp;
	    for(j = 0; j < 2; ++j){
		sethomefile(genbuf, x.userid, chess_photo_name[j]);
		fp = fopen(genbuf, "r");
		if(fp != NULL){
		    FILE* newfp;
		    char mybuf[200];
		    for(k = 0; k < 11; ++k)
			fgets(genbuf, 200, fp);
		    fgets(genbuf, 200, fp);
		    chomp(genbuf);

		    snprintf(mybuf, 200, "%s�Ѱ�ۧڴy�z�G", chess_type[j]);
		    getdata_buf(y, 0, mybuf, genbuf + 11, 80 - 11, DOECHO);
		    ++y;

		    sethomefile(mybuf, x.userid, chess_photo_name[j]);
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

			sethomefile(genbuf, x.userid, chess_photo_name[j]);
			sethomefile(mybuf, x.userid, chess_photo_name[j]);
			strcat(mybuf, ".new");

			Rename(mybuf, genbuf);
		    }
		    fclose(fp);
		}
	    }
	}
#endif

	if (adminmode) {
	    int tmp;
	    if (HasUserPerm(PERM_BBSADM)) {
		snprintf(genbuf, sizeof(genbuf), "%d", x.money);
		if (getdata_str(y++, 0, BBSMNAME "���G", buf, 10, DOECHO, genbuf))
		    if ((tmp = atol(buf)) != 0) {
			if (tmp != x.money) {
			    money_changed = 1;
			    changefrom = x.money;
			    x.money = tmp;
			}
		    }
	    }
	    snprintf(genbuf, sizeof(genbuf), "%d", x.exmailbox);
	    if (getdata_str(y++, 0, "�ʶR�H�c�G", buf, 6,
			    DOECHO, genbuf))
		if ((tmp = atoi(buf)) != 0)
		    x.exmailbox = (int)tmp;

	    getdata_buf(y++, 0, "�{�Ҹ�ơG", x.justify,
			sizeof(x.justify), DOECHO);
	    getdata_buf(y++, 0, "�̪���{�����G",
			x.lasthost, sizeof(x.lasthost), DOECHO);

	    while (1) {
		struct tm t = {0};
		time4_t clk = x.lastlogin;
		localtime4_r(&clk, &t);
		snprintf(genbuf, sizeof(genbuf), "%04i/%02i/%02i %02i:%02i:%02i",
			t.tm_year + 1900, t.tm_mon+1, t.tm_mday,
			t.tm_hour, t.tm_min, t.tm_sec);
		if (getdata_str(y, 0, "�̪�W�u�ɶ��G", buf, 20, DOECHO, genbuf) != 0) {
		    int y, m, d, hh, mm, ss;
		    if (ParseDateTime(buf, &y, &m, &d, &hh, &mm, &ss))
			continue;
		    t.tm_year = y-1900;
		    t.tm_mon  = m-1;
		    t.tm_mday = d;
		    t.tm_hour = hh;
		    t.tm_min  = mm;
		    t.tm_sec  = ss;
		    clk = mktime(&t);
		    if (!clk)
			continue;
		    x.lastlogin= clk;
		}
		y++;
		break;
	    }

	    do {
		int max_days = (x.lastlogin - x.firstlogin) / DAY_SECONDS;
		snprintf(genbuf, sizeof(genbuf), "%d", x.numlogindays);
		if (getdata_str(y++, 0, STR_LOGINDAYS "�G", buf, 10, DOECHO, genbuf))
		    if ((tmp = atoi(buf)) >= 0)
			x.numlogindays = tmp;
		if ((int)x.numlogindays > max_days)
		{
		    x.numlogindays = max_days;
		    vmsgf("�ھڦ��ϥΪ̳̫�W�u�ɶ��A�̤j�Ȭ� %d.", max_days);
		    move(--y, 0); clrtobot();
		    continue;
		}
		break;
	    } while (1);

	    snprintf(genbuf, sizeof(genbuf), "%d", x.numposts);
	    if (getdata_str(y++, 0, "�峹�ƥءG", buf, 10, DOECHO, genbuf))
		if ((tmp = atoi(buf)) >= 0)
		    x.numposts = tmp;
	    move(y-1, 0); clrtobot();
	    prints("�峹�ƥءG %d (�h: %d, �ק�h��ƽпﶵ8)\n",
		    x.numposts, x.badpost);

	    snprintf(genbuf, sizeof(genbuf), "%d", x.vl_count);
	    if (getdata_str(y++, 0, "�H�k�O���G", buf, 10, DOECHO, genbuf))
		if ((tmp = atoi(buf)) >= 0)
		    x.vl_count = tmp;

            set_chess("���l��", y++, &x.five_win, &x.five_lose, &x.five_tie);
            set_chess("�H��", y++, &x.chc_win, &x.chc_lose, &x.chc_tie);
            set_chess("���", y++, &x.go_win, &x.go_lose, &x.go_tie);
            set_chess("�t��", y++, &x.dark_win, &x.dark_lose, &x.dark_tie);
	    y -= 4; // rollback games set to get more space
	    move(y++, 0); clrtobot();
	    prints("����: ���l:%d/%d/%d �H:%d/%d/%d ��:%d/%d/%d �t:%d/%d/%d\n",
		    x.five_win, x.five_lose, x.five_tie,
		    x.chc_win, x.chc_lose, x.chc_tie,
		    x.go_win, x.go_lose, x.go_tie,
		    x.dark_win, x.dark_lose, x.dark_tie);
#ifdef FOREIGN_REG
	    if (getdata_str(y++, 0, "��b 1)�x�W 2)��L�G", buf, 2, DOECHO, x.uflag & UF_FOREIGN ? "2" : "1"))
		if ((tmp = atoi(buf)) > 0){
		    if (tmp == 2){
			x.uflag |= UF_FOREIGN;
		    }
		    else
			x.uflag &= ~UF_FOREIGN;
		}
	    if (x.uflag & UF_FOREIGN)
		if (getdata_str(y++, 0, "�ä[�~�d�v 1)�O 2)�_�G", buf, 2, DOECHO, x.uflag & UF_LIVERIGHT ? "1" : "2")){
		    if ((tmp = atoi(buf)) > 0){
			if (tmp == 1){
			    x.uflag |= UF_LIVERIGHT;
			    x.userlevel |= (PERM_LOGINOK | PERM_POST);
			}
			else{
			    x.uflag &= ~UF_LIVERIGHT;
			    x.userlevel &= ~(PERM_LOGINOK | PERM_POST);
			}
		    }
		}
#endif
	}

        if (!adminmode) {
            mvouts(b_lines-1, 0,
                   "�䥦��ƭY�ݭק�Ь� " BN_ID_PROBLEM " �ݪO\n");
        }
	break;

    case '2':
	y = 19;
	if (!adminmode) {
            if (!getdata(y++, 0, "�п�J��K�X�G", buf, PASS_INPUT_LEN + 1,
                         PASSECHO) ||
		!checkpasswd(x.passwd, buf)) {
		outs("\n\n�z��J���K�X�����T\n");
		fail++;
		break;
	    }
	}
        if (!getdata(y++, 0, "�г]�w�s�K�X�G", buf, PASS_INPUT_LEN + 1,
                     PASSECHO)) {
	    outs("\n\n�K�X�]�w����, �~��ϥ��±K�X\n");
	    fail++;
	    break;
	}
	strlcpy(genbuf, buf, PASSLEN);

	move(y+1, 0);
	outs("�Ъ`�N�]�w�K�X�u���e�K�Ӧr�����ġA�W�L���N�۰ʩ����C");

	getdata(y++, 0, "���ˬd�s�K�X�G", buf, PASS_INPUT_LEN + 1, PASSECHO);
	if (strncmp(buf, genbuf, PASSLEN)) {
	    outs("\n\n�s�K�X�T�{����, �L�k�]�w�s�K�X\n");
	    fail++;
	    break;
	}
	buf[8] = '\0';
	strlcpy(x.passwd, genpasswd(buf), sizeof(x.passwd));

	// for admin mode, do verify after.
	if (adminmode)
	{
            FILE *fp;
	    char  witness[3][IDLEN+1], title[100];
	    int uid;
	    for (i = 0; i < 3; i++) {
		if (!getdata(y + i, 0, "�п�J��U�ҩ����ϥΪ̡G",
			     witness[i], sizeof(witness[i]), DOECHO)) {
		    outs("\n����J�h�L�k���\n");
		    fail++;
		    break;
		} else if (!(uid = searchuser(witness[i], NULL))) {
		    outs("\n�d�L���ϥΪ�\n");
		    fail++;
		    break;
		} else {
		    userec_t        atuser;
		    passwd_sync_query(uid, &atuser);
                    if (!(atuser.userlevel & PERM_LOGINOK)) {
                        outs("\n�ϥΪ̥��q�L�{�ҡA�Э��s��J�C\n");
                        i--;
                    } else if (atuser.numlogindays < 6*30) {
			outs("\n" STR_LOGINDAYS "���W�L 180�A�Э��s��J\n");
			i--;
		    }
		    // Adjust upper or lower case
                    strlcpy(witness[i], atuser.userid, sizeof(witness[i]));
		}
	    }
	    y += 3;

	    if (i < 3 || fail > 0 || vans(msg_sure_ny) != 'y')
	    {
		fail++;
		break;
	    }
	    pre_confirmed = 1;

	    sprintf(title, "%s ���K�X���]�q�� (by %s)",x.userid, cuser.userid);
            unlink("etc/updatepwd.log");
	    if(! (fp = fopen("etc/updatepwd.log", "w")))
	    {
		move(b_lines-1, 0); clrtobot();
		outs("�t�ο��~: �L�k�إ߳q���ɡA�Ц� " BN_BUGREPORT " ���i�C");
		fail++; pre_confirmed = 0;
		break;
	    }

	    fprintf(fp, "%s �n�D�K�X���]:\n"
		    "���ҤH�� %s, %s, %s",
		    x.userid, witness[0], witness[1], witness[2] );
	    fclose(fp);

	    post_file(BN_SECURITY, title, "etc/updatepwd.log", "[�t�Φw����]");
	    mail_id(x.userid, title, "etc/updatepwd.log", cuser.userid);
	    for(i=0; i<3; i++)
	    {
		mail_id(witness[i], title, "etc/updatepwd.log", cuser.userid);
	    }
	}
	break;

    case '3':
	{
	    unsigned int tmp = setperms(x.userlevel, str_permid);
	    if (tmp == x.userlevel)
		fail++;
	    else {
		perm_changed = 1;
		changefrom = x.userlevel;
		x.userlevel = tmp;
	    }
	}
	break;

    case '4':
	tokill = 1;
	{
	    char reason[STRLEN];
	    char title[STRLEN], msg[1024];
	    while (!getdata(b_lines-3, 0, "�п�J�z�ѥH�ܭt�d�G", reason, 50, DOECHO));
	    if (vans(msg_sure_ny) != 'y')
	    {
		fail++;
		break;
	    }
	    pre_confirmed = 1;
	    snprintf(title, sizeof(title),
		    "�R��ID: %s (����: %s)", x.userid, cuser.userid);
	    snprintf(msg, sizeof(msg),
		    "�b�� %s �ѯ��� %s ����R���A�z��:\n %s\n\n"
		    "�u��m�W:%s\n��}:%s\n�{�Ҹ��:%s\nEmail:%s\n",
		    x.userid, cuser.userid, reason,
		    x.realname, x.address, x.justify, x.email);
	    post_msg(BN_SECURITY, title, msg, "[�t�Φw����]");
	}
	break;

    case '5':
        mvouts(b_lines - 8, 0, "\n"
           "�w���ܦh�ϥΪ̷d���M���p�粒 ID �j�p�g�|�����L�k�ק�H�e�峹�A\n"
           "�B�����ֺ޲z/���@�����D�A"
           ANSI_COLOR(1;31)
           "�ҥH�а������ϥΪ̦ۦ�ӽЧ�j�p�g���A�ȡC\n" ANSI_RESET
           "���D�O���ȻݨD(�p�ѨM���ID) ���M�ФŨϥΦ��\\���j�p�g\n");
        clrtobot();

	if (getdata_str(b_lines - 3, 0, "�s���ϥΪ̥N���G", genbuf, IDLEN + 1,
			DOECHO, x.userid)) {
	    if (!is_validuserid(genbuf)) {
		outs("���~! ��J�� ID ���X�W�w\n");
		fail++;
	    } else if (searchuser(genbuf, NULL)) {
		outs("���~! �w�g���P�� ID ���ϥΪ�\n");
		fail++;
#if !defined(NO_CHECK_AMBIGUOUS_USERID) && defined(USE_REGCHECKD)
	    } else if ( regcheck_ambiguous_userid_exist(genbuf) > 0 &&
			vans("���N���L�������H�b���A"
                             "�T�w�ϥΪ̨S���n�F�a�ƶ�? [y/N] ") != 'y')
	    {
		    fail++;
#endif
	    } else
		strlcpy(x.userid, genbuf, sizeof(x.userid));
	}
	break;

    case '6':
	chicken_toggle_death(x.userid);
	break;

    case '7':
	violate_law(&x, unum);
	return;

    case '8':
#ifdef ASSESS
        reassign_badpost(x.userid);
#else
        vmsg("�����ثe���䴩�h��]�w�C");
#endif
        return;

    default:
	return;
    }

    if (fail) {
	pressanykey();
	return;
    }

    if (!pre_confirmed)
    {
	if (vans(msg_sure_ny) != 'y')
	    return;
    }

    // now confirmed. do everything directly.

    // perm_changed is by sysop only.
    if (perm_changed) {
	sendalert(x.userid,  ALERT_PWD_PERM); // force to reload perm
	post_change_perm(changefrom, x.userlevel, cuser.userid, x.userid);
#ifdef PLAY_ANGEL
        // TODO notify Angelbeats
	if (x.userlevel & ~changefrom & PERM_ANGEL) {
            angel_register_new(x.userid);
            mail_id(x.userid, "�ͻH���X�ӤF�I", "etc/angel_notify",
                    "[�ѨϤ��|]");
        }
#endif
    }

    if (strcmp(orig_uid, x.userid)) {
	char            src[STRLEN], dst[STRLEN];
	kick_all(orig_uid);
	sethomepath(src, orig_uid);
	sethomepath(dst, x.userid);
	Rename(src, dst);
	setuserid(unum, x.userid);

	// log change for security reasons.
	char title[STRLEN];
	snprintf(title, sizeof(title), "�ܧ�ID: %s -> %s (����: %s)",
		 orig_uid, x.userid, cuser.userid);
	post_msg(BN_SECURITY, title, title, "[�t�Φw����]");
    }
    if (mail_changed && !adminmode) {
	// wait registration.
	x.userlevel &= ~(PERM_LOGINOK | PERM_POST);
    }

    if (tokill) {
	kick_all(x.userid);
	delete_allpost(x.userid);
	kill_user(unum, x.userid);
	return;
    } else
	log_usies("SetUser", x.userid);

    if (money_changed) {
	char title[TTLEN+1];
	char msg[512];
	char reason[50];
	clrtobot();
	clear();
	while (!getdata(5, 0, "�п�J�z�ѥH�ܭt�d�G",
		    reason, sizeof(reason), DOECHO));

	snprintf(msg, sizeof(msg),
		"   ����" ANSI_COLOR(1;32) "%s" ANSI_RESET "��" ANSI_COLOR(1;32) "%s" ANSI_RESET "����"
		"�q" ANSI_COLOR(1;35) "%d" ANSI_RESET "�令" ANSI_COLOR(1;35) "%d" ANSI_RESET "\n"
		"   " ANSI_COLOR(1;37) "����%s�ק���z�ѬO�G%s" ANSI_RESET,
		cuser.userid, x.userid, changefrom, x.money,
		cuser.userid, reason);
	snprintf(title, sizeof(title),
		"[�w�����i] ����%s�ק�%s����", cuser.userid,
		x.userid);
	post_msg(BN_SECURITY, title, msg, "[�t�Φw����]");
	setumoney(unum, x.money);
    }

    passwd_sync_update(unum, &x);

    if (adminmode)
	kick_all(x.userid);
}

int
u_info(void)
{
    move(2, 0);
    reload_money();
    user_display(cuser_ref, 0);
    uinfo_query (cuser.userid, 0, usernum);
    pwcuReload();
    strlcpy(currutmp->nickname, cuser.nickname, sizeof(currutmp->nickname));
    return 0;
}

void
showplans_userec(userec_t *user)
{
    char genbuf[ANSILINELEN];

    if(user->userlevel & PERM_VIOLATELAW)
    {
        int can_save = ((user->userlevel & PERM_LOGINOK) &&
                        (user->userlevel & PERM_BASIC)) ? 1 : 0;

        prints(" " ANSI_COLOR(1;31) "���H�H�W %s" ANSI_RESET,
               can_save ? "�|��ú��@��" : "");

        if (user->vl_count)
            prints(" (�w�֭p %d ��)", user->vl_count);
	return;
    }

#ifdef CHESSCOUNTRY
    if (user_query_mode) {
	int    i = 0;
	FILE  *fp;

       sethomefile(genbuf, user->userid, chess_photo_name[user_query_mode - 1]);
	if ((fp = fopen(genbuf, "r")) != NULL)
	{
	    char   photo[6][ANSILINELEN];
	    int    kingdom_bid = 0;
	    int    win = 0, lost = 0;

	    move(7, 0);
	    while (i < 12 && fgets(genbuf, sizeof(genbuf), fp))
	    {
		chomp(genbuf);
		if (i < 6)  /* Ū�Ӥ��� */
		    strlcpy(photo[i], genbuf, sizeof(photo[i]));
		else if (i == 6)
		    kingdom_bid = atoi(genbuf);
		else
		    prints("%s %s\n", photo[i - 7], genbuf);

		i++;
	    }
	    fclose(fp);

	    if (user_query_mode == 1) {
		win = user->five_win;
		lost = user->five_lose;
	    } else if(user_query_mode == 2) {
		win = user->chc_win;
		lost = user->chc_lose;
	    }
	    prints("%s <�`�@���Z> %d �� %d ��\n", photo[5], win, lost);


	    /* �Ѱ���� */
	    setapath(genbuf, bcache[kingdom_bid - 1].brdname);
	    strlcat(genbuf, "/chess_ensign", sizeof(genbuf));
	    show_file(genbuf, 13, 10, SHOWFILE_ALLOW_COLOR);
	    return;
	}
    }
#endif /* defined(CHESSCOUNTRY) */

    sethomefile(genbuf, user->userid, fn_plans);
    if (!show_file(genbuf, 7, MAX_QUERYLINES, SHOWFILE_ALLOW_COLOR))
        prints("�m�ӤH�W���n%s �ثe�S���W��\n", user->userid);
}

void
showplans(const char *uid)
{
    userec_t user;
    if(getuser(uid, &user))
       showplans_userec(&user);
}
/*
 * return value: how many items displayed */
int
showsignature(char *fname, int *j, SigInfo *si)
{
    FILE           *fp;
    char            buf[ANSILINELEN];
    int             i, lines = t_lines;
    char            ch;

    clear();
    move(2, 0);
    lines -= 3;

    setuserfile(fname, "sig.0");
    *j = strlen(fname) - 1;
    si->total = 0;
    si->max = 0;

    for (ch = '1'; ch <= '9'; ch++) {
	fname[*j] = ch;
	if ((fp = fopen(fname, "r"))) {
	    si->total ++;
	    si->max = ch - '1';
	    if(lines > 0 && si->max >= si->show_start)
	    {
		int y = vgety() + 1;
		prints(ANSI_COLOR(36) "�i ñ�W��.%c �j" ANSI_RESET "\n", ch);
		lines--;
		if(lines > MAX_SIGLINES/2)
		    si->show_max = si->max;
		for (i = 0; lines > 0 && i < MAX_SIGLINES &&
			fgets(buf, sizeof(buf), fp) != NULL; i++)
		{
		    chomp(buf);
		    mvouts(y++, 0, buf);
		    lines--;
		}
		move(y, 0);
	    }
	    fclose(fp);
	}
    }
    if(lines > 0)
	si->show_max = si->max;
    return si->max;
}

int
u_editsig(void)
{
    int             aborted;
    char            ans[4];
    int             j, browsing = 0;
    char            genbuf[PATHLEN];
    SigInfo	    si;

    memset(&si, 0, sizeof(si));

browse_sigs:

    showsignature(genbuf, &j, &si);
    getdata(0, 0, (browsing || (si.max > si.show_max)) ?
	    "ñ�W�� (E)�s�� (D)�R�� (N)½�� (Q)�����H[Q] ":
	    "ñ�W�� (E)�s�� (D)�R�� (Q)�����H[Q] ",
	    ans, sizeof(ans), LCECHO);

    if(ans[0] == 'n')
    {
	si.show_start = si.show_max + 1;
	if(si.show_start > si.max)
	    si.show_start = 0;
	browsing = 1;
	goto browse_sigs;
    }

    aborted = 0;
    if (ans[0] == 'd')
	aborted = 1;
    else if (ans[0] == 'e')
	aborted = 2;

    if (aborted) {
	if (!getdata(1, 0, "�п��ñ�W��(1-9)�H[1] ", ans, sizeof(ans), DOECHO))
	    ans[0] = '1';
	if (ans[0] >= '1' && ans[0] <= '9') {
	    genbuf[j] = ans[0];
	    if (aborted == 1) {
		unlink(genbuf);
		outs(msg_del_ok);
	    } else {
		setutmpmode(EDITSIG);
		aborted = veditfile(genbuf);
		if (aborted != EDIT_ABORTED)
		    outs("ñ�W�ɧ�s����");
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

    getdata(b_lines - 1, 0, "�W�� (D)�R�� (E)�s�� [Q]�����H[Q] ",
	    genbuf, 3, LCECHO);

    if (genbuf[0] == 'e') {
	int             aborted;

	setutmpmode(EDITPLAN);
	setuserfile(genbuf, fn_plans);
	aborted = veditfile(genbuf);
	if (aborted != EDIT_ABORTED)
	    outs("�W����s����");
	pressanykey();
	return 0;
    } else if (genbuf[0] == 'd') {
	setuserfile(genbuf, fn_plans);
	unlink(genbuf);
	outmsg("�W���R������");
    }
    return 0;
}

/* �C�X�Ҧ����U�ϥΪ� */
struct ListAllUsetCtx {
    int usercounter;
    int totalusers;
    unsigned short u_list_special;
    int y;
};

static int
u_list_CB(void *data, int num, userec_t * uentp)
{
    char            permstr[8], *ptr;
    register int    level;
    struct ListAllUsetCtx *ctx = (struct ListAllUsetCtx*) data;
    (void)num;

    if (uentp == NULL) {
	move(2, 0);
	clrtoeol();
	prints(ANSI_REVERSE "  �ϥΪ̥N��   %-25s   �W��  �峹  %s  "
	       "�̪���{���     " ANSI_RESET "\n",
	       "�︹�ʺ�",
	       HasUserPerm(PERM_SEEULEVELS) ? "����" : "");
	ctx->y = 3;
	return 0;
    }
    if (bad_user_id(uentp->userid))
	return 0;

    if ((uentp->userlevel & ~(ctx->u_list_special)) == 0)
	return 0;

    if (ctx->y == b_lines) {
	int ch;
	prints(ANSI_COLOR(34;46) "  �w��� %d/%d �H(%d%%)  " ANSI_COLOR(31;47) "  "
	       "(Space)" ANSI_COLOR(30) " �ݤU�@��  " ANSI_COLOR(31) "(Q)" ANSI_COLOR(30) " ���}  " ANSI_RESET,
	       ctx->usercounter, ctx->totalusers, ctx->usercounter * 100 / ctx->totalusers);
	ch = vkey();
	if (ch == 'q' || ch == 'Q')
	    return -1;
	ctx->y = 3;
    }
    if (ctx->y == 3) {
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
	   uentp->nickname,
	   uentp->numlogindays, uentp->numposts,
	   HasUserPerm(PERM_SEEULEVELS) ? permstr : "", ptr);
    ctx->usercounter++;
    ctx->y++;
    return 0;
}

int
u_list(void)
{
    char            genbuf[3];
    struct ListAllUsetCtx data, *ctx = &data;

    setutmpmode(LAUSERS);
    ctx->u_list_special = ctx->usercounter = 0;
    ctx->totalusers = SHM->number;
    if (HasUserPerm(PERM_SEEULEVELS)) {
	getdata(b_lines - 1, 0, "�[�� [1]�S���� (2)�����H",
		genbuf, 3, DOECHO);
	if (genbuf[0] != '2')
	    ctx->u_list_special = PERM_BASIC | PERM_CHAT | PERM_PAGE | PERM_POST | PERM_LOGINOK | PERM_BM;
    }
    u_list_CB(ctx, 0, NULL);
    passwd_apply(ctx, u_list_CB);
    move(b_lines, 0);
    clrtoeol();
    prints(ANSI_COLOR(34;46) "  �w��� %d/%d ���ϥΪ�(�t�ήe�q�L�W��)  "
	   ANSI_COLOR(31;47) "  (�Ы����N���~��)  " ANSI_RESET, ctx->usercounter, ctx->totalusers);
    vkey();
    return 0;
}

/* vim:sw=4
 */
