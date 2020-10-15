#include "bbs.h"

// UNREGONLY �אּ�� BASIC �ӧP�_�O�_�� guest.

#define CheckMenuPerm(x) \
    ( (x == MENU_UNREGONLY)? \
      ((!HasUserPerm(PERM_BASIC) || HasUserPerm(PERM_LOGINOK))?0:1) :\
	((!x) ? 1 :  \
         ((x & PERM_LOGINOK) ? HasBasicUserPerm(x) : HasUserPerm(x))))

/* help & menu processring */
static int      refscreen = NA;
extern char    *boardprefix;
extern struct utmpfile_t *utmpshm;

static const char *title_tail_msgs[] = {
    "�ݪO",
    "�t�C",
    "��K",
};
static const char *title_tail_attrs[] = {
    ANSI_COLOR(37),
    ANSI_COLOR(32),
    ANSI_COLOR(36),
};
enum {
    TITLE_TAIL_BOARD = 0,
    TITLE_TAIL_SELECT,
    TITLE_TAIL_DIGEST,
};

// �ѩ���v�]���A�o�̷|�X�{�T�ؽs��:
// MODE (�w�q�� modes.h)    �O BBS ��U�إ\��b utmp ���s�� (var.c �n�[�r��)
// Menu Index (M_*)	    �O menu.c ����������n�������� mode �� index
// AdBanner Index	    �O�ʺA�ݪ��n��ܤ��򪺭�
// �q�e�o�O�Ψ�� mode map ���ഫ�� (�O�H�ݱo���Y����)
// ����� Menu Index �� AdBanner Index �X�@�A�Ш��U��������
///////////////////////////////////////////////////////////////////////
// AdBanner (SHM->notes) �e�X���O Note �O��ذϡu<�t��> �ʺA�ݪO�v(SYS)
// �ؿ��U���峹�A�ҥH�s�� Menu (M_*) �ɭn�Ө䶶�ǡG
// ��ذϽs��     => Menu Index => MODE
// (AdBannerIndex)
// ====================================
// 00�����e��     =>  M_GOODBYE
// 01�D���       =>  M_MMENU   => MMENU
// 02�t�κ��@��   =>  M_ADMIN   => ADMIN
// 03�p�H�H���   =>  M_MAIL    => MAIL
// 04�𶢲�Ѱ�   =>  M_TMENU   => TMENU
// 05�ӤH�]�w��   =>  M_UMENU   => UMENU
// 06�t�Τu���   =>  M_XMENU   => XMENU
// 07�T�ֻP��   =>  M_PMENU   => PMENU
// 08��tt�j�M��   =>  M_SREG    => SREG
// 09��tt�q�c��   =>  M_PSALE   => PSALE
// 10��tt�C�ֳ�   =>  M_AMUSE   => AMUSE
// 11��tt�Ѱ|     =>  M_CHC     => CHC
// 12�S�O�W��     =>  M_NMENU   => NMENU
///////////////////////////////////////////////////////////////////////
// �ѩ� MODE �P menu �����ǲ{�b�w���@�P (�̦��i��O�@�P��)�A�ҥH������
// �ഫ�O�a menu_mode_map �ӳB�z�C
// �n�w�q�s Menu �ɡA�Цb M_MENU_MAX ���e�[�J�s�ȡA�æb menu_mode_map
// �[�J������ MODE �ȡC �t�~�A�b Notes �U�]�n�W�[������ AdBanner �Ϥ�
// �Y���Q�[�Ϥ��h�n�ק� N_SYSADBANNER
///////////////////////////////////////////////////////////////////////

enum {
    M_GOODBYE=0,
    M_MMENU,	 M_ADMIN, M_MAIL, M_TMENU,
    M_UMENU,     M_XMENU, M_PMENU,M_SREG,
    M_PSALE,	 M_AMUSE, M_CHC,  M_NMENU,

    M_MENU_MAX,			// �o�O menu (M_*) ���̤j��
    N_SYSADBANNER = M_MENU_MAX, // �w�q M_* ��h�֦������� ADBANNER
    M_MENU_REFRESH= -1,		// �t�ΥΤ��쪺 index �� (�i��ܨ䥦���ʻP�I�q)
};

static const int menu_mode_map[M_MENU_MAX] = {
    0,
    MMENU,	ADMIN,	MAIL,	TMENU,
    UMENU,	XMENU,	PMENU,	SREG,
    PSALE,	AMUSE,	CHC,	NMENU
};

typedef struct {
    int     (*cmdfunc)();
    int     level;
    char    *desc;                   /* hotkey/description */
} commands_t;

///////////////////////////////////////////////////////////////////////

void
showtitle(const char *title, const char *mid)
{
    /* we have to...
     * - display title in left, cannot truncate.
     * - display mid message, cannot truncate
     * - display tail (board info), if possible.
     */
    int llen, rlen, mlen, mpos = 0;
    int pos = 0;
    int tail_type;
    const char *mid_attr = ANSI_COLOR(33);
    int is_currboard_special = 0;
    char buf[64];


    /* prepare mid */
#ifdef DEBUG
    {
	sprintf(buf, "  current pid: %6d  ", getpid());
	mid = buf;
	mid_attr = ANSI_COLOR(41;5);
    }
#else
    if (ISNEWMAIL(currutmp)) {
	mid = "    �A���s�H��    ";
	mid_attr = ANSI_COLOR(41;5);
    } else if ( HasUserPerm(PERM_ACCTREG) ) {
	// TODO cache this value?
	int nreg = regform_estimate_queuesize();
	if(nreg > 100)
	{
	    nreg -= (nreg % 10);
	    sprintf(buf, "  �W�L %03d �g���f��  ", nreg);
	    mid_attr = ANSI_COLOR(41;5);
	    mid = buf;
	}
    }
#endif

    /* prepare tail */
    if (currmode & MODE_SELECT)
	tail_type = TITLE_TAIL_SELECT;
    else if (currmode & MODE_DIGEST)
	tail_type = TITLE_TAIL_DIGEST;
    else
	tail_type = TITLE_TAIL_BOARD;

    if(currbid > 0)
    {
	assert(0<=currbid-1 && currbid-1<MAX_BOARD);
	is_currboard_special = (
		(getbcache(currbid)->brdattr & BRD_HIDE) &&
		(getbcache(currbid)->brdattr & BRD_POSTMASK));
    }

    /* now, calculate real positioning info */
    llen = strlen(title);
    mlen = strlen(mid);
    mpos = (t_columns -1 - mlen)/2;

    /* first, print left. */
    clear();
    outs(TITLE_COLOR "�i");
    outs(title);
    outs("�j");
    pos = llen + 4;

    /* print mid */
    while(pos++ < mpos)
	outc(' ');
    outs(mid_attr);
    outs(mid);
    pos += mlen;
    outs(TITLE_COLOR);

    /* try to locate right */
    rlen = strlen(currboard) + 4 + 4;
    if(currboard[0] && pos+rlen < t_columns)
    {
	// print right stuff
	while(pos++ < t_columns-rlen)
	    outc(' ');
	outs(title_tail_attrs[tail_type]);
	outs(title_tail_msgs[tail_type]);
	outs("�m");

	if (is_currboard_special)
	    outs(ANSI_COLOR(32));
	outs(currboard);
	outs(title_tail_attrs[tail_type]);
	outs("�n" ANSI_RESET "\n");
    } else {
	// just pad it.
	while(pos++ < t_columns)
	    outc(' ');
	outs(ANSI_RESET "\n");
    }

}

int TopBoards(void);

/* Ctrl-Z Anywhere Fast Switch, not ZG. */
static char zacmd = 0;

// ZA is waiting, hurry to the meeting stone!
int
ZA_Waiting(void)
{
    return (zacmd != 0);
}

void
ZA_Drop(void)
{
    zacmd = 0;
}

// Promp user our ZA bar and return for selection.
int
ZA_Select(void)
{
    int k;

    if (!is_login_ready ||
        !HasUserPerm(PERM_BASIC) ||
        HasUserPerm(PERM_VIOLATELAW))
        return 0;

    // TODO refresh status bar?
    vs_footer(VCLR_ZA_CAPTION " ���ֳt����: ",
	    " (b)�峹�C�� (c)���� (t)���� (f)�ڪ��̷R (m)�H�c (u)�ϥΪ̦W��");
    k = vkey();

    if (k < ' ' || k >= 'z') return 0;
    k = tolower(k);

    if(strchr("bcfmut", k) == NULL)
	return 0;

    zacmd = k;
    return 1;
}

// The ZA processor, only invoked in menu.
void
ZA_Enter(void)
{
    char cmd = zacmd;
    while (zacmd)
    {
	cmd = zacmd;
	zacmd = 0;

	// All ZA applets must check ZA_Waiting() at every stack of event loop.
	switch(cmd) {
	    case 'b':
		Read();
		break;
	    case 'c':
		Class();
		break;
	    case 't':
		TopBoards();
		break;
	    case 'f':
		Favorite();
		break;
	    case 'm':
		m_read();
		break;
	    case 'u':
		t_users();
		break;
	}
	// if user exit with new ZA assignment,
	// direct enter in next loop.
    }
}

/* �ʵe�B�z */
#define FILMROW 11
static unsigned short menu_row = 12;
static unsigned short menu_column = 20;

#ifdef EXP_ALERT_ADBANNER_USONG
static int
decide_menu_row(const commands_t *p) {
    if ((p[0].level && !HasUserPerm(p[0].level)) &&
        HasUserFlag(UF_ADBANNER_USONG) &&
        HasUserFlag(UF_ADBANNER)) {
        return menu_row + 1;
    }

    return menu_row;
}
#else
# define decide_menu_row(x) (menu_row)
#endif

static void
show_status(void)
{
    int i;
    struct tm      ptime;
    char           *myweek = "�Ѥ@�G�T�|����";

    localtime4_r(&now, &ptime);
    i = ptime.tm_wday << 1;
    move(b_lines, 0);
    vbarf(ANSI_COLOR(34;46) "[%d/%d �P��%c%c %d:%02d]"
	  ANSI_COLOR(1;33;45) "%-14s"
	  ANSI_COLOR(30;47) " �u�W" ANSI_COLOR(31)
	  "%d" ANSI_COLOR(30) "�H, �ڬO" ANSI_COLOR(31) "%s"
	  ANSI_COLOR(30) "\t[�I�s��]" ANSI_COLOR(31) "%s ",
	  ptime.tm_mon + 1, ptime.tm_mday, myweek[i], myweek[i + 1],
	  ptime.tm_hour, ptime.tm_min, SHM->today_is,
	  SHM->UTMPnumber, cuser.userid,
	  str_pager_modes[currutmp->pager % PAGER_MODES]);
}

/*
 * current caller of adbanner:
 *   xyz.c:   adbanner_goodbye();   // logout
 *   menu.c:  adbanner(cmdmode);    // ...
 *   board.c: adbanner(0);	    // ����ܦb board.c �̦ۤv�B�z(���ӬO������)
 */

void
adbanner_goodbye()
{
    adbanner(M_GOODBYE);
}

void
adbanner(int menu_index)
{
    int i = menu_index;

    // don't show if stat in class or user wants to skip adbanners
    if (currstat == CLASS || !(HasUserFlag(UF_ADBANNER)))
	return;

    // also prevent SHM busy status
    if (SHM->Pbusystate || SHM->last_film <= 0)
	return;

    if (    i != M_MENU_REFRESH &&
	    i >= 0		&&
	    i <  N_SYSADBANNER  &&
	    i <= SHM->last_film)
    {
	// use system menu - i
    } else {
	// To display ADBANNERs in slide show mode.
	// Since menu is updated per hour, the total presentation time
	// should be less than one hour. 60*60/MAX_ADBANNER[500]=7 (seconds).
	// @ Note: 60 * 60 / MAX_ADBANNER =3600/MAX_ADBANNER = "how many seconds
	// can one ADBANNER to display" to slide through every banners in one hour.
	// @ now / (3600 / MAx_ADBANNER) means "get the index of which to show".
	// syncnow();

	const int slideshow_duration = 3600 / MAX_ADBANNER,
		  slideshow_index    = now  / slideshow_duration;

	// index range: 0 =>[system] => N_SYSADBANNER    => [user esong] =>
	//              last_usong   => [advertisements] => last_film
	int valid_usong_range = (SHM->last_usong > N_SYSADBANNER &&
				 SHM->last_usong < SHM->last_film);

	if (SHM->last_film > N_SYSADBANNER) {
	    if (HasUserFlag(UF_ADBANNER_USONG) || !valid_usong_range)
		i = N_SYSADBANNER +       slideshow_index % (SHM->last_film+1-N_SYSADBANNER);
	    else
		i = SHM->last_usong + 1 + slideshow_index % (SHM->last_film - SHM->last_usong);
	}
	else
	    i = 0; // SHM->last_film;
    }

    // make it safe!
    i %= MAX_ADBANNER;

    move(1, 0);
    clrtoln(1 + FILMROW);	/* �M���W���� */
#ifdef LARGETERM_CENTER_MENU
    out_lines(SHM->notes[i], 11, (t_columns - 80)/2);	/* �u�L11��N�n */
#else
    out_lines(SHM->notes[i], 11, 0);	/* �u�L11��N�n */
#endif
    outs(ANSI_RESET);
#ifdef DEBUG
    // XXX piaip test
    move(FILMROW, 0); prints(" [ %d ] ", i);
#endif
}

static int
show_menu(int menu_index, const commands_t * p)
{
    register int    n = 0;
    register char  *s;
    int row = menu_row;

    adbanner(menu_index);

    // seems not everyone likes the menu in center.
#ifdef LARGETERM_CENTER_MENU
    // update menu column [fixed const because most items are designed as fixed)
    menu_column = (t_columns-40)/2;
    row = 12 + (t_lines-24)/2;
#endif

#ifdef EXP_ALERT_ADBANNER_USONG
    if ((p[0].level && !HasUserPerm(p[0].level)) &&
        HasUserFlag(UF_ADBANNER_USONG) &&
        HasUserFlag(UF_ADBANNER)) {
        // we have one more extra line to display ADBANNER_USONG!
        int alert_column = menu_column;
        move(row, 0);
        vpad(t_columns-2, "�w");
        if (alert_column > 2)
            alert_column -= 2;
        alert_column -= alert_column % 2;
        move(row++, alert_column);
        outs(" �W�謰�ϥΪ̤߱��I���d���ϡA���N�����߳� ");
    }
    assert(row == decide_menu_row(p));
#endif

    move(row, 0);
    while ((s = p[n].desc)) {
	if (CheckMenuPerm(p[n].level)) {
            prints("%*s  (%s%c" ANSI_RESET ")%s\n",
                   menu_column, "",
                   (HasUserFlag(UF_MENU_LIGHTBAR) ? ANSI_COLOR(36) :
                    ANSI_COLOR(1;36)), s[0], s+1);
	}
	n++;
    }
    return n - 1;
}

static void
domenu(int menu_index, const char *cmdtitle, int cmd, const commands_t cmdtable[])
{
    int             lastcmdptr, cmdmode;
    int             n, pos, total, i;
    int             err;

    assert(0 <= menu_index && menu_index < M_MENU_MAX);
    cmdmode = menu_mode_map[menu_index];

    setutmpmode(cmdmode);
    showtitle(cmdtitle, BBSName);
    total = show_menu(menu_index, cmdtable);

    show_status();
    lastcmdptr = pos = 0;

    do {
	i = -1;
	switch (cmd) {
	case Ctrl('Z'):
	    ZA_Select(); // we'll have za loop later.
	    refscreen = YEA;
	    i = lastcmdptr;
	    break;
	case Ctrl('N'):
	    New();
	    refscreen = YEA;
	    i = lastcmdptr;
	    break;
	case Ctrl('A'):
	    if (mail_man() == FULLUPDATE)
		refscreen = YEA;
	    i = lastcmdptr;
	    break;
	case KEY_DOWN:
	    i = lastcmdptr;
	case KEY_HOME:
	case KEY_PGUP:
	    do {
		if (++i > total)
		    i = 0;
	    } while (!CheckMenuPerm(cmdtable[i].level));
	    break;
	case KEY_UP:
	    i = lastcmdptr;
	case KEY_END:
	case KEY_PGDN:
	    do {
		if (--i < 0)
		    i = total;
	    } while (!CheckMenuPerm(cmdtable[i].level));
	    break;
	case KEY_LEFT:
	case 'e':
	case 'E':
	    if (cmdmode == MMENU)
		cmd = 'G';	    // to exit
	    else if ((cmdmode == MAIL) && chkmailbox())
		cmd = 'R';	    // force keep reading mail
	    else
		return;
	default:
	    if ((cmd == 's' || cmd == 'r') &&
		(cmdmode == MMENU || cmdmode == TMENU || cmdmode == XMENU)) {
		if (cmd == 's')
		    ReadSelect();
		else
		    Read();
		refscreen = YEA;
		i = lastcmdptr;
		currstat = cmdmode;
		break;
	    }
	    if (cmd == KEY_ENTER || cmd == KEY_RIGHT) {
		move(b_lines, 0);
		clrtoeol();

		currstat = XMODE;

		if ((err = (*cmdtable[lastcmdptr].cmdfunc) ()) == QUIT)
		    return;
		currutmp->mode = currstat = cmdmode;

		if (err == XEASY) {
		    refresh();
		    safe_sleep(1);
		} else if (err != XEASY + 1 || err == FULLUPDATE)
		    refscreen = YEA;

                // keep current position
		i = lastcmdptr;
                break;
	    }

	    if (cmd >= 'a' && cmd <= 'z')
		cmd = toupper(cmd);
	    while (++i <= total && cmdtable[i].desc)
		if (cmdtable[i].desc[0] == cmd)
		    break;

	    if (!CheckMenuPerm(cmdtable[i].level)) {
		for (i = 0; cmdtable[i].cmdfunc; i++)
		    if (CheckMenuPerm(cmdtable[i].level))
			break;
		if (!cmdtable[i].cmdfunc)
		    return;
	    }

	    if (cmd == 'H' && i > total){
		/* TODO: Add menu help */
	    }
	}

	// end of all commands
	if (ZA_Waiting())
	{
	    ZA_Enter();
	    refscreen = 1;
	    currstat = cmdmode;
	}

	if (i > total || !CheckMenuPerm(cmdtable[i].level))
	    continue;

	if (refscreen) {
	    showtitle(cmdtitle, BBSName);
	    // menu �]�w M_MENU_REFRESH �i�� ADBanner ��ܧO����T
	    show_menu(M_MENU_REFRESH, cmdtable);
	    show_status();
	    refscreen = NA;
	}
	cursor_clear(decide_menu_row(cmdtable) + pos, menu_column);
	n = pos = -1;
	while (++n <= (lastcmdptr = i))
	    if (CheckMenuPerm(cmdtable[n].level))
		pos++;

        // If we want to replace cursor_show by cursor_key, it must be inside
        // while(expr) othrewise calling 'continue' inside for-loop won't wait
        // for key.
	cursor_show(decide_menu_row(cmdtable) + pos, menu_column);
    } while (((cmd = vkey()) != EOF) || refscreen);

    abort_bbs(0);
}
/* INDENT OFF */

static int
view_user_money_log() {
    char userid[IDLEN+1];
    char fpath[PATHLEN];

    vs_hdr("�˵��ϥΪ̥���O��");
    usercomplete("�п�J�n�˵���ID: ", userid);
    if (!is_validuserid(userid))
        return 0;
    sethomefile(fpath, userid, FN_RECENTPAY);
    if (more(fpath, YEA) < 0)
        vmsgf("�ϥΪ� %s �L�̪����O��", userid);
    return 0;
}

static int
view_user_login_log() {
    char userid[IDLEN+1];
    char fpath[PATHLEN];

    vs_hdr("�˵��ϥΪ̳̪�W�u�O��");
    usercomplete("�п�J�n�˵���ID: ", userid);
    if (!is_validuserid(userid))
        return 0;
    sethomefile(fpath, userid, FN_RECENTLOGIN);
    if (more(fpath, YEA) < 0)
        vmsgf("�ϥΪ� %s �L�̪�W�u�O��", userid);
    return 0;
}

static int x_admin_money(void);
static int x_admin_user(void);

static int deprecate_userlist() {
    vs_hdr2(" " BBSNAME " ", " �w���ܨϥΪ̦W��");
    outs("\n"
         "���\\��w���ܨϥΪ̦W��ϡC\n"
         "�ЦܨϥΪ̦W�� (Ctrl-U) �ë��U��������C\n"
         "(�b�ϥΪ̦W��� h �|�����㻡��)\n\n"
         "�����I�s��:     Ctrl-U p\n"
         "�����N:         Ctrl-U C\n"
         "��ܤW�X�����T: Ctrl-U l\n");
    pressanykey();
    return 0;
}

// ----------------------------------------------------------- MENU DEFINITION
// �`�N�C�� menu �̦h����P����ܶW�L 11 �� (80x24 �зǤj�p������)

static const commands_t m_admin_money[] = {
    {view_user_money_log, PERM_SYSOP|PERM_ACCOUNTS,
                                                "View Log      �˵�����O��"},
    {give_money, PERM_SYSOP|PERM_VIEWSYSOP,	"Givemoney     ���]��"},
    {NULL, 0, NULL}
};

static const commands_t m_admin_user[] = {
    {view_user_money_log, PERM_SYSOP|PERM_ACCOUNTS,
                                        "Money Log      �̪����O��"},
    {view_user_login_log, PERM_SYSOP|PERM_ACCOUNTS|PERM_BOARD,
                                        "OLogin Log     �̪�W�u�O��"},
    {u_list, PERM_SYSOP,		"Users List     �C�X���U�W��"},
    {search_user_bybakpwd, PERM_SYSOP|PERM_ACCOUNTS,
                                        "DOld User data �d�\\�ƥ��ϥΪ̸��"},
    {NULL, 0, NULL}
};

/* administrator's maintain menu */
static const commands_t adminlist[] = {
    {m_user, PERM_SYSOP,		"User          �ϥΪ̸��"},
    {m_board, PERM_BOARD,		"Board         �]�w�ݪO"},
    {m_register,
	PERM_ACCOUNTS|PERM_ACCTREG,	"Register      �f�ֵ��U���"},
    {x_file, PERM_SYSOP|PERM_VIEWSYSOP,	"Xfile         �s��t���ɮ�"},
    {x_admin_money, PERM_SYSOP|PERM_ACCOUNTS|PERM_VIEWSYSOP,
                                        "Money         �i" MONEYNAME "�����j"},
    {x_admin_user, PERM_SYSOP|PERM_ACCOUNTS|PERM_BOARD|PERM_POLICE_MAN,
                                        "LUser Log     �i�ϥΪ̸�ưO���j"},
    {search_user_bypwd,
	PERM_ACCOUNTS|PERM_POLICE_MAN,	"Search User    �S��j�M�ϥΪ�"},
#ifdef USE_VERIFYDB
    {verifydb_admin_search_display,
	PERM_ACCOUNTS,			"Verify Search  �j�M�{�Ҹ�Ʈw"},
#endif
    {m_loginmsg, PERM_SYSOP,		"GMessage Login �i�����y"},
    {NULL, 0, NULL}
};

/* mail menu */
static const commands_t maillist[] = {
    {m_read, PERM_READMAIL,     "Read          �ڪ��H�c"},
    {m_send, PERM_LOGINOK,      "Send          �����H�H"},
    {mail_list, PERM_LOGINOK,   "Mail List     �s�ձH�H"},
    {setforward, PERM_LOGINOK,  "Forward       �]�w�H�c�۰���H" },
    {mail_mbox, PERM_INTERNET,  "Zip UserHome  ��Ҧ��p�H��ƥ��]�^�h"},
    {built_mail_index,
	PERM_LOGINOK,		"Savemail      ���ثH�c����"},
    {mail_all, PERM_SYSOP,      "All           �H�H���Ҧ��ϥΪ�"},
#ifdef USE_MAIL_ACCOUNT_SYSOP
    {mail_account_sysop, 0,     "Contact AM    �H�H���b������"},
#endif
    {NULL, 0, NULL}
};

#ifdef PLAY_ANGEL
static const commands_t angelmenu[] = {
    {a_angelmsg, PERM_ANGEL,"Leave message �d�����p�D�H"},
    {a_angelmsg2,PERM_ANGEL,"Call screen   �I�s�e���өʯd��"},
    {angel_check_master,PERM_ANGEL,
                            "Master check  �d�ߤp�D�H���A"},
    // Cannot use R because r is reserved for Read/Mail due to TMENU.
    {a_angelreport, 0,      "PReport       �u�W�ѨϪ��A���i"},
    {NULL, 0, NULL}
};

static int menu_angelbeats() {
    domenu(M_TMENU, "Angel Beats! �ѨϤ��|", 'L', angelmenu);
    return 0;
}
#endif

/* Talk menu */
static const commands_t talklist[] = {
    {t_users, 0,            "Users         �u�W�ϥΪ̦C��"},
    {t_query, 0,            "Query         �d�ߺ���"},
    // PERM_PAGE - ���y���n PERM_LOGIN �F
    // �S�D�z�i�H talk ������y�C
    {t_talk, PERM_LOGINOK,  "Talk          ��H���"},
    // PERM_CHAT �D login �]���A�|���H�Φ��n�O�H�C
    {t_chat, PERM_LOGINOK,  "Chat          �i" BBSMNAME2 "�h�H��ѫǡj"},
    {deprecate_userlist, 0, "Pager         �����I�s��"},
    {t_qchicken, 0,         "Watch Pet     �d���d��"},
#ifdef PLAY_ANGEL
    {a_changeangel,
	PERM_LOGINOK,	    "AChange Angel �󴫤p�Ѩ�"},
    {menu_angelbeats, PERM_ANGEL|PERM_SYSOP,
                            "BAngel Beats! �ѨϤ��|"},
#endif
    {t_display, 0,          "Display       ��ܤW�X�����T"},
    {NULL, 0, NULL}
};

/* name menu */
static int t_aloha() {
    friend_edit(FRIEND_ALOHA);
    return 0;
}

static int t_special() {
    friend_edit(FRIEND_SPECIAL);
    return 0;
}

static const commands_t namelist[] = {
    {t_override, PERM_LOGINOK,"OverRide      �n�ͦW��"},
    {t_reject, PERM_LOGINOK,  "Black         �a�H�W��"},
    {t_aloha,PERM_LOGINOK,    "ALOHA         �W���q���W��"},
    {t_fix_aloha,PERM_LOGINOK,"XFixALOHA     �ץ��W���q��"},
    {t_special,PERM_LOGINOK,  "Special       ��L�S�O�W��"},
    {NULL, 0, NULL}
};

static int u_view_recentlogin()
{
    char fn[PATHLEN];
    setuserfile(fn, FN_RECENTLOGIN);
    return more(fn, YEA);
}

#ifdef USE_RECENTPAY
static int u_view_recentpay()
{
    char fn[PATHLEN];
    clear();
    mvouts(10, 5, "�`�N: ���B���e�ȨѰѦҡA���" MONEYNAME
                        "���ʥH���褺����Ƭ���");
    pressanykey();
    setuserfile(fn, FN_RECENTPAY);
    return more(fn, YEA);
}
#endif

static const commands_t myfilelist[] = {
    {u_editplan,    PERM_LOGINOK,   "QueryEdit     �s��W����"},
    {u_editsig,	    PERM_LOGINOK,   "Signature     �s��ñ�W��"},
    {NULL, 0, NULL}
};

static const commands_t myuserlog[] = {
    {u_view_recentlogin, 0,   "LRecent Login  �̪�W���O��"},
#ifdef USE_RECENTPAY
    {u_view_recentpay,   0,   "PRecent Pay    �̪����O��"},
#endif
    {NULL, 0, NULL}
};

static int
u_myfiles()
{
    domenu(M_UMENU, "�ӤH�ɮ�", 'Q', myfilelist);
    return 0;
}

static int
u_mylogs()
{
    domenu(M_UMENU, "�ӤH�O��", 'L', myuserlog);
    return 0;
}

void Customize(); // user.c

static int
u_customize()
{
    Customize();
    return 0;
}


/* User menu */
static const commands_t userlist[] = {
    {u_customize,   PERM_BASIC,	    "UCustomize    �ӤH�Ƴ]�w"},
    {u_info,	    PERM_BASIC,     "Info          �]�w�ӤH��ƻP�K�X"},
    {u_loginview,   PERM_BASIC,     "VLogin View   ��ܶi���e��"},
    {u_myfiles,	    PERM_LOGINOK,   "My Files      �i�ӤH�ɮסj (�W��,ñ�W��...)"},
    {u_mylogs,	    PERM_LOGINOK,   "LMy Logs      �i�ӤH�O���j (�̪�W�u...)"},
    {u_register,    MENU_UNREGONLY, "Register      ��g�m���U�ӽг�n"},
#ifdef ASSESS
    {u_cancelbadpost,PERM_LOGINOK,  "Bye BadPost   �ӽЧR���h��"},
#endif // ASSESS
    {deprecate_userlist,       0,   "KCloak        �����N"},
    {NULL, 0, NULL}
};

#ifdef HAVE_USERAGREEMENT
static int
x_agreement(void)
{
    more(HAVE_USERAGREEMENT, YEA);
    return 0;
}
#endif

static int
x_admin_money(void)
{
    char init = 'V';
    if (HasUserPerm(PERM_VIEWSYSOP))
        init = 'G';
    domenu(M_XMENU, "���������޲z", init, m_admin_money);
    return 0;
}

static int
x_admin_user(void)
{
    domenu(M_XMENU, "�ϥΪ̰O���޲z", 'O', m_admin_user);
    return 0;
}

#ifdef HAVE_INFO
static int
x_program(void)
{
    more("etc/version", YEA);
    return 0;
}
#endif

#ifdef HAVE_LICENSE
static int
x_gpl(void)
{
    more("etc/GPL", YEA);
    return 0;
}
#endif

#ifdef HAVE_SYSUPDATES
static int
x_sys_updates(void)
{
    more("etc/sysupdates", YEA);
    return 0;
}
#endif

#ifdef DEBUG
int _debug_reportstruct()
{
    clear();
    prints("boardheader_t:\t%d\n", sizeof(boardheader_t));
    prints("fileheader_t:\t%d\n", sizeof(fileheader_t));
    prints("userinfo_t:\t%d\n", sizeof(userinfo_t));
    prints("screenline_t:\t%d\n", sizeof(screenline_t));
    prints("SHM_t:\t%d\n", sizeof(SHM_t));
    prints("userec_t:\t%d\n", sizeof(userec_t));
    pressanykey();
    return 0;
}

#endif

/* XYZ tool sub menu */
static const commands_t m_xyz_hot[] = {
    {x_week, 0,      "Week          �m���g���Q�j�������D�n"},
    {x_issue, 0,     "Issue         �m����Q�j�������D�n"},
    {x_boardman,0,   "Man Boards    �m�ݪO��ذϱƦ�]�n"},
    {NULL, 0, NULL}
};

/* XYZ tool sub menu */
static const commands_t m_xyz_user[] = {
    {x_user100 ,0,   "Users         �m�ϥΪ̦ʤj�Ʀ�]�n"},
    {topsong,PERM_LOGINOK,
	             "GTop Songs    �m�ϥΪ̤߱��I���Ʀ�n"},
    {x_today, 0,     "Today         �m����W�u�H���έp�n"},
    {x_yesterday, 0, "Yesterday     �m�Q��W�u�H���έp�n"},
    {NULL, 0, NULL}
};

static int
x_hot(void)
{
    domenu(M_XMENU, "�������D�P�ݪO", 'W', m_xyz_hot);
    return 0;
}

static int
x_users(void)
{
    domenu(M_XMENU, "�ϥΪ̲έp��T", 'U', m_xyz_user);
    return 0;
}

/* XYZ tool menu */
static const commands_t xyzlist[] = {
    {x_hot,  0,      "THot Topics   �m�������D�P�ݪO�n"},
    {x_users,0,      "Users         �m�ϥΪ̬����έp�n"},
#ifndef DEBUG
    /* All these are useless in debug mode. */
#ifdef HAVE_USERAGREEMENT
    {x_agreement,0,  "Agreement     �m�����ϥΪ̱��ڡn"},
#endif
#ifdef  HAVE_LICENSE
    {x_gpl, 0,       "ILicense       GNU �ϥΰ���"},
#endif
#ifdef HAVE_INFO
    {x_program, 0,   "Program       ���{���������P���v�ŧi"},
#endif
    {x_history, 0,   "History       �m�ڭ̪������n"},
    {x_login,0,      "System        �m�t�έ��n���i�n"},
#ifdef HAVE_SYSUPDATES
    {x_sys_updates,0,"LUpdates      �m�����t�ε{����s�����n"},
#endif

#else // !DEBUG
    {_debug_reportstruct, 0,
	    	     "ReportStruct  ���i�U�ص��c���j�p"},
#endif // !DEBUG

    {p_sysinfo, 0,   "Xinfo         �m�d�ݨt�θ�T�n"},
    {NULL, 0, NULL}
};

/* Ptt money menu */
static const commands_t moneylist[] = {
    {p_give, 0,         "0Give        ����L�H" MONEYNAME},
    {save_violatelaw, 0,"1ViolateLaw  ú�@��"},
    {p_from, 0,         "2From        �Ȯɭק�G�m"},
    {ordersong,0,       "3OSong       �߱��I����"},
    {NULL, 0, NULL}
};

static const commands_t      cmdlist[] = {
    {admin, PERM_SYSOP|PERM_ACCOUNTS|PERM_BOARD|PERM_VIEWSYSOP|PERM_ACCTREG|PERM_POLICE_MAN,
				"0Admin       �i �t�κ��@�� �j"},
    {Announce,	0,		"Announce     �i ��ؤ��G�� �j"},
#ifdef DEBUG
    {Favorite,	0,		"Favorite     �i �ڪ��̤��R �j"},
#else
    {Favorite,	0,		"Favorite     �i �� �� �̷R �j"},
#endif
    {Class,	0,		"Class        �i ���հQ�װ� �j"},
    // TODO �ثe�ܦh�H�Q���v�ɷ|�ܦ� -R-1-3 (PERM_LOGINOK, PERM_VIOLATELAW,
    // PERM_NOREGCODE) �S�� PERM_READMAIL�A���o�˳·Ъ��O�L�̴N�d�����o�ͤ����
    {Mail, 	PERM_BASIC,     "Mail         �i �p�H�H��� �j"},
    // ���� bot ���w��� query online accounts, �ҥH��ѧאּ LOGINOK
    {Talk, 	PERM_LOGINOK,	"Talk         �i �𶢲�Ѱ� �j"},
    {User, 	PERM_BASIC,	"User         �i �ӤH�]�w�� �j"},
    {Xyz, 	0,		"Xyz          �i �t�θ�T�� �j"},
    {Play_Play, PERM_LOGINOK, 	"Play         �i �T�ֻP�� �j"},
    {Name_Menu, PERM_LOGINOK,	"Namelist     �i �s�S�O�W�� �j"},
#ifdef DEBUG
    {Goodbye, 	0, 		"Goodbye      �A���A���A���A��"},
#else
    {Goodbye, 	0, 		"Goodbye         ���}�A�A���K "},
#endif
    {NULL, 	0, 		NULL}
};

int main_menu(void) {
    domenu(M_MMENU, "�D�\\���", (ISNEWMAIL(currutmp) ? 'M' : 'C'), cmdlist);
    return 0;
}

static int p_money() {
    domenu(M_PSALE, BBSMNAME2 "�q�c��", '0', moneylist);
    return 0;
};

static int chessroom();

/* Ptt Play menu */
static const commands_t playlist[] = {
    {p_money, PERM_LOGINOK,  "Pay         �i " BBSMNAME2 "�q�c�� �j"},
    {chicken_main, PERM_LOGINOK,
			     "Chicken     �i " BBSMNAME2 "�i���� �j"},
    {ticket_main, PERM_LOGINOK,
                             "Gamble      �i " BBSMNAME2 "�m��   �j"},
    {chessroom, PERM_LOGINOK,"BChess      �i " BBSMNAME2 "�Ѱ|   �j"},
    {NULL, 0, NULL}
};

static const commands_t conn6list[] = {
    {conn6_main,       PERM_LOGINOK, "1Conn6Fight    �i" ANSI_COLOR(1;33) "���l���ܧ�" ANSI_RESET "�j"},
    {conn6_personal,   PERM_LOGINOK, "2Conn6Self     �i" ANSI_COLOR(1;34) "���l�ѥ���" ANSI_RESET "�j"},
    {conn6_watch,      PERM_LOGINOK, "3Conn6Watch    �i" ANSI_COLOR(1;35) "���l���[��" ANSI_RESET "�j"},
    {NULL, 0, NULL}
};

static int conn6_menu() {
    domenu(M_CHC, BBSMNAME2 "���l��", '1', conn6list);
    return 0;
}

static const commands_t chesslist[] = {
    {chc_main,         PERM_LOGINOK, "1CChessFight   �i" ANSI_COLOR(1;33) " �H���ܧ� " ANSI_RESET "�j"},
    {chc_personal,     PERM_LOGINOK, "2CChessSelf    �i" ANSI_COLOR(1;34) " �H�ѥ��� " ANSI_RESET "�j"},
    {chc_watch,        PERM_LOGINOK, "3CChessWatch   �i" ANSI_COLOR(1;35) " �H���[�� " ANSI_RESET "�j"},
    {gomoku_main,      PERM_LOGINOK, "4GomokuFight   �i" ANSI_COLOR(1;33) "���l���ܧ�" ANSI_RESET "�j"},
    {gomoku_personal,  PERM_LOGINOK, "5GomokuSelf    �i" ANSI_COLOR(1;34) "���l�ѥ���" ANSI_RESET "�j"},
    {gomoku_watch,     PERM_LOGINOK, "6GomokuWatch   �i" ANSI_COLOR(1;35) "���l���[��" ANSI_RESET "�j"},
    {gochess_main,     PERM_LOGINOK, "7GoChessFight  �i" ANSI_COLOR(1;33) " ����ܧ� " ANSI_RESET "�j"},
    {gochess_personal, PERM_LOGINOK, "8GoChessSelf   �i" ANSI_COLOR(1;34) " ��ѥ��� " ANSI_RESET "�j"},
    {gochess_watch,    PERM_LOGINOK, "9GoChessWatch  �i" ANSI_COLOR(1;35) " ����[�� " ANSI_RESET "�j"},
    {conn6_menu,       PERM_LOGINOK, "CConnect6      �i" ANSI_COLOR(1;33) "  ���l��  " ANSI_RESET "�j"},
    {NULL, 0, NULL}
};

static int chessroom() {
    domenu(M_CHC, BBSMNAME2 "�Ѱ|", '1', chesslist);
    return 0;
}

// ---------------------------------------------------------------- SUB MENUS

/* main menu */

int
admin(void)
{
    char init = 'L';

    if (HasUserPerm(PERM_VIEWSYSOP))
        init = 'X';
    else if (HasUserPerm(PERM_ACCTREG))
        init = 'R';
    else if (HasUserPerm(PERM_POLICE_MAN))
        init = 'S';

    domenu(M_ADMIN, "�t�κ��@", init, adminlist);
    return 0;
}

int
Mail(void)
{
    domenu(M_MAIL, "�q�l�l��", 'R', maillist);
    return 0;
}

int
Talk(void)
{
    domenu(M_TMENU, "��ѻ���", 'U', talklist);
    return 0;
}

int
User(void)
{
    domenu(M_UMENU, "�ӤH�]�w", 'U', userlist);
    return 0;
}

int
Xyz(void)
{
    domenu(M_XMENU, "�u��{��", 'T', xyzlist);
    return 0;
}

int
Play_Play(void)
{
    domenu(M_PMENU, "�����C�ֳ�", 'G', playlist);
    return 0;
}

int
Name_Menu(void)
{
    domenu(M_NMENU, "�W��s��", 'O', namelist);
    return 0;
}

