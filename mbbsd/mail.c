#include "bbs.h"

////////////////////////////////////////////////////////////////////////
// Build configuration
#ifndef NEWMAIL_CHECK_RANGE
// Scanning whole .DIR will be too slow - so we only check few mails.
// checking only 1 mail works more like brc style.
// 20 (one page) or 5 may be good choices.
// #define NEWMAIL_CHECK_RANGE (1)
#define NEWMAIL_CHECK_RANGE (5)
#endif

////////////////////////////////////////////////////////////////////////
// Local definition
enum SHOWMAIL_MODES {
    SHOWMAIL_NORM = 0,
    SHOWMAIL_RANGE,
};

struct ReadNewMailArg {
    int idc;
    int *delmsgs;
    int delcnt;
    int mrd;
};

////////////////////////////////////////////////////////////////////////
// Local variables (to speed up)
static int      mailkeep = 0;
static int      mailmaxkeep = 0;
static char     currmaildir[PATHLEN];
static char     msg_cc[] = ANSI_COLOR(32) "[�s�զW��]" ANSI_RESET "\n";
static char     listfile[] = "list.0";
static int	showmail_mode = SHOWMAIL_NORM;
static const    onekey_t mail_comms[];

////////////////////////////////////////////////////////////////////////
// Core utility functions

// Checks if given address is a full qualified mail address in user@host.domain
static int bad_email_offset = 0;

int
is_valid_email(const char *full_address)
{
    const char *addr = full_address;
    char c;
    int cAt = 0, cDot = 0;

    bad_email_offset = 0;
    if (!*addr)
	return 0;		/* blank */

    for (; (c = *addr); addr++, bad_email_offset++) {
        if (!isascii(c))
            return 0;
        if (isalnum(c))
           continue;
        switch(c) {
            case '-':
            case '_':
            case '+':
                continue;

            case '.':
                if (cAt)
                    cDot++;
                continue;

            case '@':
                cAt++;
                continue;
        }
        return 0;
    }
    if (cAt == 1 && cDot > 0)
        return 1;

    bad_email_offset = -1;
    return 0;
}

int
invalidaddr(const char *addr) {
    int r = is_valid_email(addr);
    if (r)
        return 0;

#ifdef DEBUG_FWDADDRERR
    clear();
    mvouts(2, 0,
           "�z��J�� email ��}���~ (address error)�C \n\n"
           "�ѩ�̪�\\�h�H�������J���T����}(id��mail)��t�η|�P�_���~\n"
           "���ˬd���X��]�A�ҥH�ڭ̻ݭn���T�����~�^���C\n\n"
           "�p�G�A�T�ꥴ���F�A�Ъ������L�U���������C\n"
           "�p�G�A�{���A��J����}�T��O�諸�A�Ч�U�����T���ƻs�_��\n"
           "�öK�� " BN_BUGREPORT " �O�C�������y�����K�`�P��p�C\n\n"
           ANSI_COLOR(1;33));
    prints("��l��J��}: [%s]\n", addr);


    if (bad_email_offset >= 0) {
        char c = addr[bad_email_offset];
        prints("���~��m: �� %d �r��: 0x%02X [ %c ]",
               bad_email_offset,  (unsigned char)c,
               isascii(c) && isprint(c) ? c : ' ');
    } else {
        outs("���~��]: email �Φ������T (�D user@host.domain)");
    }
    outs(ANSI_RESET "\n");

    pressanykey();
    clear();
#endif
    return 1;
}

int
load_mailalert(const char *userid)
{
    struct stat     st;
    char            maildir[PATHLEN];
    int             fd;
    register int    num;
    fileheader_t    my_mail;

    sethomedir(maildir, userid);
    if (!HasUserPerm(PERM_BASIC))
	return 0;
    if (stat(maildir, &st) < 0)
	return 0;
    num = st.st_size / sizeof(fileheader_t);
    if (num <= 0)
	return 0;
    if (num > NEWMAIL_CHECK_RANGE)
	num = NEWMAIL_CHECK_RANGE;

    /* �ݬݦ��S���H���٨SŪ�L�H�q�ɧ��^�Y�ˬd�A�Ĳv���� */
    if ((fd = open(maildir, O_RDONLY)) >= 0) {
	lseek(fd, st.st_size - sizeof(fileheader_t), SEEK_SET);
	while (num--) {
	    read(fd, &my_mail, sizeof(fileheader_t));
	    if (!(my_mail.filemode & FILE_READ)) {
		close(fd);
		return ALERT_NEW_MAIL;
	    }
	    lseek(fd, -(off_t) 2 * sizeof(fileheader_t), SEEK_CUR);
	}
	close(fd);
    }
    return 0;
}

int
sendalert(const char *userid, int alert) {
    int tuid;

    if ((tuid = searchuser(userid, NULL)) == 0)
	return -1;

    return sendalert_uid(tuid, alert);
}

int
sendalert_uid(int uid, int alert) {
    userinfo_t *uentp = NULL;
    int n, i;

    n = count_logins(uid, 0);
    for (i = 1; i <= n; i++)
	if ((uentp = (userinfo_t *) search_ulistn(uid, i)))
	    uentp->alerts |= alert;

    return 0;
}

void setmailalert()
{
    if(load_mailalert(cuser.userid))
           currutmp->alerts |= ALERT_NEW_MAIL;
    else
           currutmp->alerts &= ~ALERT_NEW_MAIL;
}

int
mail_log2id_text(const char *id, const char *title, const char *message,
                 const char *owner, char newmail) {
    fileheader_t    mhdr;
    char            dst[PATHLEN], dirf[PATHLEN];

    sethomepath(dst, id);
    if (stampfile(dst, &mhdr) < 0)
	return -1;

    strlcpy(mhdr.owner, owner, sizeof(mhdr.owner));
    strlcpy(mhdr.title, title, sizeof(mhdr.title));
    mhdr.filemode = newmail ? 0 :  FILE_READ;
    log_filef(dst, LOG_CREAT, "%s", message);

    sethomedir(dirf, id);
    append_record(dirf, &mhdr, sizeof(mhdr));
    return 0;
}


// TODO add header option?
int
mail_log2id(const char *id, const char *title, const char *src,
            const char *owner, char newmail, char trymove) {
    fileheader_t    mhdr;
    char            dst[PATHLEN], dirf[PATHLEN];

    sethomepath(dst, id);
    if (stampfile(dst, &mhdr) < 0)
	return -1;

    strlcpy(mhdr.owner, owner, sizeof(mhdr.owner));
    strlcpy(mhdr.title, title, sizeof(mhdr.title));
    mhdr.filemode = newmail ? 0 :  FILE_READ;

    // XXX try link first?
    //if (HardLink(src, dst) < 0 && Copy(src, dst) < 0)
    //	return -1;
    if (trymove)
    {
	if (Rename(src, dst) < 0)
	    return -1;
    } else {
	if (Copy(src, dst) < 0)
	    return -1;
    }

    sethomedir(dirf, id);
    // do not forward.
    append_record(dirf, &mhdr, sizeof(mhdr));
    return 0;
}

int
mail_id(const char *id, const char *title, const char *src, const char *owner)
{
    fileheader_t    mhdr;
    char            dst[PATHLEN], dirf[PATHLEN];
    sethomepath(dst, id);
    if (stampfile(dst, &mhdr) < 0)
	return -1;

    strlcpy(mhdr.owner, owner, sizeof(mhdr.owner));
    strlcpy(mhdr.title, title, sizeof(mhdr.title));
    mhdr.filemode = 0;
    if (Copy(src, dst) < 0)
	return -1;

    sethomedir(dirf, id);
    append_record_forward(dirf, &mhdr, sizeof(mhdr), id);
    sendalert(id, ALERT_NEW_MAIL);
    return 0;
}

void
m_init(void)
{
    sethomedir(currmaildir, cuser.userid);
}

static void
loadmailusage(void)
{
    mailkeep = get_num_records(currmaildir,sizeof(fileheader_t));
}

int
get_max_keepmail(const userec_t *u) {
    int lvl = u->userlevel;
    int keep = MAX_KEEPMAIL;  // default: 200

    if (lvl & (PERM_SYSOP | PERM_MAILLIMIT)) {
        return 0;
    }

    if (lvl & (PERM_SYSSUPERSUBOP)) {
        keep = MAX_KEEPMAIL * 10.0;  // 700 -> 2000
    } else if (lvl & (PERM_MANAGER | PERM_PRG | PERM_SYSSUBOP)) {
        keep = MAX_KEEPMAIL * 5.0;  // 500 -> 1000
#ifdef PLAY_ANGEL
    } else if (lvl & (PERM_ANGEL)) {
        keep = MAX_KEEPMAIL * 3.5;  // 700 -> 700
#endif
    } else if (lvl & (PERM_BM)) {
        keep = MAX_KEEPMAIL * 2.5;  // 300 -> 500
    } else if (!(lvl & (PERM_LOGINOK))) {
        // less than normal user
        keep = MAX_KEEPMAIL * 0.5;  // 100
    }
    return keep + u->exmailbox;
}

void
setupmailusage(void)
{
    mailmaxkeep = get_max_keepmail(&cuser);
    loadmailusage();
}

#define MAILBOX_LIM_OK   0
#define MAILBOX_LIM_KEEP 1
#define MAILBOX_LIM_HARD  2

static int
chk_mailbox_limit(void)
{
    if (HasUserPerm(PERM_SYSOP) || HasUserPerm(PERM_MAILLIMIT))
	return MAILBOX_LIM_OK;

    if (!mailkeep)
	setupmailusage();

    if (mailkeep <= mailmaxkeep)
        return MAILBOX_LIM_OK;

    if ((mailkeep / 50 < mailmaxkeep)
        || HasUserPerm(PERM_ADMIN))
        return MAILBOX_LIM_KEEP;
    else
        return MAILBOX_LIM_HARD;
}

static void
do_hold_mail(const char *fpath, const char *receiver, const char *holder,
             const char *save_title)
{
    char            buf[PATHLEN], title[128];
    char            holder_dir[PATHLEN];

    fileheader_t    mymail;

    sethomepath(buf, holder);
    stampfile(buf, &mymail);

    mymail.filemode = FILE_READ ;
    strlcpy(mymail.owner, "[��.��.��]", sizeof(mymail.owner));
    if (receiver) {
	snprintf(title, sizeof(title), "(%s) %s", receiver, save_title);
	strlcpy(mymail.title, title, sizeof(mymail.title));
    } else
	strlcpy(mymail.title, save_title, sizeof(mymail.title));

    sethomedir(holder_dir, holder);

    unlink(buf);
    Copy(fpath, buf);
    append_record_forward(holder_dir, &mymail, sizeof(mymail), holder);
}

int
do_innersend(const char *userid, char *mfpath, const char *title, char *newtitle)
{
    fileheader_t    mhdr;
    char            fpath[PATHLEN];
    char	    _mfpath[PATHLEN];
    int		    oldstat = currstat;
    char save_title[STRLEN];

    if (is_rejected(userid)) {
        vmsg("���ڦ��C");
        return -2;
    }

    if (!mfpath)
	mfpath = _mfpath;

    setutmpmode(SMAIL);

    sethomepath(mfpath, userid);
    stampfile(mfpath, &mhdr);
    strlcpy(mhdr.owner, cuser.userid, sizeof(mhdr.owner));
    strlcpy(save_title, title, sizeof(save_title));

    if (vedit2(mfpath, YEA, save_title,
		EDITFLAG_ALLOWTITLE | EDITFLAG_KIND_SENDMAIL) == EDIT_ABORTED)
    {
	unlink(mfpath);
	setutmpmode(oldstat);
	return -2;
    }

    strlcpy(mhdr.title, save_title, sizeof(mhdr.title));
    if (newtitle) strlcpy(newtitle, save_title, STRLEN);
    sethomedir(fpath, userid);
    if (append_record_forward(fpath, &mhdr, sizeof(mhdr), userid) == -1)
    {
        unlink(mfpath);
        setutmpmode(oldstat);
        return -1;
    }
    sendalert(userid, ALERT_NEW_MAIL);
    setutmpmode(oldstat);
    return 0;
}

/* �H�����H */
static int
send_inner_mail(const char *fpath, const char *title, const char *receiver) {
    char            fname[PATHLEN];
    fileheader_t    mymail;
    char            rightid[IDLEN+1];

    if (!searchuser(receiver, rightid))
	return -2;

    /* to avoid DDOS of disk */
    sethomedir(fname, rightid);
    if (strcmp(rightid, cuser.userid) == 0) {
	if (chk_mailbox_limit())
	    return -4;
    }

    sethomepath(fname, rightid);
    stampfile(fname, &mymail);
    if (!strcmp(rightid, cuser.userid)) {
	/* Using BBSNAME may be too loooooong. */
	strlcpy(mymail.owner, "[����]", sizeof(mymail.owner));
	mymail.filemode = FILE_READ;
    } else
	strlcpy(mymail.owner, cuser.userid, sizeof(mymail.owner));
    strlcpy(mymail.title, title, sizeof(mymail.title));
    unlink(fname);
    Copy(fpath, fname);
    sethomedir(fname, rightid);
    append_record_forward(fname, &mymail, sizeof(mymail), rightid);
    sendalert(receiver, ALERT_NEW_MAIL);
    return 0;
}

static char *
gen_auth_code(const char *prefix, char *buf, int length) {
    // prevent ambigious characters: oOlI
    const char *alphabets = "abcdefghjkmnpqrstuvwxyz";
    int i;

    if (!prefix)
        prefix = "";

    strlcpy(buf, prefix, length);
    buf[length-1] = 0;
    for (i = strlen(prefix); i < length - 1; i++)
        buf[i] = alphabets[random() % strlen(alphabets)];
    return buf;
}

////////////////////////////////////////////////////////////////////////
// User-interface procedures
int
setforward(void) {
    char            buf[PATHLEN], ip[50] = "", yn[4];
    FILE           *fp;
    const char *fn_forward_auth = ".secure/forward_auth",
               *fn_forward_auth_dir = ".secure";
    char auth_code[16] = "";
    time4_t auth_time = 0;
    const char *prefix = "va";

    if (!HasBasicUserPerm(PERM_LOGINOK))
	return DONOTHING;

    // There may be race condition but that's probably ok.
    setuserfile(buf, fn_forward_auth);
    if (dashs(buf) > 0) {
        // load forward auth
        fp = fopen(buf, "rt");
        auth_time = dasht(buf);
        if (fp) {
            fgets(ip, sizeof(ip), fp);
            fgets(auth_code, sizeof(auth_code), fp);
            fclose(fp);
            chomp(ip);
            chomp(auth_code);
        } else {
            unlink(buf);
        }
    }

    // verify auth
    if (*ip && *auth_code) {
        char input[sizeof(auth_code)] = "";
        int hours = (now - auth_time) / 3600;
        int min_hours = 24;

        if (hours < 0)
            hours = min_hours;

        vs_hdr("���Ҧ۰���H");
        prints("\n��H�H�c: %s\n", ip);


        if (hours < min_hours) {
            prints("���U���i���H/�󴫫H�c�|�� %d �p��\n", min_hours - hours);
        } else {
            outs("�Y�Q���H/�󴫫H�c�Ъ����� Enter\n");
        }

        if (getdata(5, 0, "�п�J�W����H�H�c���쪺�����ҽX: ", input,
                    sizeof(input), LCECHO) &&
            strcmp(auth_code, input) == 0) {
            outs(ANSI_COLOR(1;32) "�T�{���Ҧ��\\ " ANSI_RESET "\n");
            unlink(buf);
            // write auth!
            setuserfile(buf, FN_FORWARD);
            fp = fopen(buf, "wt");
            if (fp) {
                fputs(ip, fp);
                fclose(fp);
                prints("��H�H�c�w�]�w�� %s", ip);
            } else {
                outs(ANSI_COLOR(1;31) "�t�β��` - ��H�H�c�]�w���ѡC" ANSI_RESET);
            }
            pressanykey();
            return FULLUPDATE;
        }

        // incorrect code.
        outs("���ҽX���~�C\n");
        if (*input && !str_starts_with(input, prefix))
            prints("���ҽX���� %s �}�Y���r��C\n", prefix);

        // valid for at least one day.
        if (hours >= min_hours) {
            if (getdata(10, 0, "�n���H�H�c�δ��H�c��? [y/N]", yn, sizeof(yn),
                        LCECHO) && yn[0] == 'y') {
                unlink(buf);
            } else {
                return FULLUPDATE;
            }
        } else {
            prints("�нT�{����H���A��J���ҽX�C\n"
                   "�Y�W�L %d �p�ɤ�������A"
                   "�i��A����J���~���ҽX���ܴ��H�c�έ��o�C\n", min_hours);
            pressanykey();
            return FULLUPDATE;
        }
    }

    // create new one.
    vs_hdr("�]�w�۰���H");
    outs(ANSI_COLOR(1;36) "\n\n"
	"�ѩ�\\�h�ϥΪ̦��N�εL�N���]�w���~�����y���۰���H�Q�c�N�ϥΡA\n"
	"�������w���ʤΨ���s�i�H���Ҷq�A�۰���H���H�U�]�w:\n"
        " - �H��̤@�ߧאּ�]�w�۰���H�̪� ID\n"
        " - �n���g�L Email �{��\n"
        " - ����]�w���P�����䥦�ϥΪ�\n"
        "\n"
	"���K���B�Цh����\n"
	ANSI_RESET "\n");

    setuserfile(buf, FN_FORWARD);
    if ((fp = fopen(buf, "r"))) {
	fscanf(fp, "%" toSTR(sizeof(ip)) "s", ip);
	fclose(fp);
#ifdef UNTRUSTED_FORWARD_TIMEBOMB
        if (dasht(buf) < UNTRUSTED_FORWARD_TIMEBOMB)
            unlink(buf);
#endif

    }
    chomp(ip);
    prints("�ثe�]�w�۰���H��: %s",
           dashf(buf) ? ip : ANSI_COLOR(1;31)"(����)" ANSI_RESET);
    getdata_buf(b_lines - 1, 0, "�п�J�۰���H��Email: ",
		ip, sizeof(ip), DOECHO);
    strip_blank(ip, ip);

    if (*ip) {
        if (strcasestr(ip, str_mail_address) ||
            strchr(ip, '@') == NULL) {
            vmsg("�T��۰���H�������䥦�ϥΪ�");
            return FULLUPDATE;
        }

        if (invalidaddr(ip)) {
            vmsg("Email ��J���~");
            return FULLUPDATE;
        }

        getdata(b_lines, 0,
                "�T�w�n�ϥΦ۰���H? "
                "(y)�}�l���� (n)���� [Q]���ק�]�w [y/n/Q]: ",
                yn, sizeof(yn), LCECHO);
        if (*yn == 'y') {
            char authtemp[PATHLEN];
            setuserfile(buf, FN_FORWARD);
            unlink(buf);
            setuserfile(buf, fn_forward_auth_dir);
            Mkdir(buf);
            setuserfile(buf, fn_forward_auth);
            fp = fopen(buf, "wt");
            if (!fp) {
                vmsg("�t�ο��~ - �L�k�]�w�۰���H�C");
                return FULLUPDATE;
            }
            gen_auth_code(prefix, auth_code, sizeof(auth_code));
            strlcpy(authtemp, buf, sizeof(authtemp));
            strlcat(authtemp, ".tmp", sizeof(authtemp));
            fprintf(fp, "%s\n%s\n", ip, auth_code);
            fclose(fp);
            fp = fopen(authtemp, "wt");
            if (!fp) {
                unlink(buf);
                vmsg("�t�ο��~ - �L�k�e�X�T�{�H�C");
                return FULLUPDATE;
            }
            snprintf(buf, sizeof(buf), BBSNAME " �۰���H�T�{ (%s)",
                     cuser.userid);
            fprintf(fp,
                    BBSNAME " �۰���H�T�{\n\n"
                    "�ϥΪ� %s �n�D�۰���H�ܦ��H�c %s\n"
                    "�Y�o���O�z���b���A�Ъ����R�����H��C\n"
                    "�Y�T�w�n�}�Ҧ۰���H�A"
                    "�п�J�U�� %s �}�Y�����ҽX��۰���H�]�w:\n"
                    " %s\n", cuser.userid, ip, prefix, auth_code);
            fclose(fp);
            bsmtp(authtemp, buf, ip, cuser.userid);
            unlink(authtemp);
            vs_hdr("�T�{�H��w�H�X");
            prints("\n\n"
                 ANSI_COLOR(1;33)
                 "���T�{ Email �����T�ʡA�t�Τw�H�X�@���t���ҽX���T�{�H�A\n"
                 "���D��: %s\n"
                 "�۰���H�b�����T�{�e���|�ҥΡC\n" ANSI_RESET
                 "�Цb����ӫH���A���i�J�]�w��H�H�c�ÿ�J���ҽX�C\n", buf);
            pressanykey();
            return FULLUPDATE;
        } else if (*yn != 'n') {
            vmsg("�]�w���ܧ�!");
            return FULLUPDATE;
        }
    }
    unlink(buf);
    vmsg("�����۰���H!");
    return FULLUPDATE;
}

int
toggle_showmail_mode(void)
{
    showmail_mode ++;
    showmail_mode %= SHOWMAIL_RANGE;
    return FULLUPDATE;
}

int
built_mail_index(void)
{
    char            genbuf[128];
    char command[1024];
    char homepath[PATHLEN];

    if (!HasUserPerm(PERM_BASIC))
        return DONOTHING;

    move(b_lines - 4, 0);
    outs("���\\��u�b�H�c�ɷ��l�ɨϥΡA" ANSI_COLOR(1;33) "�L�k" ANSI_RESET "�Ϧ^�Q�R�����H��C\n"
	 "���D�z�M���o�ӥ\\�઺�@�ΡA�_�h" ANSI_COLOR(1;33) "�Ф��n�ϥ�" ANSI_RESET "�C\n"
	 "ĵ�i�G���N���ϥαN�ɭP" ANSI_COLOR(1;33) "���i�w�������G" ANSI_RESET "�I\n");
    getdata(b_lines - 1, 0,
	    "�T�w���ثH�c?(y/N)", genbuf, 3,
	    LCECHO);
    if (genbuf[0] != 'y')
	return FULLUPDATE;

    sethomepath(homepath, cuser.userid);
    snprintf(command, sizeof(command), "bin/buildir '%s' > /dev/null", homepath);
    mvouts(b_lines - 1, 0, ANSI_COLOR(1;31) "�w�g�B�z����!! �Ѧh���K �q�Э��~" ANSI_RESET);
    system(command);
    pressanykey();
    return FULLUPDATE;
}

int
mail_muser(userec_t muser, const char *title, const char *filename)
{
    return mail_id(muser.userid, title, filename, cuser.userid);
}

int
chkmailbox(void)
{
    m_init();

    switch (chk_mailbox_limit()) {
	case MAILBOX_LIM_HARD:
	    bell();
	case MAILBOX_LIM_KEEP:
	    bell();
	    vmsgf("�z�O�s�H��ƥ� %d �W�X�W�� %d, �о�z", mailkeep, mailmaxkeep);
	    return mailkeep;

	default:
	    return 0;
    }
}

int
chkmailbox_hard_limit() {
    if (chk_mailbox_limit() == MAILBOX_LIM_HARD) {
        vmsgf("�z�O�s�H��ƥ� %d ���W�X�W�� %d, �о�z", mailkeep, mailmaxkeep);
        return 1;
    }
    return 0;
}

void
hold_mail(const char *fpath, const char *receiver, const char *title)
{
    char            buf[4];

    getdata(b_lines - 1, 0,
	    (HasUserFlag(UF_DEFBACKUP)) ?
	    "�w���Q�H�X�A�O�_�ۦs���Z(Y/N)�H[Y] " :
	    "�w���Q�H�X�A�O�_�ۦs���Z(Y/N)�H[N] ",
	    buf, sizeof(buf), LCECHO);

    if (TOBACKUP(buf[0]))
	do_hold_mail(fpath, receiver, cuser.userid, title);
}

int
do_send(const char *userid, const char *title, const char *log_source)
{
    fileheader_t    mhdr;
    char            fpath[STRLEN];
    int             internet_mail;
    userec_t        xuser;
    int ret	    = -1;
    char save_title[STRLEN];

    STATINC(STAT_DOSEND);
    if (strchr(userid, '@'))
	internet_mail = 1;
    else {
	internet_mail = 0;
	if (!getuser(userid, &xuser))
	    return -1;
	if (!(xuser.userlevel & PERM_READMAIL))
	    return -3;
    }
    /* process title */
    if (title)
	strlcpy(save_title, title, sizeof(save_title));
    else {
	char tmp_title[STRLEN-20];
	getdata(2, 0, "�D�D�G", tmp_title, sizeof(tmp_title), DOECHO);
	strlcpy(save_title, tmp_title, sizeof(save_title));
    }

    if (internet_mail) {

	setutmpmode(SMAIL);
	sethomepath(fpath, cuser.userid);
	stampfile(fpath, &mhdr);

	if (vedit2(fpath, NA, save_title,
		    EDITFLAG_ALLOWTITLE | EDITFLAG_KIND_SENDMAIL) == EDIT_ABORTED) {
	    unlink(fpath);
	    clear();
	    return -2;
	}
	clear();
	prints("�H��Y�N�H�� %s\n���D���G%s\n�T�w�n�H�X��? (Y/N) [Y]",
	       userid, save_title);
	switch (vkey()) {
	case 'N':
	case 'n':
	    outs("N\n�H��w����");
	    ret = -2;
	    break;
	default:
	    outs("Y\n�еy��, �H��ǻ���...\n");
	    ret = bsmtp(fpath, save_title, userid, NULL);
            LOG_IF(LOG_CONF_INTERNETMAIL,
                   log_filef("log/internet_mail.log", LOG_CREAT,
                             "%s [%s - %s] %s -> %s: %s\n",
                             Cdatelite(&now), log_source, __FUNCTION__,
                             cuser.userid, userid, save_title));
	    hold_mail(fpath, userid, save_title);
	    break;
	}
	unlink(fpath);

    } else {

	// XXX the title maybe changed inside do_innersend...
	ret = do_innersend(userid, fpath, save_title, save_title);
	if (ret == 0) // success
	    hold_mail(fpath, userid, save_title);

	clear();
    }
    return ret;
}

void
my_send(const char *uident)
{
    switch (do_send(uident, NULL, __FUNCTION__)) {
	case -1:
	outs(err_uid);
	break;
    case -2:
	outs(msg_cancel);
	break;
    case -3:
	prints("�ϥΪ� [%s] �L�k���H", uident);
	break;
    }
    pressanykey();
}

int
m_send(void)
{
    // in-site mail
    char uident[IDLEN+1];

    if (!HasBasicUserPerm(PERM_LOGINOK))
	return DONOTHING;

    vs_hdr("�����H�H");
    usercomplete(msg_uid, uident);
    showplans(uident);
    if (uident[0])
    {
	my_send(uident);
	return FULLUPDATE;
    }
    return DIRCHANGED;
}

/* �s�ձH�H�B�^�H : multi_send, (multi_)reply */
static void
multi_list(struct Vector *namelist, int *recipient)
{
    char            uid[16];
    char            genbuf[200];
    int             i, cRemoved;

    while (1) {
	vs_hdr("�s�ձH�H�W��");
	ShowVector(namelist, 3, 0, msg_cc, 0);
	move(1, 0);
	outs("(I)�ޤJ�n�� (O)�ޤJ�W�u�q�� (0-9)�ޤJ��L�S�O�W��");
	getdata(2, 0,
	       "(A)�W�[     (D)�R��         (M)�T�{�H�H�W��   (Q)���� �H[M]",
		genbuf, 4, LCECHO);
	switch (genbuf[0]) {
	case 'a':
	    while (1) {
		move(1, 0);
		usercomplete("�п�J�n�W�[���N��(�u�� ENTER �����s�W): ", uid);
		if (uid[0] == '\0')
		    break;

		move(2, 0);
		clrtoeol();

		if (!searchuser(uid, uid))
		    outs(err_uid);
		else if (Vector_search(namelist, uid) < 0) {
		    Vector_add(namelist, uid);
		    (*recipient)++;
		}
		ShowVector(namelist, 3, 0, msg_cc, 0);
	    }
	    break;
	case 'd':
	    while (*recipient) {
		move(1, 0);
		namecomplete2(namelist, "�п�J�n�R�����N��(�u�� ENTER �����R��): ", uid);
		if (uid[0] == '\0')
		    break;
		if (Vector_remove(namelist, uid))
		    (*recipient)--;
		ShowVector(namelist, 3, 0, msg_cc, 0);
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
	    ToggleVector(namelist, recipient, genbuf, msg_cc);
	    break;
	case 'o':
	    setuserfile(genbuf, FN_ALOHAED);
	    ToggleVector(namelist, recipient, genbuf, msg_cc);
	    break;
	case 'q':
	    *recipient = 0;
	    return;
	default:
            vs_hdr("�s�ձH�H�W��");
            outs("\n���b�ˬd�W��... \n"); doupdate();
            cRemoved = 0;
            for (i = 0; i < Vector_length(namelist); i++) {
                const char *p = Vector_get(namelist, i);
                if (searchuser(p, uid) &&
                    strcmp(STR_GUEST, uid) &&
                    !is_rejected(uid))
                    continue;
                // ok, bad guys exist.
                if (!cRemoved)
                    outs("�U�C ID �L�k����H��A�w�ۦW�沾���F"
                         "�Э��s�T�{�W��C\n\n");
                cRemoved ++;
                prints("%-.*s ", IDLEN, uid);
                Vector_remove(namelist, p);
                i--; // this is a hack. currently vector index is stable after remove.
                if ((cRemoved + 1) * (IDLEN + 1) >= t_columns)
                    outs("\n");
            }
            if (cRemoved) {
                pressanykey();
                continue;
            }
            return;
        }
    }
}

static void
multi_send(const char *title)
{
    FILE           *fp;
    fileheader_t    mymail;
    char            fpath[TTLEN], *ptr;
    int             recipient, listing;
    char            genbuf[PATHLEN];
    char	    buf[IDLEN+1];
    int             edflags = EDITFLAG_ALLOWTITLE;
    struct Vector   namelist;
    int             i;
    const char     *p;

    Vector_init(&namelist, IDLEN+1);
    listing = recipient = 0;
    if (*quote_file) {
	Vector_add(&namelist, quote_user);
	recipient = 1;
	fp = fopen(quote_file, "r");
	assert(fp);
	while (fgets(genbuf, sizeof(genbuf), fp)) {
	    if (strncmp(genbuf, "�� ", 3)) {
		if (listing)
		    break;
	    } else {
		if (listing) {
		    char *strtok_pos;
		    ptr = genbuf + 3;
		    for (ptr = strtok_r(ptr, " \n\r", &strtok_pos);
			    ptr;
			    ptr = strtok_r(NULL, " \n\r", &strtok_pos)) {
			if (searchuser(ptr, ptr) && Vector_search(&namelist, ptr) < 0 &&
			    strcmp(cuser.userid, ptr)) {
			    Vector_add(&namelist, ptr);
			    recipient++;
			}
		    }
		} else if (!strncmp(genbuf + 3, "[�q�i]", 6))
		    listing = 1;
	    }
	}
	fclose(fp);
	ShowVector(&namelist, 3, 0, msg_cc, 0);
        if (!listing) {
            vs_hdr2(" �s�զ^�H ", " �H��榡���~");
            outs("\n�`�N�A�ѩ�o�H�H������s��A"
                 "���ʫH��榡���~�A�w�򥢭�q�i�W���T�C\n"
                 "�A�u���ʫإߦ^�H�W��C");
            pressanykey();
        }
    }
    multi_list(&namelist, &recipient);
    move(1, 0);
    clrtobot();

    if (recipient) {
	char save_title[STRLEN];
	setutmpmode(SMAIL);
	if (title)
	    do_reply_title(2, title, str_reply, save_title, sizeof(save_title));
	else {
	    getdata(2, 0, "�D�D�G", fpath, sizeof(fpath), DOECHO);
	    snprintf(save_title, sizeof(save_title), "[�q�i] %s", fpath);
	}

	setuserfile(fpath, fn_notes);

        // TODO we should not let user edit this stupid list.
	if ((fp = fopen(fpath, "w"))) {
	    fprintf(fp, "�� [�q�i] �@ %d �H����", recipient);
	    listing = 80;

	    for (i = 0; i < Vector_length(&namelist); i++) {
		const char *p = Vector_get(&namelist, i);
		recipient = strlen(p) + 1;
		if (listing + recipient > 75) {
		    listing = recipient;
		    fprintf(fp, "\n��");
		} else
		    listing += recipient;

		fprintf(fp, " %s", p);
	    }
	    memset(genbuf, '-', 75);
	    genbuf[75] = '\0';
	    fprintf(fp, "\n%s\n\n", genbuf);
	    fclose(fp);
	}
        edflags |= EDITFLAG_KIND_MAILLIST;

	if (vedit(fpath, YEA, save_title) == EDIT_ABORTED) {
	    unlink(fpath);
	    Vector_delete(&namelist);
	    vmsg(msg_cancel);
	    return;
	}

	for (i = 0; i < Vector_length(&namelist); i++) {
	    p = Vector_get(&namelist, i);
            searchuser(p, buf);
            sethomepath(genbuf, buf);
	    stampfile(genbuf, &mymail);
	    unlink(genbuf);
	    Copy(fpath, genbuf);

	    strlcpy(mymail.owner, cuser.userid, sizeof(mymail.owner));
	    strlcpy(mymail.title, save_title, sizeof(mymail.title));
	    mymail.filemode |= FILE_MULTI;	/* multi-send flag */
	    sethomedir(genbuf, buf);
	    if (append_record_forward(genbuf, &mymail, sizeof(mymail), buf) == -1)
		vmsg(err_uid);
	    sendalert(buf, ALERT_NEW_MAIL);
	}
	hold_mail(fpath, NULL, save_title);
	unlink(fpath);
	Vector_delete(&namelist);
    } else {
	Vector_delete(&namelist);
	vmsg(msg_cancel);
    }
}

static int
mailbox_reply(int ent, fileheader_t * fhdr, const char *direct)
{
    int use_multi = 0;

    // do not allow guest to use this
    if (!HasUserPerm(PERM_BASIC))
	return DONOTHING;

    if (!fhdr || !fhdr->filename[0])
	return DONOTHING;

    // Use group-reply or single-reply?
    if (fhdr->filemode & FILE_MULTI) {
        char ans[3] = "";
        vs_hdr("�s�իH��");
        do {
            getdata(2, 0, "�аݭn�^�H����o�H��(r)"
                    "�٬O�������H(a)�A�����}(q) [r/a/q]? ",
                    ans, sizeof(ans), LCECHO);
        } while (ans[0] != 'r' && ans[0] != 'a' && ans[0] != 'q');

        switch(ans[0]) {
            case 'r':
                use_multi = 0;
                break;
            case 'a':
                use_multi = 1;
                break;
            default:
                return FULLUPDATE;
        }
    }

    if (!use_multi)
	return mail_reply(ent, fhdr, direct);

    setuserfile(quote_file, fhdr->filename);
    if (!dashf(quote_file)) {
	vmsg("��H��w�����C");
    } else {
        strlcpy(quote_user, fhdr->owner, sizeof(quote_user));
        multi_send(fhdr->title);
    }
    *quote_user = *quote_file = 0;
    return FULLUPDATE;
}

int
mail_list(void)
{
    if (!HasBasicUserPerm(PERM_LOGINOK) ||
        HasUserPerm(PERM_VIOLATELAW))
        return DONOTHING;

    vs_hdr("�s�է@�~");
    multi_send(NULL);
    return 0;
}

int
mail_all(void)
{
    FILE           *fp;
    fileheader_t    mymail;
    char            fpath[TTLEN];
    char            genbuf[200];
    int             i, unum;
    char           *userid;
    int             edflags;
    char save_title[STRLEN];

    vs_hdr("���Ҧ��ϥΪ̪��t�γq�i");
    setutmpmode(SMAIL);
    getdata(2, 0, "�D�D�G", fpath, sizeof(fpath), DOECHO);
    snprintf(save_title, sizeof(save_title),
	     "[�t�γq�i]" ANSI_COLOR(1;32) " %s" ANSI_RESET, fpath);

    setuserfile(fpath, fn_notes);

    if ((fp = fopen(fpath, "w"))) {
	fprintf(fp, "�� [" ANSI_COLOR(1) "�t�γq�i" ANSI_RESET "] �o�O�ʵ��Ҧ��ϥΪ̪��H\n");
	fprintf(fp, "-----------------------------------------------------"
		"----------------------\n");
	fclose(fp);
    }
    *quote_file = 0;

    edflags = EDITFLAG_ALLOWTITLE | EDITFLAG_KIND_SENDMAIL;
    if (vedit2(fpath, YEA, save_title, edflags) == EDIT_ABORTED) {
	unlink(fpath);
	outs(msg_cancel);
	pressanykey();
	return 0;
    }

    setutmpmode(MAILALL);
    vs_hdr("�H�H��...");

    sethomepath(genbuf, cuser.userid);
    stampfile(genbuf, &mymail);
    unlink(genbuf);
    Copy(fpath, genbuf);
    unlink(fpath);
    strcpy(fpath, genbuf);

    strlcpy(mymail.owner, cuser.userid, sizeof(mymail.owner));	/* ���� ID */
    strlcpy(mymail.title, save_title, sizeof(mymail.title));

    sethomedir(genbuf, cuser.userid);
    if (append_record_forward(genbuf, &mymail, sizeof(mymail), cuser.userid) == -1)
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
	    Copy(fpath, genbuf);

	    strlcpy(mymail.owner, cuser.userid, sizeof(mymail.owner));
	    strlcpy(mymail.title, save_title, sizeof(mymail.title));
	    /* mymail.filemode |= FILE_MARKED; Ptt ���i�令���|mark */
	    sethomedir(genbuf, userid);
	    if (append_record_forward(genbuf, &mymail, sizeof(mymail), userid) == -1)
		outs(err_uid);
	    vmsgf("%*s %5d / %5d", IDLEN + 1, userid, i + 1, unum);
	}
    }
    return 0;
}

int
get_account_sysop(struct Vector *namelist)
{
    FILE *fp;
    if ( (fp = fopen(FN_MAIL_ACCOUNT_SYSOP, "r")) != NULL) {
        char line[STRLEN+1];
        while(fgets(line, sizeof(line), fp)) {
            char userid[IDLEN+1];

            chomp(line);
            if (is_validuserid(line) && searchuser(line, userid))
                Vector_add(namelist, line);
        }
        fclose(fp);
        return 0;
    } else
        return -1;
}

static int
show_mail_account_sysop_desc(void)
{
    if (more(FN_MAIL_ACCOUNT_SYSOP_DESC, YEA) < 0)
	return 0;

    char yn[2];
    getdata(b_lines, 0, "�аݱz�O�_�\\Ū�H�W����? [y/N]: ", yn, sizeof(yn),
	    LCECHO);
    if (yn[0] == 'y')
	return 0;
    return -1;
}

int
mail_account_sysop(void)
{
    // send mail to account sysop
    int             i;
    fileheader_t    mymail;
    char            fpath[TTLEN];
    char            genbuf[PATHLEN];
    char            idbuf[IDLEN+1];
    int             oldstat = currstat;
    char            save_title[STRLEN];
    char            tmp_title[STRLEN-20];
    struct          Vector namelist;

    Vector_init(&namelist, IDLEN+1);

    if (get_account_sysop(&namelist) == -1)
    {
        vmsg("Ū������H�W�楢�ѡA�ЦV " BN_BUGREPORT " �^��");
        Vector_delete(&namelist);
        return DIRCHANGED;
    }

    if (Vector_length(&namelist) == 0)
    {
        vmsg("�L���Ħ���H�W��A�ЦV " BN_BUGREPORT " �^��");
        Vector_delete(&namelist);
        return DIRCHANGED;
    }

    if (show_mail_account_sysop_desc() < 0)
	return DIRCHANGED;
    clear();
    vs_hdr("�H�H���b������");
    getdata(2, 0, "�D�D:", tmp_title, sizeof(tmp_title), DOECHO);
    strlcpy(save_title, tmp_title, sizeof(save_title));
    if (!save_title[0])
    {
	vmsg("�����C");
	return DIRCHANGED;
    }

    setutmpmode(SMAIL);

    setuserfile(fpath, fn_notes);

    if (vedit2(fpath, YEA, save_title,
                EDITFLAG_ALLOWTITLE | EDITFLAG_KIND_SENDMAIL) == EDIT_ABORTED)
    {
        unlink(fpath);
        setutmpmode(oldstat);
        vmsg(msg_cancel);
        Vector_delete(&namelist);
        return DIRCHANGED;
    }

    for (i = 0; i < Vector_length(&namelist); i++) {
        const char *userid = Vector_get(&namelist, i);
        if(!searchuser(userid, idbuf))
            continue;
        sethomepath(genbuf, idbuf);
        stampfile(genbuf, &mymail);
        unlink(genbuf);
        Copy(fpath, genbuf);

        strlcpy(mymail.owner, cuser.userid, sizeof(mymail.owner));
        strlcpy(mymail.title, save_title, sizeof(mymail.title));
        mymail.filemode |= FILE_MULTI;
        sethomedir(genbuf, idbuf);

        if (append_record_forward(genbuf, &mymail, sizeof(mymail), idbuf) == -1)
            vmsg(err_uid);
        sendalert(idbuf, ALERT_NEW_MAIL);
    }
    Vector_delete(&namelist);

    hold_mail(fpath, NULL, save_title);
    unlink(fpath);

    setutmpmode(oldstat);

    pressanykey();
    return FULLUPDATE;
}


int
mail_mbox(void)
{
    char tagname[PATHLEN];
    char cmd[PATHLEN*2];
    fileheader_t fhdr;
    time4_t last_tag = 0;

    setuserfile(tagname, ".zipped_home");
    last_tag = dasht(tagname);

    if (last_tag > 0 && (now - last_tag) < 7 * DAY_SECONDS &&
        !HasUserPerm(PERM_SYSOP)) {
        vmsgf("�C�g�ȥi�ƥ��@���A���U���٦� %d �ѡC",
              7 - (now - last_tag) / DAY_SECONDS);
        return 0;
    }

    snprintf(cmd, sizeof(cmd), "/tmp/%s.uu", cuser.userid);
    snprintf(fhdr.title, sizeof(fhdr.title), "%s �p�H���", cuser.userid);
    // TODO doforward does not return real execution code... we may need to
    // fix it in future.
    if (doforward(cmd, &fhdr, 'Z') == 0) {
        log_filef(tagname, LOG_CREAT,
                  "%s %s\n", Cdatelite(&now), cmd);
        rotate_text_logfile(tagname, SZ_RECENTLOGIN, 0.2);
    }
    return 0;
}

static int
m_internet_forward(int ent GCC_UNUSED, fileheader_t * fhdr, const char *direct)
{
    if (!HasUserPerm(PERM_FORWARD))
        return DONOTHING;
    forward_file(fhdr, direct);
    return FULLUPDATE;
}

static int
m_forward(int ent GCC_UNUSED, fileheader_t * fhdr, const char *direct GCC_UNUSED)
{
    char            uid[STRLEN];
    char save_title[STRLEN];

    if (!HasBasicUserPerm(PERM_LOGINOK))
	return DONOTHING;

    vs_hdr("��F�H��");
    usercomplete(msg_uid, uid);
    if (uid[0] == '\0')
	return FULLUPDATE;

    strlcpy(quote_user, fhdr->owner, sizeof(quote_user));
    setuserfile(quote_file, fhdr->filename);
    snprintf(save_title, sizeof(save_title), "%.64s (fwd)", fhdr->title);
    move(1, 0);
    clrtobot();
    prints("��H��: %s\n��  �D: %s\n", uid, save_title);
    showplans(uid);

    switch (do_send(uid, save_title, __FUNCTION__)) {
    case -1:
	outs(err_uid);
	break;
    case -2:
	outs(msg_cancel);
	break;
    case -3:
	prints("�ϥΪ� [%s] �L�k���H", uid);
	break;
    }
    pressanykey();
    quote_user[0]='\0';
    quote_file[0]='\0';
    if (strcasecmp(uid, cuser.userid) == 0)
	return DIRCHANGED;
    return FULLUPDATE;
}

int
doforward(const char *direct, const fileheader_t * fh, int mode)
{
    static char     address[STRLEN] = "";
    char            fname[PATHLEN];
    char            genbuf[PATHLEN];
    int             return_no;
    const char      *hostaddr;

    if (!address[0] && strcasecmp(cuser.email, "x") != 0)
     	strlcpy(address, cuser.email, sizeof(address));

    if( mode == 'U' ){
	if ('y' != tolower(vmsg("�Ы� y ���� uuencode�C"
                                "�Y�z���M������O uuencode �Ч�� F ��H�C")))
            return 1;
    }
    trim(address);

    // if user has address and not the default 'x' (no-email)...
    if (address[0]) {
	snprintf(genbuf, sizeof(genbuf),
		 "�T�w��H�� [%s] ��(Y/N/Q)�H[Y] ", address);
	getdata(b_lines, 0, genbuf, fname, 3, LCECHO);

	if (fname[0] == 'q') {
	    outmsg("������H");
	    return 1;
	}
	if (fname[0] == 'n')
	    address[0] = '\0';
    }
    if (!address[0]) {
	do {
	    getdata(b_lines - 1, 0, "�п�J��H�a�}�G", fname, 60, DOECHO);
	    if (fname[0]) {
		if (strchr(fname, '.'))
		    strlcpy(address, fname, sizeof(address));
		else
		    snprintf(address, sizeof(address),
                    	"%s.bbs@%s", fname, MYHOSTNAME);
	    } else {
		vmsg("������H");
		return 1;
	    }
	} while (mode == 'Z' && strstr(address, MYHOSTNAME));
    }
    if (mode == 'Z') {
        if (strstr(address, MYHOSTNAME) ||
            strstr(address, ".brd@")  ||
            strstr(address, "@bs2.to")  ||
            strstr(address, ".bbs@")) {
            vmsg("���i�ϥ� BBS �H�c�C");
            return 1;
        }
    }
    // XXX bug: if user has already provided mail address...
    /* according to our experiment, many users leave blanks */
    trim(address);
    if (invalidaddr(address))
	return -2;

    // decide if address is internet mail
    hostaddr = strchr(address, '@');
    if (hostaddr && strcasecmp(hostaddr + 1, MYHOSTNAME) == 0)
        hostaddr = NULL;

    outmsg("��H���еy��...");
    refresh();

    /* �l�ܨϥΪ� */
    if (HasUserPerm(PERM_LOGUSER))
	log_user("mailforward to %s ",address);

    // �B�z�����¦W��
    do {
	char xid[IDLEN+1], *dot;

	strlcpy(xid, address, sizeof(xid));
	dot = strchr(xid, '.');
	if (dot) *dot = 0;
	dot = strcasestr(address, ".bbs@");

	if (dot) {
	    if (strcasecmp(strchr(dot, '@')+1, MYHOSTNAME) != 0)
		break;
	} else {
	    // accept only local name
	    if (strchr(address, '@'))
		break;
	}

	// now the xid holds local name
	if (!is_validuserid(xid) ||
	    searchuser(xid, xid) <= 0)
	{
	    vmsg("�䤣�즹�ϥΪ� ID�C");
	    return 1;
	}

	// some stupid users set self as rejects.
	if (strcasecmp(xid, cuser.userid) == 0)
	    break;

        if (is_rejected(xid)) {
            // We used to simply ignore here, and then people (especially those
            // in BuyTogether and similiars) say "my mail is lost".
            // After notifying SYSOPs, we believe it will be better to let all
            // users know what's going on.
            vmsg("�L�k�H�H�����ϥΪ�");
	    return 1;
        }
    } while (0);

    // PATHLEN will be used as command line, so we need it longer.
    assert(PATHLEN >= 256);
    if (mode == 'Z') {
	assert(is_validuserid(cuser.userid));
	assert(is_valid_email(address));
#ifdef MUTT_PATH
	snprintf(fname, sizeof(fname),
		 TAR_PATH " -X " BBSHOME "/etc/ziphome.exclude "
                 "-czf /tmp/home.%s.tgz home/%c/%s; "
		 MUTT_PATH " -s 'home.%s.tgz' "
                 "-a /tmp/home.%s.tgz -- '%s' </dev/null;"
		 "rm /tmp/home.%s.tgz",
		 cuser.userid, cuser.userid[0], cuser.userid,
		 cuser.userid, cuser.userid, address, cuser.userid);
	system(fname);
	return 0;
#else
	snprintf(fname, sizeof(fname),
		 TAR_PATH " -X " BBSHOME "/etc/ziphome.exclude "
                 "-czf - home/%c/%s | "
		"/usr/bin/uuencode %s.tgz > %s",
		cuser.userid[0], cuser.userid, cuser.userid, direct);
	system(fname);
	strlcpy(fname, direct, sizeof(fname));
#endif
    } else if (mode == 'U') {
	char            tmp_buf[PATHLEN];

	snprintf(fname, sizeof(fname), "/tmp/bbs.uu%05d", (int)currpid);
	snprintf(tmp_buf, sizeof(tmp_buf),
		 "/usr/bin/uuencode %s/%s uu.%05d > %s",
		 direct, fh->filename, (int)currpid, fname);
	system(tmp_buf);
    } else if (mode == 'F') {
	char            tmp_buf[PATHLEN];

	snprintf(fname, sizeof(fname), "/tmp/bbs.f%05d", (int)currpid);
	snprintf(tmp_buf, sizeof(tmp_buf), "%s/%s", direct, fh->filename);
        if (!dashf(tmp_buf)) {
            unlink(fname);
            return -1;
        }
        // TODO append article reference, if available.
	Copy(tmp_buf, fname);
    } else
	return -1;

    if (hostaddr) {
        LOG_IF(LOG_CONF_INTERNETMAIL,
               log_filef("log/internet_mail.log", LOG_CREAT,
                         "%s [%s] %s -> %s: %s - %s\n",
                         Cdatelite(&now), __FUNCTION__,
                         cuser.userid, address, direct, fh->title));
    }

    return_no = bsmtp(fname, fh->title, address, NULL);
    unlink(fname);
    return (return_no);
}

static void
mailtitle(void)
{
    char buf[STRLEN];

    if (mailmaxkeep)
    {
	snprintf(buf, sizeof(buf), ANSI_COLOR(32) "(�e�q:%d/%d�g)", mailkeep, mailmaxkeep);
    } else {
	snprintf(buf, sizeof(buf), ANSI_COLOR(32) "(�j�p:%d�g)", mailkeep);
    }

    showtitle("�l����", BBSName);
    prints("[��]���}[����]���[��]�\\Ū�H�� [O]���~�H:%s [h]�D�U %s\n" ,
	    REJECT_OUTTAMAIL(cuser) ? ANSI_COLOR(31) "��" ANSI_RESET : "�}",
#ifdef USE_TIME_CAPSULE
            "[~]" RECYCLE_BIN_NAME
#else
            ""
#endif
            );
    vbarf(ANSI_REVERSE "  �s��   �� �� �@ ��          �H  ��  ��  �D\t%s ", buf);
}

static void
maildoent(int num, fileheader_t * ent)
{
    const char *title, *mark, *color = NULL;
    char isonline = 0, datepart[6], type = ' ';
    int title_type = SUBJECT_NORMAL;

    if (ent->filemode & FILE_MARKED)
    {
	type = (ent->filemode & FILE_READ) ?
	    'm' : 'M';
    }
    else if (ent->filemode & FILE_REPLIED)
    {
	type = (ent->filemode & FILE_READ) ?
	    'r' : 'R';
    }
    else
    {
	type = (ent->filemode & FILE_READ) ?
	    ' ' : '+';
    }

    if (FindTaggedItem(ent))
	type = 'D';

    title = subject_ex(ent->title, &title_type);
    switch (title_type) {
        case SUBJECT_FORWARD:
            mark = "��";
            color = ANSI_COLOR(1;36);
            break;
        case SUBJECT_REPLY:
            mark = "R:";
            color = ANSI_COLOR(1;33);
            break;
        default:
        case SUBJECT_NORMAL:
            mark = "��";
            color = ANSI_COLOR(1;31);
            break;
    }

    strlcpy(datepart, ent->date, sizeof(datepart));
    isonline = query_online(ent->owner);

    /* print out */
    if (strncmp(currtitle, title, TTLEN) != 0)
    {
	/* is title. */
	color = "";
    }

    prints("%6d %c %-6s%s%-15.14s%s%s %s%-*.*s%s\n",
	    num, type, datepart,
	    isonline ? ANSI_COLOR(1) : "",
	    ent->owner,
	    isonline ? ANSI_RESET : "",
	    mark, color,
	    t_columns - 34, t_columns - 34,
	    title,
	    *color ? ANSI_RESET : "");
}


static int
mail_del(int ent, const fileheader_t * fhdr, const char *direct)
{
    if (fhdr->filemode & FILE_MARKED)
	return DONOTHING;

    if (currmode & MODE_SELECT) {
        vmsg("�Х��^�쥿�`�Ҧ���A�i��R��...");
        return READ_REDRAW;
    }

    if (vans(msg_del_ny) == 'y') {
	if (!delete_fileheader(direct, fhdr, ent)) {
            int del_ret = 0;
            setupmailusage();
            del_ret = delete_file_content(direct, fhdr, direct, NULL, 0);
	    loadmailusage();
            if (del_ret == DELETE_FILE_CONTENT_BACKUP_FAILED)
                vmsg("�H��w�R�������|�i�J" RECYCLE_BIN_NAME);
#ifdef DEBUG
            else if (!IS_DELETE_FILE_CONTENT_OK(del_ret))
                vmsg("�ɮפw���s�b�A�Ц�" BN_BUGREPORT "���i�A����");
#endif
	    return DIRCHANGED;
	} else {
            vmsg("�R�����ѡA�нT�w���h���n�J��A���աC");
            return DIRCHANGED;
        }
    }
    return READ_REDRAW;
}

int b_call_in(int ent, const fileheader_t * fhdr, const char *direct);

static int
mail_read(int ent, fileheader_t * fhdr, const char *direct)
{
    char buf[PATHLEN];
    int done;

    // in current design, mail_read is ok for single entry.
    setdirpath(buf, direct, fhdr->filename);
    strlcpy(currtitle, subject(fhdr->title), sizeof(currtitle));

    /* whether success or not, update flag.
     * or users may bug about "black-hole" mails
     * and blinking notification */
    if( !(fhdr->filemode & FILE_READ))
    {
	fhdr->filemode |= FILE_READ;
	substitute_ref_record(direct, fhdr, ent);
    }
    done = more(buf, YEA);
    // quick control
    switch (done) {
	case -1:
	    /* no such file */
	    clear();
	    vmsg("���ʫH�L���e�C");
	    return FULLUPDATE;
	case RET_DOREPLY:
	case RET_DOREPLYALL:
	    mailbox_reply(ent, fhdr, direct);
	    return FULLUPDATE;
    }
    return done;
}

static int
mail_read_all(int ent GCC_UNUSED, fileheader_t * fhdr GCC_UNUSED,
              const char *direct GCC_UNUSED)
{
    off_t   i = 0, num = 0;
    int	    fd = 0;
    fileheader_t xfhdr;

    currutmp->alerts &= ~ALERT_NEW_MAIL;
    if ((fd = open(currmaildir, O_RDWR)) < 0)
	return DONOTHING;

    if ((num = lseek(fd, 0, SEEK_END)) < 0)
	num = 0;
    num /= sizeof(fileheader_t);

    i = num - NEWMAIL_CHECK_RANGE;
    if (i < 0) i = 0;

    if (lseek(fd, i * (off_t)sizeof(fileheader_t), SEEK_SET) < 0)
	i = num;

    for (; i < num; i++)
    {
	if (read(fd, &xfhdr, sizeof(xfhdr)) <= 0)
	    break;
	if (xfhdr.filemode & FILE_READ)
	    continue;
	xfhdr.filemode |= FILE_READ;
	if (lseek(fd, i * (off_t)sizeof(fileheader_t), SEEK_SET) < 0)
	    break;
	write(fd, &xfhdr, sizeof(xfhdr));
    }

    close(fd);
    return DIRCHANGED;
}

/* in boards/mail �^�H����@�̡A��H����i */
int
mail_reply(int ent, fileheader_t * fhdr, const char *direct)
{
    char            uid[STRLEN];
    FILE           *fp;
    char            genbuf[512];
    int		    oent = ent;
    char save_title[STRLEN];

    // do not allow guest to use this
    if (!HasUserPerm(PERM_BASIC))
	return DONOTHING;

    if (!fhdr || !fhdr->filename[0])
	return DONOTHING;

    if (fhdr->owner[0] == '[' || fhdr->owner[0] == '-'
	|| (fhdr->filemode & FILE_ANONYMOUS))
    {
	// system mail. reject.
	vmsg("�L�k�^�H�C");
	return FULLUPDATE;
    }

    vs_hdr("�^  �H");

    /* Trick: mail �^�H�� ent/direct=0, currstat = RMAIL. */
    if (currstat == RMAIL)
	setuserfile(quote_file, fhdr->filename);
    else
	setbfile(quote_file, currboard, fhdr->filename);

    /* find the author */
    strlcpy(quote_user, fhdr->owner, sizeof(quote_user));
    if (strchr(quote_user, '.')) {
	char           *t;
	char *strtok_pos;
	genbuf[0] = '\0';
	if ((fp = fopen(quote_file, "r"))) {
	    fgets(genbuf, sizeof(genbuf), fp);
	    fclose(fp);
	}
	t = strtok_r(genbuf, str_space, &strtok_pos);
	if (t && (strcmp(t, str_author1)==0 || strcmp(t, str_author2)==0)
		&& (t=strtok_r(NULL, str_space, &strtok_pos)) != NULL)
	    strlcpy(uid, t, sizeof(uid));
	else {
	    vmsg("���~: �䤣��@�̡C");
	    quote_user[0]='\0';
	    quote_file[0]='\0';
	    return FULLUPDATE;
	}
    } else {
        int abort = 0;
	strlcpy(uid, quote_user, sizeof(uid));
        if (searchuser(uid, NULL) <= 0) {
            mvouts(2, 0, "�w�䤣�즹�ϥΪ̡C\n");
            abort = 1;
        } else if (!is_file_owner_id(fhdr, uid)) {
            char ans[3];
            mvouts(2, 0, ANSI_COLOR(1;31)
                   "ĵ�i: ���g�峹����@�̱b���w�Q���s���U�A"
                   "�z���H�i��Q�L�����s�ϥΪ̦���\n" ANSI_RESET);
            outs(  "      �ѩ� BBS ���b���L����i�Q���s���U�A"
                   "�ҥH���ɦ��H�H�i�ভ�w���H�F�A\n"
                   "      ���]���i��O�P�@�H�ѤF�K�X�᭫���C\n");
            outs(  "      ���קK�z���p�߱H�X�Ӹ�άO�p�H�T���A"
                   "��ĳ�z���A���T�w��設����A�H�H�C\n");
            getdata(7, 0, "�T�w���M�n�H�H������[y/N]? ", ans, sizeof(ans),
                    LCECHO);
            if (ans[0] != 'y') {
                abort = 1;
            } else {
                move(3, 0); clrtobot();
            }
        }
        if (abort) {
            quote_user[0]='\0';
            quote_file[0]='\0';
            vmsg("���H�H�C");
            return FULLUPDATE;
        }
    }

    /* make the title */
    do_reply_title(3, fhdr->title, str_reply, save_title, sizeof(save_title));
    prints("\n���H�H: %s\n��  �D: %s\n", uid, save_title);

    /* edit, then send the mail */
    switch (do_send(uid, save_title, __FUNCTION__)) {
    case -1:
	outs(err_uid);
	break;
    case -2:
	outs(msg_cancel);
	break;
    case -3:
	prints("�ϥΪ� [%s] �L�k���H", uid);
	break;

    case 0:
	/* success */
	if (direct &&	/* for board, no direct */
            !(fhdr->filemode & FILE_REPLIED))
	{
	    fhdr->filemode |= FILE_REPLIED;
	    substitute_fileheader(direct, fhdr, fhdr, oent);
	}
	break;
    }
    pressanykey();
    quote_user[0]='\0';
    quote_file[0]='\0';
    if (strcasecmp(uid, cuser.userid) == 0)
	return DIRCHANGED;
    return FULLUPDATE;
}

static int
mail_nooutmail(int ent GCC_UNUSED, fileheader_t * fhdr GCC_UNUSED,
               const char *direct GCC_UNUSED)
{
    if (!HasBasicUserPerm(PERM_LOGINOK))
        return DONOTHING;

    pwcuToggleOutMail();
    return FULLUPDATE;
}

static int
mail_mark(int ent, fileheader_t * fhdr, const char *direct)
{
    // If user does not have proper permission, he can only unmark iems.
    if (HasBasicUserPerm(PERM_LOGINOK))
        fhdr->filemode ^= FILE_MARKED;
    else
        fhdr->filemode &= (~FILE_MARKED);

    substitute_ref_record(direct, fhdr, ent);
    return PART_REDRAW;
}

/* help for mail reading */

static const char *hlp_mailmove[] = {
    "�i���ʴ�Сj", NULL,
    "  �U�ʶl��", "�� n j ",
    "  �W�ʶl��", "�� p k ",
    "  ����½��", "^F N PgDn �ť���",
    "  ���e½��", "^B P PgUp",
    "  �Ĥ@�ʫH", "Home",
    "  �̫�@��", "End  $",
    "  ����...",  "0-9�Ʀr��",
    "  �j�M���D", "/",
    "  �������}", "�� q e",
    NULL,
}, *hlp_mailbasic[] = {
    "�i�򥻾ާ@�j", NULL,
    "  Ū�H",	  "�� r",
    "  �^�H",	  "R y",
    "  �R�����H", "d",
    "  �H�o�s�H", "^P",
    "", "",
    "�i��H�P����j", NULL,
    "  ������H", "x",
    "  ���~��H", "F",
    "  ����ݪO", "^X",
    NULL,
}, *hlp_mailadv[] = {
    "�i�i�����O�j", NULL,
    "  ���w�d���H", "D",
    "  �аO���n�H��", "m (�קK�~�R)",
    "  �аO�ݧR�H��", "t",
    "  �屼�ݧR�H��", "^D",
    "  ��z���y��H�^", "u",
    "  ���ثH�c",     "^G (���l�ɤ~��)",
#ifdef USE_TIME_CAPSULE
    "  " RECYCLE_BIN_NAME,   "~",
#endif
    NULL,
}, *hlp_mailconf[] = {
    "�i�]�w�j", NULL,
    "  �O�_�������~�H", "O",
    NULL,
}, *hlp_mailempty[] = {
    "", "",
    NULL,
}, *hlp_mailman[] = {
    "�i�p�H�H�󧨡j", NULL,
    "  �s���p�H�H��", "z",
    "  ���J�p�H�H��", "c",
     NULL,
};

static int
m_help(void)
{
    const char ** p1[3] = { hlp_mailmove, hlp_mailbasic, hlp_mailconf },
	       ** p2[3] = { hlp_mailadv,  hlp_mailempty, hlp_mailman };
    const int  cols[3] = { 31, 22, 24 },    // column width
               desc[3] = { 12, 14, 18 };    // desc width
    const int  cols2[3]= { 36, 17, 24 },    // columns width
               desc2[3]= { 18, 14, 18 };    // desc width
    clear();
    showtitle("�q�l�H�c", "�ϥλ���");
    outs("\n");
    vs_multi_T_table_simple(p1, 3, cols, desc,
	    HLP_CATEGORY_COLOR, HLP_DESCRIPTION_COLOR, HLP_KEYLIST_COLOR);
    vs_multi_T_table_simple(p2, HasUserPerm(PERM_MAILLIMIT)?3:2, cols2, desc2,
	    HLP_CATEGORY_COLOR, HLP_DESCRIPTION_COLOR, HLP_KEYLIST_COLOR);
    PRESSANYKEY();
    return FULLUPDATE;
}

static int
mail_cross_post(int unused_arg GCC_UNUSED, fileheader_t * fhdr,
                const char *direct GCC_UNUSED)
{
    char            xboard[20], fname[80], xfpath[80], xtitle[80];
    fileheader_t    xfile;
    FILE           *xptr;
    char            genbuf[200];
    int		    xbid;

    if (!HasBasicUserPerm(PERM_LOGINOK))
        return DONOTHING;

    // XXX TODO ���קK�H�k�ϥΪ̤j�q��ӶD�O���A���w�C���o��q�C
    if (HasUserPerm(PERM_VIOLATELAW))
    {
	static int violatecp = 0;
	if (violatecp++ >= MAX_CROSSNUM)
	    return DONOTHING;
    }

    move(2, 0);
    clrtoeol();

    if (1)
    {
	outs(ANSI_COLOR(1;31)
	"�Ъ`�N: �Y�L�q��������N�����~�O�A�ɭP�Q�}�@�氱�v�C\n" ANSI_RESET
	"�Y���S�O�ݨD�Ь��U�O�D�A�ХL�̡u���A���v(���O�ۤv��)�C\n\n");
    }

    move(1, 0);
    CompleteBoard("������峹��ݪO�G", xboard);

    if (*xboard == '\0' || !haspostperm(xboard))
    {
	vmsg("�L�k���");
	return FULLUPDATE;
    }

    xbid = getbnum(xboard);
    assert(0<=xbid-1 && xbid-1<MAX_BOARD);

    if (!CheckPostRestriction(xbid))
    {
	vmsg("���F�ӬݪO�o����󭭨� (�i�b�ӬݪO���� i �d�ݭ���)");
	return FULLUPDATE;
    }

#ifdef USE_COOLDOWN
   if(check_cooldown(&bcache[xbid - 1]))
       return READ_REDRAW;
#endif

    do_reply_title(2, fhdr->title, str_forward, xtitle, sizeof(xtitle));
    strip_ansi(xtitle, xtitle, STRIP_ALL);

    getdata(2, 0, "(S/L)�o�� (Q)�����H[Q] ", genbuf, 3, LCECHO);
    if (genbuf[0] == 'l' || genbuf[0] == 's') {
	int             currmode0 = currmode;

	currmode = 0;
	setbpath(xfpath, xboard);
	stampfile(xfpath, &xfile);
        strlcpy(xfile.owner, cuser.userid, sizeof(xfile.owner));
	strlcpy(xfile.title, xtitle, sizeof(xfile.title));
	if (genbuf[0] == 'l') {
	    xfile.filemode = FILE_LOCAL;
	}
	setuserfile(fname, fhdr->filename);
	{
	    const char *save_currboard;
	    xptr = fopen(xfpath, "w");
	    assert(xptr);

	    save_currboard = currboard;
	    currboard = xboard;
	    write_header(xptr, xfile.title);
	    currboard = save_currboard;

	    fprintf(xptr, "�� [��������� %s �H�c]\n\n", cuser.userid);

	    b_suckinfile(xptr, fname);
            addforwardsignature(xptr, NULL);
	    fclose(xptr);
	}

	setbdir(fname, xboard);
	append_record(fname, &xfile, sizeof(xfile));
	setbtotal(getbnum(xboard));

#ifdef USE_COOLDOWN
	if (bcache[getbnum(xboard) - 1].brdattr & BRD_COOLDOWN)
	    add_cooldowntime(usernum, 5);
	add_posttimes(usernum, 1);
#endif
        log_crosspost_in_allpost(xboard, &xfile);

	// cross-post does not add numpost.
	outs("����H�󤣼W�[�峹�ơA�q�Х]�[�C");

	vmsg("�峹�������");
	currmode = currmode0;
    }
    return FULLUPDATE;
}

int
mail_man(void)
{
    char            buf[PATHLEN], title[64], backup_path[PATHLEN];
    int             mode0 = currutmp->mode;
    int             stat0 = currstat;

    // TODO if someday we put things in user man...?
    sethomeman(buf, cuser.userid);
    sethomedir(backup_path, cuser.userid);

    // if user already has man directory or permission,
    // allow entering mail-man folder.

    if (!dashd(buf) && !HasUserPerm(PERM_MAILLIMIT))
	return DONOTHING;

    snprintf(title, sizeof(title), "%s ���H��", cuser.userid);
    a_menu(title, buf, HasUserPerm(PERM_MAILLIMIT) ? 1 : 0, 0, NULL, backup_path);
    currutmp->mode = mode0;
    currstat = stat0;
    return FULLUPDATE;
}

// XXX BUG mail_cite ���i��|���i a_menu, �� a_menu �| check
// currbid�C �@����V�|���޿���~...
static int
mail_cite(int ent GCC_UNUSED, fileheader_t * fhdr, const char *direct GCC_UNUSED)
{
    char            fpath[PATHLEN];
    char            title[TTLEN + 1];
    static char     xboard[20] = "";
    char            buf[20];
    int             bid;

    if (!HasUserPerm(PERM_BASIC))
        return DONOTHING;

    setuserfile(fpath, fhdr->filename);
    strlcpy(title, "�� ", sizeof(title));
    strlcpy(title + 3, fhdr->title, sizeof(title) - 3);
    a_copyitem(fpath, title, 0, 1);

    if (cuser.userlevel >= PERM_BM) {
	move(2, 0);
	clrtoeol();
	move(3, 0);
	clrtoeol();
	move(1, 0);

	CompleteBoard(
		HasUserPerm(PERM_MAILLIMIT) ?
		"��J�ݪO�W�� (����Enter�i�J�p�H�H��)�G" :
		"��J�ݪO�W�١G",
		buf);
	if (*buf)
	    strlcpy(xboard, buf, sizeof(xboard));
	if (*xboard && ((bid = getbnum(xboard)) > 0)){ /* XXXbid */
            char backup_path[PATHLEN];
	    setapath(fpath, xboard);
            setbdir(backup_path, xboard);
	    setutmpmode(ANNOUNCE);
            // TODO what's the backup_path here?
	    a_menu(xboard, fpath,
		   HasUserPerm(PERM_BOARD) ? 2 : is_BM_cache(bid) ? 1 : 0,
		   bid, NULL, backup_path);
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
mail_save(int ent GCC_UNUSED, fileheader_t * fhdr GCC_UNUSED, const char *direct GCC_UNUSED)
{
    char            fpath[PATHLEN], backup_path[PATHLEN];
    char            title[TTLEN + 1];

    if (!HasUserPerm(PERM_MAILLIMIT))
        return DONOTHING;
    setuserfile(fpath, fhdr->filename);
    strlcpy(title, "�� ", sizeof(title));
    strlcpy(title + 3, fhdr->title, sizeof(title) - 3);
    a_copyitem(fpath, title, fhdr->owner, 1);
    sethomeman(fpath, cuser.userid);
    sethomedir(backup_path, cuser.userid);
    a_menu(cuser.userid, fpath, 1, 0, NULL, backup_path);
    return FULLUPDATE;
}

static int
del_range_mail(int ent, fileheader_t * fhdr, char *direct)
{
    int ret = del_range(ent, fhdr, direct, direct);
    if (ret == DIRCHANGED)
        setupmailusage();
    return ret;
}

int
m_read(void)
{
    int back_bid;
    static int cReEntrance = 0;


    if (!HasUserPerm(PERM_READMAIL))
        return DONOTHING;

    if (cReEntrance > 0) {
        vmsg("��p�A�z���e�w�b�H�c���C");
        return FULLUPDATE;
    }

    if (get_num_records(currmaildir, sizeof(fileheader_t))) {
	int was_in_digest = currmode & MODE_DIGEST;
	currmode &= ~MODE_DIGEST;
	back_bid = currbid;
	currbid = 0;
        cReEntrance++;
	i_read(RMAIL, currmaildir, mailtitle, maildoent, mail_comms, -1);
	currbid = back_bid;
	currmode |= was_in_digest;
	setmailalert();
        cReEntrance--;
	return 0;
    } else {
	outs("�z�S���ӫH");
	return XEASY;
    }
}

////////////////////////////////////////////////////////////////////////
// Optional Features

static int
mail_waterball(int ent GCC_UNUSED, fileheader_t * fhdr,
               const char *direct GCC_UNUSED)
{
#ifdef OUTJOBSPOOL
    static char     address[60] = "", cmode = 1;
    char            fname[500], genbuf[PATHLEN];
    FILE           *fp;

    if (!(strstr(fhdr->title, "���u") && strstr(fhdr->title, "�O��"))) {
	vmsg("�����O ���u�O�� �~��ϥΤ��y��z����!");
	return 1;
    }

    if (!address[0])
	strlcpy(address, cuser.email, sizeof(address));

    move(b_lines - 8, 0); clrtobot();
    outs(ANSI_COLOR(1;33;45) "�����y��z�{�� " ANSI_RESET "\n"
	 "�t�αN�|���өM���P�H�᪺���y�U�ۿW��\n"
	 "����I���ɭ� (�y�p�ɬq���~) �N��ƾ�z�n�H�e���z\n\n\n");

    if (address[0]) {
	snprintf(genbuf, sizeof(genbuf), "�H�� [%s] ��[Y/n/q]�H ", address);
	getdata(b_lines - 5, 0, genbuf, fname, 3, LCECHO);
	if (fname[0] == 'q') {
	    outmsg("�����B�z");
	    return 1;
	}
	if (fname[0] == 'n')
	    address[0] = '\0';
    }
    if (!address[0]) {
	move(b_lines-4, 0);
	prints(   "�Ъ`�N�ثe�u�䴩�H���з� e-mail �a�}�C\n"
		"�Y�Q�H�^���H�c�Хο�J %s.bbs@" MYHOSTNAME "\n", cuser.userid);

	getdata(b_lines - 5, 0, "�п�J�l��a�}�G", fname, 60, DOECHO);
	if (fname[0] && strchr(fname, '.')) {
	    strlcpy(address, fname, sizeof(address));
	} else {
	    vmsg("�a�}�榡�����T�A�����B�z");
	    return 1;
	}
    }
    trim(address);
    if (invalidaddr(address))
	return -2;
    move(b_lines-4, 0); clrtobot();

    if( strstr(address, ".bbs") && REJECT_OUTTAMAIL(cuser) ){
	outs("\n�z�����n���}�������~�H, ���y��z�t�Τ~��H�J���G\n"
	     "�г·Ш�i�l����j���j�g O�令�������~�H (�b�k�W��)\n"
	     "�A���s���楻�\\�� :)\n");
	vmsg("�Х��}���~�H, �A���s���楻�\\��");
	return FULLUPDATE;
    }

    //snprintf(fname, sizeof(fname), "%d\n", cmode);
    outs("�t�δ��Ѩ�ؼҦ�: \n"
	 "�Ҧ� 0: ��²�Ҧ�, �N���t�C�ⱱ��X, ��K�H�¤�r�s�边��z����\n"
	 "�Ҧ� 1: ���R�Ҧ�, �]�t�C�ⱱ��X��, ��K�b bbs�W�����s�覬��\n");
    getdata(b_lines - 1, 0, "�ϥμҦ�(0/1/Q)? [1]", fname, 3, LCECHO);
    if (fname[0] == 'q') {
	outmsg("�����B�z");
	return FULLUPDATE;
    }
    cmode = (fname[0] != '0' && fname[0] != '1') ? 1 : fname[0] - '0';

    snprintf(fname, sizeof(fname), BBSHOME "/jobspool/water.src.%s-%d",
	     cuser.userid, (int)now);
    setuserfile(genbuf, fhdr->filename);
    Copy(genbuf, fname);
    /* dirty code ;x */
    snprintf(fname, sizeof(fname), BBSHOME "/jobspool/water.des.%s-%d",
	     cuser.userid, (int)now);
    fp = fopen(fname, "wt");
    assert(fp);
    fprintf(fp, "%s\n%s\n%d\n", cuser.userid, address, cmode);
    fclose(fp);
    vmsg("�]�w����, �t�αN�b�U�@�Ӿ��I(�y�p�ɬq���~)�N��ƱH���z");
    return FULLUPDATE;
#else
    return DONOTHING;
#endif
}

#ifdef USE_TIME_CAPSULE
static int
mail_recycle_bin(int ent GCC_UNUSED, fileheader_t * fhdr GCC_UNUSED,
                 const char *direct) {
    if (!HasUserPerm(PERM_BASIC))
        return DONOTHING;
    return psb_recycle_bin(direct, "�ӤH�H�c");
}
#else // USE_TIME_CAPSULE
static int
mail_recycle_bin(int ent GCC_UNUSED,
                 fileheader_t * fhdr GCC_UNUSED,
                 const char *direct GCC_UNUSED) {
    return DONOTHING;
}
#endif // USE_TIME_CAPSULE

////////////////////////////////////////////////////////////////////////
// Command List

// CAUTION: Every commands here should check permission.

static const onekey_t mail_comms[] = {
    { 0, NULL }, // Ctrl('A')
    { 0, NULL }, // Ctrl('B')
    { 0, NULL }, // Ctrl('C')
    { 0, NULL }, // Ctrl('D')
    { 0, NULL }, // Ctrl('E')
    { 0, NULL }, // Ctrl('F')
    { 0, built_mail_index }, // Ctrl('G')
    { 0, NULL }, // Ctrl('H')
    { 0, toggle_showmail_mode }, // Ctrl('I')
    { 0, NULL }, // Ctrl('J')
    { 0, NULL }, // Ctrl('K')
    { 0, NULL }, // Ctrl('L')
    { 0, NULL }, // Ctrl('M')
    { 0, NULL }, // Ctrl('N')
    { 0, NULL }, // Ctrl('O')	// DO NOT USE THIS KEY - UNIX not sending
    { 0, m_send }, // Ctrl('P')
    { 0, NULL }, // Ctrl('Q')
    { 0, NULL }, // Ctrl('R')
    { 0, NULL }, // Ctrl('S')
    { 0, NULL }, // Ctrl('T')
    { 0, NULL }, // Ctrl('U')
    { 0, NULL }, // Ctrl('V')
    { 0, NULL }, // Ctrl('W')
    { 1, mail_cross_post }, // // Ctrl('X')
    { 0, NULL }, // Ctrl('Y')
    { 0, NULL }, // Ctrl('Z') 26
    { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL },
    { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL },
    { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL },
    { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL },
    { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL },
    { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL },
    { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL },
    { 0, NULL }, { 0, NULL }, { 0, NULL },
    { 0, NULL }, // 'A' 65
    { 0, NULL }, // 'B'
    { 0, NULL }, // 'C'
    { 1, del_range_mail }, // 'D'
    { 0, NULL }, // 'E'
    { 1, m_internet_forward }, // 'F'
    { 0, NULL }, // 'G'
    { 0, NULL }, // 'H'
    { 0, NULL }, // 'I'
    { 0, NULL }, // 'J'
    { 0, NULL }, // 'K'
    { 0, NULL }, // 'L'
    { 0, NULL }, // 'M'
    { 0, NULL }, // 'N'
    { 1, mail_nooutmail }, // 'O'
    { 0, NULL }, // 'P'
    { 0, NULL }, // 'Q'
    { 1, mailbox_reply }, // 'R'
    { 0, NULL }, // 'S'
    { 1, NULL }, // 'T'
    { 0, NULL }, // 'U'
    { 0, NULL }, // 'V'
    { 0, NULL }, // 'W'
    { 0, NULL }, // 'X'
    { 0, NULL }, // 'Y'
    { 0, NULL }, // 'Z' 90
    { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL },
    { 0, NULL }, // 'a' 97
    { 0, NULL }, // 'b'
    { 1, mail_cite }, // 'c'
    { 1, mail_del }, // 'd'
    { 0, NULL }, // 'e'
    { 0, NULL }, // 'f'
    { 0, NULL }, // 'g'
    { 0, m_help }, // 'h'
    { 0, NULL }, // 'i'
    { 0, NULL }, // 'j'
    { 0, NULL }, // 'k'
    { 0, NULL }, // 'l'
    { 1, mail_mark }, // 'm'
    { 0, NULL }, // 'n'
    { 0, NULL }, // 'o'
    { 0, NULL }, // 'p'
    { 0, NULL }, // 'q'
    { 1, mail_read }, // 'r'
    { 1, mail_save }, // 's'
    { 0, NULL }, // 't'
    { 1, mail_waterball }, // 'u'
    { 0, mail_read_all }, // 'v'
    { 1, b_call_in }, // 'w'
    { 1, m_forward }, // 'x'
    { 1, mailbox_reply }, // 'y'
    { 0, mail_man }, // 'z' 122
    { 0, NULL }, // '{' 123
    { 0, NULL }, // '|' 124
    { 0, NULL }, // '}' 125
    { 1, mail_recycle_bin }, // '~' 126
};

////////////////////////////////////////////////////////////////////////
// Mailer backend

#include <netdb.h>
#include <pwd.h>
#include <time.h>

#ifndef USE_BSMTP
static int
bbs_sendmail(const char *fpath, const char *title, char *receiver)
{
    char           *ptr;
    char            genbuf[256];
    FILE           *fin, *fout;

    /* ���~�d�I */
    if ((ptr = strchr(receiver, ';'))) {
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
	    strlcpy(hacker, receiver, sizeof(hacker));
	return send_inner_mail(fpath, title, hacker);
    }
    /* Running the sendmail */
    assert(*fpath);
    snprintf(genbuf, sizeof(genbuf),
	    "/usr/sbin/sendmail -f %s%s %s > /dev/null",
	    cuser.userid, str_mail_address, receiver);
    fin = fopen(fpath, "r");
    if (fin == NULL)
	return -1;
    fout = popen(genbuf, "w");
    if (fout == NULL) {
	fclose(fin);
	return -1;
    }

    if (fpath)
	fprintf(fout, "Reply-To: %s%s\nFrom: %s <%s%s>\n",
		cuser.userid, str_mail_address,
		cuser.nickname,
		cuser.userid, str_mail_address);
    fprintf(fout,"To: %s\nSubject: %s\n"
		 "Mime-Version: 1.0\r\n"
		 "Content-Type: text/plain; charset=\"big5\"\r\n"
		 "Content-Transfer-Encoding: 8bit\r\n"
		 "X-Disclaimer: " BBSNAME "�糧�H���e�����t�d�C\n\n",
		receiver, title);

    while (fgets(genbuf, sizeof(genbuf), fin)) {
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
bsmtp(const char *fpath, const char *title, const char *rcpt, const char *from)
{
    char            buf[80], *ptr;
    time4_t         chrono;
    MailQueue       mqueue;

    if (!from)
	from = cuser.userid;

    /* check if the mail is a inner mail */
    if ((ptr = strstr(rcpt, str_mail_address)) || !strchr(rcpt, '@')) {
	char            hacker[20];
	int             len;

	if (strchr(rcpt, '@')) {
	    strlcpy(hacker, rcpt, sizeof(hacker));
	    len = ptr - rcpt;
	    if (0 <= len && (size_t)len < sizeof(hacker))
		hacker[len] = '\0';
	} else
	    strlcpy(hacker, rcpt, sizeof(hacker));
	return send_inner_mail(fpath, title, hacker);
    }
    chrono = now;

    /* stamp the queue file */
    strlcpy(buf, "out/", sizeof(buf));
    for (;;) {
	snprintf(buf + 4, sizeof(buf) - 4, "M.%d.%d.A", (int)++chrono, getpid());
	if (!dashf(buf)) {
	    Copy(fpath, buf);
	    break;
	}
    }

    fpath = buf;

    /* setup mail queue */
    mqueue.mailtime = chrono;
    // XXX (unused) mqueue.method = method;
    strlcpy(mqueue.filepath, fpath, sizeof(mqueue.filepath));
    strlcpy(mqueue.subject, title, sizeof(mqueue.subject));
    strlcpy(mqueue.sender, from, sizeof(mqueue.sender));
    // username is deprecated: why use it?
    // strlcpy(mqueue.username, username, sizeof(mqueue.username));
    strlcpy(mqueue.username, "", sizeof(mqueue.username));
    strlcpy(mqueue.rcpt, rcpt, sizeof(mqueue.rcpt));

    if (append_record("out/" FN_DIR, (fileheader_t *) & mqueue, sizeof(mqueue)) < 0)
	return 0;
    return chrono;
}
#endif				/* USE_BSMTP */
