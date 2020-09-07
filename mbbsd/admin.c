#include "bbs.h"

/* �i�����y�Ŷ� */
int
m_loginmsg(void)
{
  char msg[100];
  move(21,0);
  clrtobot();
  if(SHM->loginmsg.pid && SHM->loginmsg.pid != currutmp->pid)
    {
      outs("�ثe�w�g���H�U�� �i�����y�]�w�Х���զn�A�]�w..");
      getmessage(SHM->loginmsg);
    }
  getdata(22, 0,
     "�i�����y:��������,���z�Z�ϥΪ̬���,�]�w�������۰ʨ���,�T�w�n�]?(y/N)",
          msg, 3, LCECHO);

  if(msg[0]=='y' &&

     getdata_str(23, 0, "�]�w�i�����y:", msg, 56, DOECHO, SHM->loginmsg.last_call_in))
    {
          SHM->loginmsg.pid=currutmp->pid; /*�������h �N����race condition */
          strlcpy(SHM->loginmsg.last_call_in, msg, sizeof(SHM->loginmsg.last_call_in));
          strlcpy(SHM->loginmsg.userid, cuser.userid, sizeof(SHM->loginmsg.userid));
    }
  return 0;
}

/* �ϥΪ̺޲z */
int
m_user(void)
{
    userec_t        xuser;
    int             id;
    char            genbuf[200];

    vs_hdr("�ϥΪ̳]�w");
    usercomplete(msg_uid, genbuf);
    if (*genbuf) {
	move(2, 0);
	if ((id = getuser(genbuf, &xuser))) {
	    user_display(&xuser, 1);
	    if( HasUserPerm(PERM_ACCOUNTS) )
		uinfo_query(xuser.userid, 1, id);
	    else
		pressanykey();
	} else {
	    outs(err_uid);
	    clrtoeol();
	    pressanykey();
	}
    }
    return 0;
}

static int retrieve_backup(userec_t *user)
{
    int     uid;
    char    src[PATHLEN], dst[PATHLEN];
    char    ans;

    if ((uid = searchuser(user->userid, user->userid))) {
	userec_t orig;
	passwd_sync_query(uid, &orig);
	strlcpy(user->passwd, orig.passwd, sizeof(orig.passwd));
	setumoney(uid, user->money);
	passwd_sync_update(uid, user);
	return 0;
    }

    ans = vans("�ثe�� PASSWD �ɨS���� ID�A�s�W�ܡH[y/N]");

    if (ans != 'y') {
	vmsg("�ثe�� PASSWDS �ɨS���� ID�A�Х��s�W���b��");
	return -1;
    }

    if (setupnewuser((const userec_t *)user) >= 0) {
	sethomepath(dst, user->userid);
	if (!dashd(dst)) {
	    snprintf(src, sizeof(src), "tmp/%s", user->userid);
	    if (!dashd(src) || !Rename(src, dst))
		mkuserdir(user->userid);
	}
	return 0;
    }
    return -1;
}

int
upgrade_passwd(userec_t *puser)
{
    if (puser->version == PASSWD_VERSION)
	return 1;
    if (!puser->userid[0])
	return 1;
    // unknown version
    return 0;

#if 0
    // this is a sample.
    if (puser->version == 2275) // chicken change
    {
	memset(puser->career,  0, sizeof(puser->career));
	memset(puser->chkpad0, 0, sizeof(puser->chkpad0));
	memset(puser->chkpad1, 0, sizeof(puser->chkpad1));
	memset(puser->chkpad2, 0, sizeof(puser->chkpad2));
	puser->lastseen= 0;
	puser->version = PASSWD_VERSION;
	return ;
    }
#endif
}

struct userec_filter_t;
typedef struct userec_filter_t userec_filter_t;
typedef const char *(*userec_filter_func)(userec_filter_t *, const userec_t *);

typedef struct {
    int field;
    char key[22];
} userec_keyword_t;

typedef struct {
    uint32_t userlevel_mask;
    uint32_t userlevel_wants;
    uint32_t role_mask;
    uint32_t role_wants;
} userec_perm_t;

enum {
    USEREC_REGTIME_Y,
    USEREC_REGTIME_YM,
    USEREC_REGTIME_YMD,
    USEREC_REGTIME_YMD_H,
    USEREC_REGTIME_YMD_HM,
    USEREC_REGTIME_YMD_HMS,
};

static const char * const userec_regtime_desc[] = {
    "�d�ߵ��U�ɶ� (�~��)",
    "�d�ߵ��U�ɶ� (�~���)",
    "�d�ߵ��U�ɶ� (�~���)",
    "�d�ߵ��U�ɶ� (�~��� ��)",
    "�d�ߵ��U�ɶ� (�~��� �ɤ�)",
    "�d�ߵ��U�ɶ� (�~���� �ɤ���)",
};

typedef struct {
    struct tm regtime;
    // See USEREC_REGTIME_* enum above.
    uint8_t match_type;
} userec_regtime_t;

#define USEREC_FILTER_KEYWORD (1)
#define USEREC_FILTER_PERM (2)
#define USEREC_FILTER_REGTIME (3)

struct userec_filter_t {
    int type;
    const char *(*filter)(struct userec_filter_t *, const userec_t *);
    const char *(*desc)(struct userec_filter_t *);
    union {
	userec_keyword_t keyword;
	userec_perm_t perm;
	userec_regtime_t regtime;
    };
};

static const char *
userec_filter_keyword_filter(userec_filter_t *uf, const userec_t *user)
{
    assert(uf->type == USEREC_FILTER_KEYWORD);
    const char * const key = uf->keyword.key;
    const int keytype = uf->keyword.field;
    if ((!keytype || keytype == 1) &&
	DBCS_strcasestr(user->userid, key))
	return user->userid;
    else if ((!keytype || keytype == 2) &&
	     DBCS_strcasestr(user->realname, key))
	return user->realname;
    else if ((!keytype || keytype == 3) &&
	     DBCS_strcasestr(user->nickname, key))
	return user->nickname;
    else if ((!keytype || keytype == 4) &&
	     DBCS_strcasestr(user->address, key))
	return user->address;
    else if ((!keytype || keytype == 5) &&
	     strcasestr(user->email, key)) // not DBCS.
	return user->email;
    else if ((!keytype || keytype == 6) &&
	     strcasestr(user->lasthost, key)) // not DBCS.
	return user->lasthost;
    else if ((!keytype || keytype == 7) &&
	     DBCS_strcasestr(user->career, key))
	return user->career;
    else if ((!keytype || keytype == 8) &&
	     DBCS_strcasestr(user->justify, key))
	return user->justify;
    return NULL;
}

static const char *
userec_filter_keyword_desc(userec_filter_t *uf)
{
    return uf->keyword.key;
}

static const char *
userec_filter_perm_filter(userec_filter_t *uf, const userec_t *user)
{
    assert(uf->type == USEREC_FILTER_PERM);
    if ((user->userlevel & uf->perm.userlevel_mask) ==
	uf->perm.userlevel_wants &&
	(user->role & uf->perm.role_mask) == uf->perm.role_wants)
	return "�v���ŦX";
    return NULL;
}

static const char *
userec_filter_perm_desc(userec_filter_t *uf GCC_UNUSED)
{
    return "�d�߱b���v��";
}

static const char *
userec_filter_regtime_filter(userec_filter_t *uf, const userec_t *user)
{
    assert(uf->type == USEREC_FILTER_REGTIME);
    const struct tm *regtime = &uf->regtime.regtime;
    struct tm _regtime;

    localtime4_r(&user->firstlogin, &_regtime);

    // Check date and time based on what the user specified
    switch (uf->regtime.match_type) {
	case USEREC_REGTIME_YMD_HMS:
	    if (regtime->tm_sec != _regtime.tm_sec)
		return NULL;
	    // fall through
	case USEREC_REGTIME_YMD_HM:
	    if (regtime->tm_min != _regtime.tm_min)
		return NULL;
	    // fall through
	case USEREC_REGTIME_YMD_H:
	    if (regtime->tm_hour != _regtime.tm_hour)
		return NULL;
	    // fall through
	case USEREC_REGTIME_YMD:
	    if (regtime->tm_mday != _regtime.tm_mday)
		return NULL;
	    // fall through
	case USEREC_REGTIME_YM:
	    if (regtime->tm_mon != _regtime.tm_mon)
		return NULL;
	    // fall through
	case USEREC_REGTIME_Y:
	    if (regtime->tm_year != _regtime.tm_year)
		return NULL;
	    return "���U�ɶ��ŦX";
	default:
	    // Not reached
	    assert(0);
    }
}

static const char *
userec_filter_regtime_desc(userec_filter_t *uf)
{
    return userec_regtime_desc[uf->regtime.match_type];
}

static int
get_userec_filter(int row, userec_filter_t *uf)
{
    char tbuf[4];

    move(row, 0);
    outs("�j�M���: [0]���� 1.ID 2.�m�W 3.�ʺ� 4.�a�} 5.Mail 6.IP 7.¾�~ 8.�{��\n");
    outs("          [a]�����\\�{�ҽX���U [b]���U�ɶ�\n");
    row += 2;
    do {
	memset(uf, 0, sizeof(*uf));
	getdata(row++, 0, "�n�j�M���ظ�ơH", tbuf, 2, DOECHO);

	if (strlen(tbuf) > 1)
	    continue;

	char sel = tbuf[0];
	if (!sel)
	    sel = '0';
	if (sel >= '0' && sel <= '8') {
	    uf->type = USEREC_FILTER_KEYWORD;
	    uf->filter = userec_filter_keyword_filter;
	    uf->desc = userec_filter_keyword_desc;
	    uf->keyword.field = sel - '0';
	    getdata(row++, 0, "�п�J����r: ", uf->keyword.key,
		    sizeof(uf->keyword.key), DOECHO);
	    if (!uf->keyword.key[0])
		return -1;
	} else if (sel == 'a' || sel == 'A') {
	    uf->type = USEREC_FILTER_PERM;
	    uf->filter = userec_filter_perm_filter;
	    uf->desc = userec_filter_perm_desc;
	    uf->perm.userlevel_mask = uf->perm.userlevel_wants =
		PERM_NOREGCODE;
	} else if (sel == 'b' || sel == 'B') {
	    char buf[20], *p;

	    uf->type = USEREC_FILTER_REGTIME;
	    uf->filter = userec_filter_regtime_filter;
	    uf->desc = userec_filter_regtime_desc;

	    row += 3;
	    outs("���U�ɶ��j�M�̤֥]�t�~���A�̲ӥi���A�Ш̻ݨD��J�C\n");
	    outs("�~����п�J \"�~/��\"�F�ܤ��h��J \"�~/��/�� ��:��\"�C\n");
	    outs("���קK�~�P�A�j�M�Ӧܤp�ɮɽж� \"�~/��/�� ��:\"�C\n");
	    getdata(row++, 0, "�п�J���U�ɶ� (�p 2019/12/31 23:59:59): ",
		    buf, sizeof(buf), DOECHO);

	    // check the returned pointer to make sure the match uses the whole string
	    if ((p = strptime(buf, "%Y/%m/%d %H:%M:%S", &uf->regtime.regtime)) && !*p)
		uf->regtime.match_type = USEREC_REGTIME_YMD_HMS;
	    else if ((p = strptime(buf, "%Y/%m/%d %H:%M", &uf->regtime.regtime)) && !*p)
		uf->regtime.match_type = USEREC_REGTIME_YMD_HM;
	    else if ((p = strptime(buf, "%Y/%m/%d %H:", &uf->regtime.regtime)) && !*p)
		uf->regtime.match_type = USEREC_REGTIME_YMD_H;
	    else if ((p = strptime(buf, "%Y/%m/%d", &uf->regtime.regtime)) && !*p)
		uf->regtime.match_type = USEREC_REGTIME_YMD;
	    else if ((p = strptime(buf, "%Y/%m", &uf->regtime.regtime)) && !*p)
		uf->regtime.match_type = USEREC_REGTIME_YM;
	    else if ((p = strptime(buf, "%Y", &uf->regtime.regtime)) && !*p)
		uf->regtime.match_type = USEREC_REGTIME_Y;
	    else {
		outs("�ɶ��榡���~\n");
		return -1;
	    }
	}
    } while (!uf->type);
    return 0;
}

#define MAX_USEREC_FILTERS (5)

static int
search_key_user(const char *passwdfile, int mode)
{
    userec_t        user;
    int             ch;
    int             unum = 0;
    FILE            *fp1 = fopen(passwdfile, "r");
    char            friendfile[PATHLEN]="";
    const char	    *keymatch;
    int	            isCurrentPwd;
    userec_filter_t ufs[MAX_USEREC_FILTERS];
    size_t	    num_ufs = 0;

    assert(fp1);

    isCurrentPwd = (strcmp(passwdfile, FN_PASSWD) == 0);
    clear();
    if (!mode) {
	userec_filter_t *uf = &ufs[0];
	uf->type = 1;
	uf->filter = userec_filter_keyword_filter;
	uf->desc = userec_filter_keyword_desc;
	uf->keyword.field = 1;
	getdata(0, 0, "�п�Jid :", uf->keyword.key, sizeof(uf->keyword.key),
		DOECHO);
	num_ufs++;
    } else {
	// improved search
	vs_hdr("����r�j�M");
	if (!get_userec_filter(1, &ufs[0]))
	    num_ufs++;
    }

    if (!num_ufs) {
	fclose(fp1);
	return 0;
    }
    const char *desc = ufs[0].desc(&ufs[0]);
    vs_hdr(desc);

    // <= or < ? I'm not sure...
    while ((fread(&user, sizeof(user), 1, fp1)) > 0 && unum++ < MAX_USERS) {

	// skip empty records
	if (!user.userid[0])
	    continue;

	if (!(unum & 0xFF)) {
	    vs_hdr(desc);
	    prints("�� [%d] �����\n", unum);
	    refresh();
	}

	// XXX �o�̷|���¸�ơA�n�p�� PWD �� upgrade
	if (!upgrade_passwd(&user))
	    continue;

	for (size_t i = 0; i < num_ufs; i++) {
	    keymatch = ufs[i].filter(&ufs[i], &user);
	    if (!keymatch)
		break;
	}
        while (keymatch) {
	    vs_hdr(desc);
	    prints("�� [%d] �����\n", unum);
	    refresh();

	    user_display(&user, 1);
	    // user_display does not have linefeed in tail.

	    if (isCurrentPwd && HasUserPerm(PERM_ACCOUNTS))
		uinfo_query(user.userid, 1, unum);
	    else
		outs("\n");

	    // XXX don't trust 'user' variable after here
	    // because uinfo_query may have changed it.

	    vs_footer("  �j�M�b��  ", mode ?
		      "  (�ť���)�j�M�U�@�� (A)�[�J�W�� (F)�s�W���� (Q)���}" :
		      "  (�ť���)�j�M�U�@�� (A)�[�J�W�� (S)���γƥ���� "
		      "(Q)���}");

	    while ((ch = vkey()) == 0)
		;
	    if (ch == 'a' || ch == 'A') {
		if(!friendfile[0])
		{
		    friend_special();
		    setfriendfile(friendfile, FRIEND_SPECIAL);
		}
		friend_add(user.userid, FRIEND_SPECIAL, keymatch);
		break;
	    }
	    if (mode && (ch == 'f' || ch == 'F')) {
		if (num_ufs >= MAX_USEREC_FILTERS) {
		    vmsg("�j�M����ƶq�w�F�W���C");
		    continue;
		}
		clear();
		vs_hdr("�s�W����");
		if (get_userec_filter(1, &ufs[num_ufs])) {
		    vmsg("�s�W���󥢱�");
		    continue;
		}
		num_ufs++;
		desc = "�h������";
		break;
	    }
	    if (ch == ' ')
		break;
	    if (ch == 'q' || ch == 'Q') {
		fclose(fp1);
		return 0;
	    }
	    if (ch == 's' && !mode) {
		if (retrieve_backup(&user) >= 0) {
		    fclose(fp1);
		    vmsg("�w���\\���γƥ���ơC");
		    return 0;
		} else {
		    vmsg("���~: ���γƥ���ƥ��ѡC");
		}
	    }
	}
    }

    fclose(fp1);
    return 0;
}

/* �H���N key �M��ϥΪ� */
int
search_user_bypwd(void)
{
    search_key_user(FN_PASSWD, 1);
    return 0;
}

/* �M��ƥ����ϥΪ̸�� */
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
    outs("�п�J�A�n�ΨӴM��ƥ����ɮ� �Ϋ� 'q' ���}\n");
    outs(" [" ANSI_COLOR(1;31) "1" ANSI_RESET "]�@�ѫe,"
	 " [" ANSI_COLOR(1;31) "2" ANSI_RESET "]��ѫe,"
	 " [" ANSI_COLOR(1;31) "3" ANSI_RESET "]�T�ѫe\n");
    outs(" [" ANSI_COLOR(1;31) "4" ANSI_RESET "]�|�ѫe,"
	 " [" ANSI_COLOR(1;31) "5" ANSI_RESET "]���ѫe,"
	 " [" ANSI_COLOR(1;31) "6" ANSI_RESET "]���ѫe\n");
    outs(" [7]�ƥ���\n");
    do {
	move(5, 1);
	outs("��� => ");
	ch = vkey();
	if (ch == 'q' || ch == 'Q')
	    return 0;
    } while (ch < '1' || ch > '7');
    ch -= '1';
    if( access(choice[ch], R_OK) != 0 )
	vmsg("�ɮפ��s�b");
    else
	search_key_user(choice[ch], 0);
    return 0;
}

static void
bperm_msg(const boardheader_t * board)
{
    prints("\n�]�w [%s] �ݪO��(%s)�v���G", board->brdname,
	   board->brdattr & BRD_POSTMASK ? "�o��" : "�\\Ū");
}

unsigned int
setperms(unsigned int pbits, const char * const pstring[])
{
    register int    i;

    move(4, 0);
    for (i = 0; i < NUMPERMS / 2; i++) {
	prints("%c. %-20s %-15s %c. %-20s %s\n",
	       'A' + i, pstring[i],
	       ((pbits >> i) & 1 ? "��" : "��"),
	       i < 10 ? 'Q' + i : '0' + i - 10,
	       pstring[i + 16],
	       ((pbits >> (i + 16)) & 1 ? "��" : "��"));
    }
    clrtobot();
    while (
       (i = vmsg("�Ы� [A-5] �����]�w�A�� [Return] �����G"))!='\r')
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
	outs((pbits >> i) & 1 ? "��" : "��");
    }
    return pbits;
}

#ifdef CHESSCOUNTRY
static void
AddingChessCountryFiles(const char* apath)
{
    char filename[PATHLEN];
    char symbolicname[PATHLEN];
    char adir[PATHLEN];
    FILE* fp;
    fileheader_t fh;

    setadir(adir, apath);

    /* creating chess country regalia */
    snprintf(filename, sizeof(filename), "%s/chess_ensign", apath);
    close(OpenCreate(filename, O_WRONLY));

    strlcpy(symbolicname, apath, sizeof(symbolicname));
    stampfile(symbolicname, &fh);
    symlink("chess_ensign", symbolicname);

    strcpy(fh.title, "�� �Ѱ���� (����R���A�t�λݭn)");
    strcpy(fh.owner, str_sysop);
    append_record(adir, &fh, sizeof(fileheader_t));

    /* creating member list */
    snprintf(filename, sizeof(filename), "%s/chess_list", apath);
    if (!dashf(filename)) {
	fp = fopen(filename, "w");
	assert(fp);
	fputs("�Ѱ��W\n"
		"�b��            ����    �[�J���        ���ũγQ�֫R��\n"
		"�w�w�w�w�w�w    �w�w�w  �w�w�w�w�w      �w�w�w�w�w�w�w\n",
		fp);
	fclose(fp);
    }

    strlcpy(symbolicname, apath, sizeof(symbolicname));
    stampfile(symbolicname, &fh);
    symlink("chess_list", symbolicname);

    strcpy(fh.title, "�� �Ѱꦨ���� (����R���A�t�λݭn)");
    strcpy(fh.owner, str_sysop);
    append_record(adir, &fh, sizeof(fileheader_t));

    /* creating profession photos' dir */
    snprintf(filename, sizeof(filename), "%s/chess_photo", apath);
    Mkdir(filename);

    strlcpy(symbolicname, apath, sizeof(symbolicname));
    stampfile(symbolicname, &fh);
    symlink("chess_photo", symbolicname);

    strcpy(fh.title, "�� �Ѱ�Ӥ��� (����R���A�t�λݭn)");
    strcpy(fh.owner, str_sysop);
    append_record(adir, &fh, sizeof(fileheader_t));
}
#endif /* defined(CHESSCOUNTRY) */

/* �۰ʳ]�ߺ�ذ� */
void
setup_man(const boardheader_t * board, const boardheader_t * oldboard)
{
    char            genbuf[200];

    setapath(genbuf, board->brdname);
    Mkdir(genbuf);

#ifdef CHESSCOUNTRY
    if (oldboard == NULL || oldboard->chesscountry != board->chesscountry)
	if (board->chesscountry != CHESSCODE_NONE)
	    AddingChessCountryFiles(genbuf);
	// else // doesn't remove files..
#endif
}

void delete_board_link(boardheader_t *bh, int bid)
{
    assert(0<=bid-1 && bid-1<MAX_BOARD);
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
     snprintf(bakdir, sizeof(bakdir), "%s.bak", dir1);
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
    char            genbuf[PATHLEN], ans[4];
    int y;

    bid = getbnum(bname);
    if (!bid || !bname[0] || get_record(fn_board, &bh, sizeof(bh), bid) == -1) {
	vmsg(err_bid);
	return -1;
    }
    assert(0<=bid-1 && bid-1<MAX_BOARD);
    prints("�ݪO�W�١G%s %s\n�ݪO�����G%s\n�ݪObid�G%d\n�ݪOGID�G%d\n"
	   "�O�D�W��G%s", bh.brdname, (bh.brdattr & BRD_NOCREDIT) ?
           ANSI_COLOR(1;31) "[�w�]�w�o��L�峹�������y]" ANSI_RESET : "",
           bh.title, bid, bh.gid, bh.BM);
    bperm_msg(&bh);

    /* Ptt �o���_��|�ɨ�U�� */
    move(9, 0);
    if (bh.brdattr & BRD_SYMBOLIC) {
        snprintf(genbuf, sizeof(genbuf), "[�ݪO�s��] (D)�R�� [Q]����? ");
    } else {
        snprintf(genbuf, sizeof(genbuf), "(E)�]�w (V)�o����y%s%s [Q]����? ",
                 HasUserPerm(PERM_BOARD) ? " (B)Vote (S)�Ϧ^ (C)�X�� (G)�ֳz�ѥd" : "",
                 HasUserPerm(PERM_SYSSUBOP | PERM_SYSSUPERSUBOP | PERM_BOARD) ? " (D)�R��" : "");
    }
    getdata(10, 0, genbuf, ans, 3, LCECHO);
    if (isascii(*ans))
        *ans = tolower(*ans);

    if ((bh.brdattr & BRD_SYMBOLIC) && *ans) {
        switch (*ans) {
            case 'd':
            case 'q':
                break;
            default:
                vmsg("�T���ʳs���ݪO�A�Ъ����ץ���ݪO");
                break;
        }
    }

    switch (*ans) {
    case 'g':
	if (HasUserPerm(PERM_BOARD)) {
	    char            path[PATHLEN];
	    setbfile(genbuf, bname, FN_TICKET_LOCK);
	    setbfile(path, bname, FN_TICKET_END);
	    rename(genbuf, path);
	}
	break;
    case 's':
	if (HasUserPerm(PERM_BOARD)) {
	  snprintf(genbuf, sizeof(genbuf),
		   BBSHOME "/bin/buildir boards/%c/%s &",
		   bh.brdname[0], bh.brdname);
	    system(genbuf);
	}
	break;
    case 'c':
	if (HasUserPerm(PERM_BOARD)) {
	   char frombname[20], fromdir[PATHLEN];
	    CompleteBoard(MSG_SELECT_BOARD, frombname);
            if (frombname[0] == '\0' || !getbnum(frombname) ||
		!strcmp(frombname,bname))
	                     break;
            setbdir(genbuf, bname);
            setbdir(fromdir, frombname);
            merge_dir(genbuf, fromdir, 0);
	    touchbtotal(bid);
	}
	break;
    case 'b':
	if (HasUserPerm(PERM_BOARD)) {
	    char            bvotebuf[10];

	    memcpy(&newbh, &bh, sizeof(bh));
	    snprintf(bvotebuf, sizeof(bvotebuf), "%d", newbh.bvote);
	    move(20, 0);
	    prints("�ݪO %s ��Ӫ� BVote�G%d", bh.brdname, bh.bvote);
	    getdata_str(21, 0, "�s�� Bvote�G", genbuf, 5, NUMECHO, bvotebuf);
	    newbh.bvote = atoi(genbuf);
	    assert(0<=bid-1 && bid-1<MAX_BOARD);
	    substitute_record(fn_board, &newbh, sizeof(newbh), bid);
	    reset_board(bid);
	    log_usies("SetBoardBvote", newbh.brdname);
	    break;
	} else
	    break;
    case 'v':
	memcpy(&newbh, &bh, sizeof(bh));
	outs("�ݪO�o�媺�峹�������y��k�ثe��");
	outs((bh.brdattr & BRD_NOCREDIT) ?
             ANSI_COLOR(1;31) "�T��" ANSI_RESET : "���`");
	getdata(21, 0, "�T�w���H", genbuf, 5, LCECHO);
	if (genbuf[0] == 'y') {
            newbh.brdattr ^= BRD_NOCREDIT;
	    assert(0<=bid-1 && bid-1<MAX_BOARD);
	    substitute_record(fn_board, &newbh, sizeof(newbh), bid);
	    reset_board(bid);
	    log_usies("ViolateLawSet", newbh.brdname);
	}
	break;
    case 'd':
	if (!(HasUserPerm(PERM_BOARD) ||
		    (HasUserPerm(PERM_SYSSUPERSUBOP) && GROUPOP())))
	    break;
	getdata_str(9, 0, msg_sure_ny, genbuf, 3, LCECHO, "N");
	if (genbuf[0] != 'y' || !bname[0])
	    outs(MSG_DEL_CANCEL);
	else if (bh.brdattr & BRD_SYMBOLIC) {
	    delete_board_link(&bh, bid);
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
		     "     %s �ݪO %s �R��", bname, cuser.userid);
	    post_msg(BN_SECURITY, bh.title, "�Ъ`�N�R�����X�k��", "[�t�Φw����]");
	    assert(0<=bid-1 && bid-1<MAX_BOARD);
	    substitute_record(fn_board, &bh, sizeof(bh), bid);
	    reset_board(bid);
            sort_bcache();
	    log_usies("DelBoard", bh.title);
	    outs("�R�O����");
	}
	break;
    case 'e':
        y = 8;
	move(y++, 0); clrtobot();
	outs("������ [Return] ���ק�Ӷ��]�w");
	memcpy(&newbh, &bh, sizeof(bh));

	while (getdata(y, 0, "�s�ݪO�W�١G", genbuf, IDLEN + 1, DOECHO)) {
	    if (getbnum(genbuf)) {
                mvouts(y + 1, 0, "���~: ���s�ݪO�W�w�s�b\n");
            } else if ( !is_valid_brdname(genbuf) ) {
                mvouts(y + 1, 0, "���~: �L�k�ϥΦ��W�١C"
                       "�Шϥέ^�Ʀr�� _-. �B�}�Y���o���Ʀr�C\n");
            } else {
                if (genbuf[0] != bh.brdname[0]) {
                    // change to 0 if you want to force permission when renaming
                    // with different initial character.
                    const int free_rename = 1;
                    if (free_rename || HasUserPerm(PERM_BOARD)) {
                        mvouts(y + 1, 0, ANSI_COLOR(1;31)
                                "ĵ�i: �ݪO���r�����P,�j�ݪO��W�|�D�`�[,"
                                "�d�U���i���~�_�u�_�h�ݪO�|�a��"
                                ANSI_RESET "\n");
                    } else {
                        mvouts(y + 1, 0,
                                "���~: �s�¦W�ٲĤ@�Ӧr���Y���P(�j�p�g���O)"
                                "�n�ݪO�`�ޥH�W���Ť~�i�]�w\n");
                        continue;
                    }
                }
		strlcpy(newbh.brdname, genbuf, sizeof(newbh.brdname));
		break;
	    }
	}
        y++;

	do {
	    getdata_str(y, 0, "�ݪO���O�G", genbuf, 5, DOECHO, bh.title);
	    if (strlen(genbuf) == 4)
		break;
	} while (1);
        y++;

	strcpy(newbh.title, genbuf);
	newbh.title[4] = ' ';

	// 7 for category
	getdata_str(y++, 0, "�ݪO�D�D�G", genbuf, BTLEN + 1 -7,
                    DOECHO, bh.title + 7);
	if (genbuf[0])
	    strlcpy(newbh.title + 7, genbuf, sizeof(newbh.title) - 7);

        do {
            int uids[MAX_BMs], i;
            if (!getdata_str(y, 0, "�s�O�D�W��G", genbuf, IDLEN * 3 + 3,
                             DOECHO, bh.BM) || strcmp(genbuf, bh.BM) == 0)
                break;
            // TODO �Ӳz�ӻ��b�o�� normalize �@������n�F�i���ثe���G���_�Ǫ�
            // �N�ި�סA�|���H�� BM list �]�w [ ...... / some_uid]�A�N�|�ܦ�
            // �@���x�D�O�D�P�ɤS���H(maybe�p�ժ�)���޲z�v���ӥB�٤���ܥX�ӡC
            // �o�]�p���V�|�A�]�L�k�P�_�O���p�߻~�](�h�F�ť�)�άO�G�N���A�A��
            // �٦��H�H���o�̥��y�^��ܫӮ𵲪G�y���ӭ^�媺ID�N�~��o�v���C
            // �������Ӿ�Ө����A���� normalize�C
	    trim(genbuf);
            parseBMlist(genbuf, uids);
            mvouts(y + 2, 0, " ��ڥͮĪ��O�DID: " ANSI_COLOR(1));
            for (i = 0; i < MAX_BMs && uids[i] >= 0; i++) {
                prints("%s ", getuserid(uids[i]));
            }
            outs(ANSI_RESET "\n");

            // ref: does_board_have_public_bm
            if (genbuf[0] <= ' ')
                outs(ANSI_COLOR(1;31)
                     " *** �]���}�Y�O�ťթΤ���, �ݪO�����W���Ϋ�i��"
                     "�O�D�W��|��ܬ��x�D���εL ***"
                     ANSI_RESET "\n");
            mvprints(y+5, 0, "�`�N: �귽�^�����P�s����v�w���Υ���ۤv�]���O�D�C\n"
                     "�Ա��Ш�(X)->(L)�t�Χ�s�O���C\n");
            if (getdata(y + 4, 0, "�T�w���O�D�W�楿�T?[y/N] ", ans,
                        sizeof(ans), LCECHO) &&
                ans[0] == 'y') {
                strlcpy(newbh.BM, genbuf, sizeof(newbh.BM));
                move(y + 1, 0); clrtobot();
                break;
            }
            move(y, 0); clrtobot();
        } while (1);
        y++;

#ifdef CHESSCOUNTRY
	if (HasUserPerm(PERM_BOARD)) {
	    snprintf(genbuf, sizeof(genbuf), "%d", bh.chesscountry);
	    if (getdata_str(y++, 0,
			"�]�w�Ѱ� (0)�L (1)���l�� (2)�H�� (3)��� (4) �¥մ�",
			ans, sizeof(ans), NUMECHO, genbuf)){
		newbh.chesscountry = atoi(ans);
		if (newbh.chesscountry > CHESSCODE_MAX ||
			newbh.chesscountry < CHESSCODE_NONE)
		    newbh.chesscountry = bh.chesscountry;
	    }
	}
#endif /* defined(CHESSCOUNTRY) */

	if (HasUserPerm(PERM_BOARD)) {
	    move(1, 0);
	    clrtobot();
	    newbh.brdattr = setperms(newbh.brdattr, str_permboard);
	    move(1, 0);
	    clrtobot();
	}

	{
	    const char* brd_symbol;
	    if (newbh.brdattr & BRD_GROUPBOARD)
        	brd_symbol = "�U";
	    else
		brd_symbol = "��";

	    newbh.title[5] = brd_symbol[0];
	    newbh.title[6] = brd_symbol[1];
	}

	if (HasUserPerm(PERM_BOARD) && !(newbh.brdattr & BRD_HIDE)) {
            getdata(y++, 0, "�]�wŪ�g�v��(y/N)�H", ans, sizeof(ans), LCECHO);
	    if (*ans == 'y') {
		getdata_str(y++, 0, "���� [R]�\\Ū (P)�o��H", ans, sizeof(ans), LCECHO,
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

	getdata(b_lines - 1, 0, "�бz�T�w(Y/N)�H[Y]", genbuf, 4, LCECHO);

	if ((*genbuf != 'n') && memcmp(&newbh, &bh, sizeof(bh))) {
	    char buf[64];

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
	    assert(0<=bid-1 && bid-1<MAX_BOARD);
	    substitute_record(fn_board, &newbh, sizeof(newbh), bid);
	    reset_board(bid);
            sort_bcache();
	    log_usies("SetBoard", newbh.brdname);

	    snprintf(buf, sizeof(buf), "[�ݪO�ܧ�] %s (by %s)", bh.brdname, cuser.userid);
	    snprintf(genbuf, sizeof(genbuf),
		    "�O�W: %s => %s\n"
		    "�O�D: %s => %s\n",
		    bh.brdname, newbh.brdname, bh.BM, newbh.BM);
	    post_msg(BN_SECURITY, buf, genbuf, "[�t�Φw����]");
	}
    }
    return 0;
}

/* �]�w�ݪO */
int
m_board(void)
{
    char            bname[32];

    vs_hdr("�ݪO�]�w");
    CompleteBoardAndGroup(msg_bid, bname);
    if (!*bname)
	return 0;
    m_mod_board(bname);
    return 0;
}

/* �]�w�t���ɮ� */
int
x_file(void)
{
    return psb_admin_edit();
}

static int add_board_record(const boardheader_t *board)
{
    int bid;
    if ((bid = getbnum("")) > 0) {
	assert(0<=bid-1 && bid-1<MAX_BOARD);
	substitute_record(fn_board, board, sizeof(boardheader_t), bid);
	reset_board(bid);
        sort_bcache();
    } else if (SHM->Bnumber >= MAX_BOARD) {
	return -1;
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

    vs_hdr("�إ߷s�O");
    memset(&newboard, 0, sizeof(newboard));

    newboard.gid = whatclass;
    if (newboard.gid == 0) {
	vmsg("�Х���ܤ@�����O�A�}�O!");
	return -1;
    }
    do {
	if (!getdata(3, 0, msg_bid, newboard.brdname,
		     sizeof(newboard.brdname), DOECHO))
	    return -1;
        if (is_valid_brdname(newboard.brdname))
            break;
        // some i* need to be acknowledged
        vmsg("�L�k�ϥΦ��W�١A�Шϥέ^�Ʀr�� _-. �B�}�Y���o���Ʀr�C");
    } while (true);

    do {
	getdata(6, 0, "�ݪO���O�G", genbuf, 5, DOECHO);
	if (strlen(genbuf) == 4)
	    break;
    } while (1);

    strcpy(newboard.title, genbuf);
    newboard.title[4] = ' ';

    getdata(8, 0, "�ݪO�D�D�G", genbuf, BTLEN + 1, DOECHO);
    if (genbuf[0])
	strlcpy(newboard.title + 7, genbuf, sizeof(newboard.title) - 7);
    setbpath(genbuf, newboard.brdname);

    // Recover ���u���ӳB�z�ؿ��w�s�b(��.BRD�S��)�����p�A���M�N�|�b
    // ���H�~�ήɳy���P�ӥؿ����h�� board entry �����ΡC
    // getbnum(newboard.brdname) > 0 �ɥѩ�ثe�]�p�O�| new board,
    // �ҥH�u���}�O��u�|�y�� bcache ���áA���i���V�C
    if (getbnum(newboard.brdname) > 0) {
	vmsg("���ݪO�w�g�s�b! �Ш����P�^��O�W");
	return -1;
    }
    if (Mkdir(genbuf) != 0) {
        if (errno == EEXIST) {
            if (!recover) {
                vmsg("�ݪO�ؿ��w�s�b�A�Y�O�n�״_�ݪO�Х� R ���O�C");
                return -1;
            }
        } else {
            vmsgf("�t�ο��~ #%d, �L�k�إ߬ݪO�ؿ��C", errno);
            return -1;
        }
    }
    newboard.brdattr = 0;
#ifdef DEFAULT_AUTOCPLOG
    newboard.brdattr |= BRD_CPLOG;
#endif

    if (HasUserPerm(PERM_BOARD)) {
	move(1, 0);
	clrtobot();
	newboard.brdattr = setperms(newboard.brdattr, str_permboard);
	move(1, 0);
	clrtobot();
    }
    getdata(9, 0, "�O�ݪO? (N:�ؿ�) (Y/n)�G", genbuf, 3, LCECHO);
    if (genbuf[0] == 'n')
    {
	newboard.brdattr |= BRD_GROUPBOARD;
	newboard.brdattr &= ~BRD_CPLOG;
    }

	{
	    const char* brd_symbol;
	    if (newboard.brdattr & BRD_GROUPBOARD)
        	brd_symbol = "�U";
	    else
		brd_symbol = "��";

	    newboard.title[5] = brd_symbol[0];
	    newboard.title[6] = brd_symbol[1];
	}

    newboard.level = 0;
    getdata(11, 0, "�O�D�W��G", newboard.BM, sizeof(newboard.BM), DOECHO);
#ifdef CHESSCOUNTRY
    if (getdata_str(12, 0, "�]�w�Ѱ� (0)�L (1)���l�� (2)�H�� (3)���", ans,
		sizeof(ans), LCECHO, "0")){
	newboard.chesscountry = atoi(ans);
	if (newboard.chesscountry > CHESSCODE_MAX ||
		newboard.chesscountry < CHESSCODE_NONE)
	    newboard.chesscountry = CHESSCODE_NONE;
    }
#endif /* defined(CHESSCOUNTRY) */

    if (HasUserPerm(PERM_BOARD) && !(newboard.brdattr & BRD_HIDE)) {
	getdata_str(14, 0, "�]�wŪ�g�v��(Y/N)�H", ans, sizeof(ans), LCECHO, "N");
	if (*ans == 'y') {
	    getdata_str(15, 0, "���� [R]�\\Ū (P)�o��H", ans, sizeof(ans), LCECHO, "R");
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

    if (add_board_record(&newboard) < 0) {
	if (SHM->Bnumber >= MAX_BOARD)
	    vmsg("�ݪO�w���A�Ь��t�ί���");
	else
    	    vmsg("�ݪO�g�J����");

	setbpath(genbuf, newboard.brdname);
	rmdir(genbuf);
	return -1;
    }

    getbcache(whatclass)->childcount = 0;
    pressanykey();
    setup_man(&newboard, NULL);
    outs("\n�s�O����");
    post_newboard(newboard.title, newboard.brdname, newboard.BM);
    log_usies("NewBoard", newboard.title);
    pressanykey();
    return 0;
}

int make_board_link(const char *bname, int gid)
{
    boardheader_t   newboard;
    int bid;

    bid = getbnum(bname);
    if(bid==0) return -1;
    assert(0<=bid-1 && bid-1<MAX_BOARD);
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
     *   is a link, so just let SYSOP know it. You may want to read
     *   board.c:load_boards().
     */

    strlcpy(newboard.brdname, bname, sizeof(newboard.brdname));
    newboard.brdname[strlen(bname) - 1] = '~';
    strlcpy(newboard.title, bcache[bid - 1].title, sizeof(newboard.title));
    strcpy(newboard.title + 5, "�I�ݪO�s��");

    newboard.gid = gid;
    BRD_LINK_TARGET(&newboard) = bid;
    newboard.brdattr = BRD_SYMBOLIC;

    if (add_board_record(&newboard) < 0)
	return -1;
    return bid;
}

int make_board_link_interactively(int gid)
{
    char buf[32];
    vs_hdr("�إ߬ݪO�s��");

    outs("\n\n�Ъ`�N: �ݪO�s���|�ɭP�s���Ҧb���s�դ��p�ժ��@�˦��s�պ޲z�v���C\n"
         "(�ҡA�b�s�� A [�p�ժ�: abc]�U�إߤF�q���s�� B ���ݪO C ���s���A\n"
         " ���G�|�ɭP abc �b�i�J�ݪO C �ɤ]���s�պ޲z�v���C)\n\n"
         "�o�O�w���{�H�ӥB�L�ѡC�b�إ߬ݪO�ɽнT�w�z�w�F�ѥi��|�o�ͪ����D�C\n");

    if (tolower(vmsg("�T�w�n�إ߷s�ݪO�s���ܡH [y/N]: ")) != 'y')
        return -1;

    CompleteBoard(msg_bid, buf);
    if (!buf[0])
	return -1;


    if (make_board_link(buf, gid) < 0) {
	vmsg("�ݪO�s���إߥ���");
	return -1;
    }
    log_usies("NewSymbolic", buf);
    return 0;
}

static void
adm_give_id_money(const char *user_id, int money, const char *mail_title)
{
    char tt[TTLEN + 1] = {0};
    int  unum = searchuser(user_id, NULL);

    // XXX �����̦��G�Q�γo�ӥ\��ӦP�ɵo���Φ����Areturn value �i��O 0
    // (�Y�N������Q����)
    if (unum <= 0 || pay_as_uid(unum, -money, "����%s: %s",
                                money >= 0 ? "�o���]" : "����",
                                mail_title) < 0) {
	move(12, 0);
	clrtoeol();
	prints("id:%s money:%d ����a!!", user_id, money);
	pressanykey();
    } else {
	snprintf(tt, sizeof(tt), "%s : %d " MONEYNAME, mail_title, money);
	mail_id(user_id, tt, "etc/givemoney.why", "[" BBSMNAME "�Ȧ�]");
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

    getdata(0, 0, "���w�ϥΪ�(S) �����ϥΪ�(A) ����(Q)�H[S]", buf, 3, LCECHO);
    if (buf[0] == 'q')
	return 1;
    else if (buf[0] == 'a') {
	to_all = 1;
	getdata(1, 0, "�o�h�ֿ��O?", buf, 20, DOECHO);
	money = atoi(buf);
	if (money <= 0) {
	    move(2, 0);
	    vmsg("��J���~!!");
	    return 1;
	}
    } else {
	if (veditfile("etc/givemoney.txt") < 0)
	    return 1;
    }

    clear();

    // check money:
    // 1. exceed limit
    // 2. id unique

    unlink("etc/givemoney.log");
    if (!(fp2 = fopen("etc/givemoney.log", "w")))
	return 1;

    getdata(0, 0, "�ʥΰ�w!�п�J����z��(�p���ʦW��):", reason, 40, DOECHO);
    fprintf(fp2,"\n�ϥβz��: %s\n", reason);

    getdata(1, 0, "�n�o���F��(Y/N)[N]", buf, 3, LCECHO);
    if (buf[0] != 'y') {
        fclose(fp2);
	return 1;
    }

    getdata(1, 0, "���]�U���D �G", tt, TTLEN, DOECHO);
    fprintf(fp2,"\n���]�U���D: %s\n", tt);
    move(2, 0);

    vmsg("�s���]�U���e");
    if (veditfile("etc/givemoney.why") < 0) {
        fclose(fp2);
	return 1;
    }

    vs_hdr("�o����...");
    if (to_all) {
	int             i, unum;
	for (unum = SHM->number, i = 0; i < unum; i++) {
	    if (bad_user_id(SHM->userid[i]))
		continue;
	    id = SHM->userid[i];
	    adm_give_id_money(id, money, tt);
            fprintf(fp2,"�� %s : %d\n", id, money);
            count++;
	}
        sprintf(buf, "(%d�H:%d" MONEYNAME ")", count, count*money);
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
	    adm_give_id_money(id, money, tt);
            fprintf(fp2,"�� %s : %d\n", id, money);
            total_money += money;
            count++;
	}
	fclose(fp);
        sprintf(buf, "(%d�H:%d" MONEYNAME ")", count, total_money);
        strcat(reason, buf);

    }

    fclose(fp2);

    sprintf(buf, "%s ���]��: %s", cuser.userid, reason);
    post_file(BN_SECURITY, buf, "etc/givemoney.log", "[���]�����i]");
    pressanykey();
    return FULLUPDATE;
}
