#include "bbs.h"

#define MAX_ITEM	8	//�̤j �m�鶵��(item) �Ӽ�
#define MAX_ITEM_LEN	30	//�̤j �C�@���ئW�r����
#define MAX_ITEM_INPUT_LEN  9   //�̤j �C�@����(��ʿ�J��)�W�r����
#define MAX_SUBJECT_LEN 650	//8*81 = 648 �̤j �D�D����
#define NARROW_ITEM_WIDTH   8   // old (narrow) item width

#ifndef GAMBLE_ACTION_DELAY_US
#define GAMBLE_ACTION_DELAY_US  (0) // �C�Ӱʧ@���e�� delay
#endif

// Use "%lld" format string whenever you access variables in bignum_t.
typedef long long bignum_t;

static bignum_t
load_ticket_record(const char *direct, bignum_t ticket[])
{
    char buf[256];
    FILE *fp;
    int i;
    bignum_t total = 0;

    snprintf(buf, sizeof(buf), "%s/" FN_TICKET_RECORD, direct);
    if (!(fp = fopen(buf, "r")))
	return 0;
    for (i = 0; i < MAX_ITEM && fscanf(fp, "%lld ", &ticket[i]) == 1; i++)
	total += ticket[i];

    fclose(fp);
    return total;
}

static int
show_ticket_data(char betname[MAX_ITEM][MAX_ITEM_LEN],
                 const char *direct, int *price,
                 const boardheader_t * bh)
{
    int i, count, wide = 0, end = 0;
    FILE *fp;
    char            genbuf[256], t[25];
    bignum_t total = 0, ticket[MAX_ITEM] = {0};

    clear();
    if (bh) {
	snprintf(genbuf, sizeof(genbuf), "%s �ֳz", bh->brdname);
	if (bh->endgamble && now < bh->endgamble &&
	    bh->endgamble - now < 3600) {
	    snprintf(t, sizeof(t),
		     "�ʽL�˼� %d ��", (int)(bh->endgamble - now));
	    showtitle(genbuf, t);
	} else
	    showtitle(genbuf, BBSNAME);
    } else
	showtitle(BBSMNAME "�m��", BBSNAME);
    move(2, 0);
    snprintf(genbuf, sizeof(genbuf), "%s/" FN_TICKET_ITEMS, direct);
    if (!(fp = fopen(genbuf, "r"))) {
	outs("\n�ثe�èS���|��ֳz\n");
	snprintf(genbuf, sizeof(genbuf), "%s/" FN_TICKET_OUTCOME, direct);
	more(genbuf, NA);
	return 0;
    }
    fgets(genbuf, MAX_ITEM_LEN, fp);
    *price = atoi(genbuf);
    for (count = 0; count < MAX_ITEM; count++) {
	if (!fgets(betname[count], MAX_ITEM_LEN, fp))
	    break;
	chomp(betname[count]);
    }
    fclose(fp);

    prints(ANSI_COLOR(32) "���W:" ANSI_RESET
     " 1.�i�ʶR�H�U���P�������m��C�C�i�n�� " ANSI_COLOR(32) "%d"
	 ANSI_RESET " " MONEYNAME "�C\n"
"      2.%s\n"
"      3.�}���ɥu���@�رm�餤��, ���ʶR�ӱm���, �h�i���ʶR���i�Ƨ����`�m���C\n"
"      4.�C�������Ѩt�Ω�� 5%% ���|��%s�C\n"
"      5." ANSI_COLOR(1;31) "�p�J�t�άG�ٳy���b���^�����U�ذ��D�A"
	 "��h�W�����H���v�A���I�۾�C" ANSI_RESET "\n"
	    ANSI_COLOR(32) "%s:" ANSI_RESET, *price,
	   bh ? "���ֳz�ѪO�D�t�d�|��èM�w�}���ɶ����G, ���褣��, �@��A��C" :
	        "�t�ΨC�� 2:00 11:00 16:00 21:00 �}���C",
	   bh ? ", �䤤 0.05% �����}���O�D, �̦h 500" : "",
	   bh ? "�O�D�ۭq�W�h�λ���" : "�e�X���}�����G");

    snprintf(genbuf, sizeof(genbuf), "%s/" FN_TICKET, direct);
    if (!dashf(genbuf)) {
	snprintf(genbuf, sizeof(genbuf), "%s/" FN_TICKET_END, direct);
	end = 1;
    }
    show_file(genbuf, 8, -1, SHOWFILE_ALLOW_ALL);
    move(15, 0); clrtobot();
    outs(ANSI_COLOR(1;32) "�ثe�U�`���p:" ANSI_RESET "\n");

    total = load_ticket_record(direct, ticket);

    for (i = 0; i < count && !wide; i++) {
        if (strlen(betname[i]) > NARROW_ITEM_WIDTH ||
            ticket[i] > 999999)
            wide = 1;
    }

    for (i = 0; i < count; i++) {
        prints(ANSI_COLOR(0;1) "%d."
               ANSI_COLOR(0;33)"%-*s: "
               ANSI_COLOR(1;33)"%-7lld%s",
               i + 1, (wide ? IDLEN : 8), betname[i],
               ticket[i], wide ? " " : "");
        if (wide) {
            if (i % 3 == 2)
                outc('\n');
        } else {
            if (i % 4 == 3)
                outc('\n');
        }
    }
    prints(ANSI_RESET "\n�w�U�`�`�B: "
           ANSI_COLOR(1;33) "%lld" ANSI_RESET, total * (*price));
    if (end) {
	outs("�A�w�g����U�`\n");
	return -count;
    }
    return count;
}

static int
append_ticket_record(const char *direct, int ch, int n, int count)
{
    FILE *fp;
    bignum_t ticket[8] = {0};
    int i;
    char genbuf[256];

    snprintf(genbuf, sizeof(genbuf), "%s/" FN_TICKET, direct);
    if (!dashf(genbuf))
	return -1;

    snprintf(genbuf, sizeof(genbuf), "%s/" FN_TICKET_USER, direct);
    log_filef(genbuf, LOG_CREAT, "%s %d %d\n", cuser.userid, ch, n);

    snprintf(genbuf, sizeof(genbuf), "%s/" FN_TICKET_RECORD, direct);

    if (!dashf(genbuf)) {
	creat(genbuf, S_IRUSR | S_IWUSR);
    }

    if ((fp = fopen(genbuf, "r+"))) {

	flock(fileno(fp), LOCK_EX);

	for (i = 0; i < MAX_ITEM; i++)
	    if (fscanf(fp, "%lld ", &ticket[i]) != 1)
		break;
	ticket[ch] += n;

	ftruncate(fileno(fp), 0);
	rewind(fp);
	for (i = 0; i < count; i++)
	    fprintf(fp, "%lld ", ticket[i]);
	fflush(fp);

	flock(fileno(fp), LOCK_UN);
	fclose(fp);
    }
    return 0;
}

void
buy_ticket_ui(int money, const char *picture, int *item,
              int type, const char *title)
{
    int             num = 0;
    char            buf[5];

    // XXX defaults to 1?
    getdata_str(b_lines - 1, 0, "�n�R�h�֥��O:",
		buf, sizeof(buf), NUMECHO, "1");
    num = atoi(buf);
    if (num < 1)
	return;

    reload_money();
    if (cuser.money/money < num) {
	vmsg("�{������ !!!");
	return;
    }

    // XXX magic numbers 5, 14...
    show_file(picture, 5, 14, SHOWFILE_ALLOW_ALL);
    pressanykey();

    *item += num;
    pay(money * num, "%s�m��[����%d,�i��%d]", title, type+1, num);

    usleep(100000); // sleep 0.1s
}

#define lockreturn0(unmode, state) if(lockutmpmode(unmode, state)) return 0
int
ticket(int bid)
{
    int             ch, end = 0;
    int	            n, price, count; /* �ʶR�i�ơB����B�ﶵ�� */
    char            path[128], fn_ticket[PATHLEN];
    char            betname[MAX_ITEM][MAX_ITEM_LEN];
    boardheader_t  *bh = NULL;

    // No scripting.
    vkey_purge();
    usleep(2.5 * GAMBLE_ACTION_DELAY_US);   // delay longer

    STATINC(STAT_GAMBLE);
    if (bid) {
	bh = getbcache(bid);
	setbpath(path, bh->brdname);
	setbfile(fn_ticket, bh->brdname, FN_TICKET);
	currbid = bid;
    } else
	strcpy(path, "etc/");

    lockreturn0(TICKET, LOCK_MULTI);
    while (1) {
	count = show_ticket_data(betname, path, &price, bh);
	if (count <= 0) {
	    pressanykey();
	    break;
	}
	move(20, 0);
        vkey_purge();
	reload_money();
	prints("�{�� " MONEYNAME ": " ANSI_COLOR(1;31) "%d" ANSI_RESET "\n"
               "�п�ܭn�ʶR������(1~%d)[Q:���}]" ANSI_RESET ":",
               cuser.money, count);
	ch = vkey();
	/*--
	  Tim011127
	  ���F����CS���D ���O�o���٤��৹���ѨM�o���D,
	  �Yuser�q�L�ˬd�U�h, ��n�O�D�}��, �٬O�|�y��user���o������
	  �ܦ��i��]��U���ֳz�������h, �]�ܦ��i��Q�O�D�s�}�ֳz�ɬ~��
	  ���L�o��ܤ֥i�H���쪺�O, ���h�u�|���@����ƬO����
	--*/
	if (ch == 'q' || ch == 'Q')
	    break;
	outc(ch);
	ch -= '1';
	if (end || ch >= count || ch < 0)
	    continue;
	n = 0;

	buy_ticket_ui(price, "etc/buyticket", &n, ch,
                      bh ? bh->brdname : BBSMNAME);

	if (bid && !dashf(fn_ticket))
	    goto doesnt_catch_up;

	if (n > 0) {
	    if (append_ticket_record(path, ch, n, count) < 0)
		goto doesnt_catch_up;
	}
        usleep(GAMBLE_ACTION_DELAY_US);
    }
    unlockutmpmode();
    return 0;

doesnt_catch_up:

    price = price * n;
    // XXX �o�O�]������U�`�ҥH�h���H �Pı�n�M�I+race condition
    if (price > 0) {
        pay_as_uid(currutmp->uid, -price, "�U�`���Ѱh�O");
    }
    vmsg("�O�D�w�g����U�`�F");
    unlockutmpmode();
    return -1;
}

#ifndef NO_GAMBLE
int
openticket(int bid)
{
    char            path[PATHLEN], buf[PATHLEN], outcome[PATHLEN];
    boardheader_t  *bh = getbcache(bid);
    FILE           *fp, *fp1;
    char            betname[MAX_ITEM][MAX_ITEM_LEN];
    int             bet, price, i;
    bignum_t money = 0, count, total = 0, ticket[MAX_ITEM] = {0};

    setbpath(path, bh->brdname);
    count = -show_ticket_data(betname, path, &price, bh);

    if (count == 0) {
	setbfile(buf, bh->brdname, FN_TICKET_END);
	unlink(buf);
        //Ptt:	��bug
        return 0;
    }
    lockreturn0(TICKET, LOCK_MULTI);

    do {
        const char *betname_sel = "�����h�O";
	do {
	    getdata(20, 0, "��ܤ��������X(0:���}�� 99:�����h�O):",
                    buf, 3, LCECHO);
	    bet = atoi(buf);
	} while ((bet < 0 || bet > count) && bet != 99);
	if (bet == 0) {
	    unlockutmpmode();
	    return 0;
	}
        if (bet == 99) {
            move(22, 0); SOLVE_ANSI_CACHE(); clrtoeol();
            prints(ANSI_COLOR(1;31) "�Ъ`�N: �����n������O $%d" ANSI_RESET,
                    price * 10);
        } else {
            betname_sel = betname[bet - 1];
        }
        move(20, 0); SOLVE_ANSI_CACHE(); clrtoeol();
        prints("�w�p�}������: �s��:%d�A�W��:%s\n", bet, betname_sel);
        getdata(21, 0, "��J���ئW�٥H�T�{�A���N�ѲM��(�}���L�k�^��): ",
                buf, MAX_ITEM_INPUT_LEN, DOECHO);
        if (strcasecmp(buf, betname_sel) == 0) {
            snprintf(buf, sizeof(buf), "%d", bet);
        } else {
            getdata(21, 0, "���ئW�٤��X�C�n���J���ؽs����[y/N]? ",
                    buf, 3, LCECHO);
            if (buf[0] != 'y') {
                move(20, 0); clrtobot();
                continue;
            }
            getdata(21, 0, "��J���X�H�T�{(�L�^������A�}���d���ۭt):",
                    buf, 3, LCECHO);
        }
        move(21, 0); SOLVE_ANSI_CACHE(); clrtoeol();
    } while (bet != atoi(buf));

    // before we fork to process,
    // confirm lock status is correct.
    setbfile(buf, bh->brdname, FN_TICKET_END);
    setbfile(outcome, bh->brdname, FN_TICKET_LOCK);

    if(access(outcome, 0) == 0)
    {
	unlockutmpmode();
	vmsg("�w�t���H�}���A�t�εy��N�۰ʤ��G�������G��ݪO");
	return 0;
    }
    if(rename(buf, outcome) != 0)
    {
	unlockutmpmode();
	vmsg("�L�k�ǳƶ}��... �Ц� " BN_BUGREPORT " ���i�ê��W�O�W�C");
	return 0;

    }

    if (fork()) {
	/* Ptt: �� fork() ������`�_�u�~�� */
	unlockutmpmode();
	vmsg("�t�εy��N�۰ʤ��G�󤤼����G�ݪO(�ѥ[�̦h�ɭn�Ƥ���)..");
	return 0;
    }
    close(0);
    close(1);
    setproctitle("open ticket");
#ifdef CPULIMIT_PER_DAY
    {
	struct rlimit   rml;
	rml.rlim_cur = RLIM_INFINITY;
	rml.rlim_max = RLIM_INFINITY;
	setrlimit(RLIMIT_CPU, &rml);
    }
#endif


    bet--;			/* �ন�x�}��index */
    /* �����ֳz�� bet == 99 �ܦ� bet == 98 */

    total = load_ticket_record(path, ticket);
    setbfile(buf, bh->brdname, FN_TICKET_LOCK);
    if (!(fp1 = fopen(buf, "r")))
	exit(1);

    /* �٨S�}��������U�` �u�nmv�@���N�n */
    if (bet != 98) {
	int forBM;
	money = total * price;

	forBM = money * 0.0005;
	if(forBM > 500) forBM = 500;
        pay(-forBM, "%s �m���⦨", bh->brdname);

	mail_redenvelop("[�m���⦨]", cuser.userid, forBM, NULL);
	money = ticket[bet] ? money * 0.95 / ticket[bet] : 9999999;
    } else {
	pay(price * 10, "�ֳz�h�O����O");
	money = price;
    }
    setbfile(outcome, bh->brdname, FN_TICKET_OUTCOME);
    if ((fp = fopen(outcome, "w"))) {
        int wide = 0;
	fprintf(fp, "�ֳz����\n");
	while (fgets(buf, sizeof(buf), fp1)) {
	    buf[sizeof(buf)-1] = 0;
	    fputs(buf, fp);
	}

	fprintf(fp, "\n�U�`���p\n");
        for (i = 0; i < count && !wide; i++) {
            if (strlen(betname[i]) > NARROW_ITEM_WIDTH ||
                ticket[i] > 999999)
                wide = 1;
        }
	for (i = 0; i < count; i++) {
            if (i % (wide ? 3 : 4) == 0)
                    fputc('\n', fp);
            fprintf(fp, "%d.%-*s: %-7lld%s",
                    i + 1, (wide ? IDLEN : 8), betname[i],
                    ticket[i], wide ? " " : "");
	}
        fputs("\n\n", fp);


	if (bet != 98) {
	    fprintf(fp,
                    "�}���ɶ�: %s\n"
		    "�}�����G: %s\n"
		    "�U�`�`�B: %lld\n"
		    "�������: %lld�i/%lld�i  (%f)\n"
		    "�C�i�����m��i�o %lld " MONEYNAME "\n\n",
                    Cdatelite(&now), betname[bet],
                    total * price,
                    ticket[bet], total,
                    total ? (double)ticket[bet] / total : (double)0,
                    money);

	    fprintf(fp, "%s �}�X:%s �`�B:%lld �m��/�i:%lld ���v:%1.2f\n\n",
		    Cdatelite(&now), betname[bet], total * price, money,
		    total ? (double)ticket[bet] / total : (double)0);
	} else
	    fprintf(fp, "�ֳz�����h�^: %s\n\n", Cdatelite(&now));

    } // XXX somebody may use fp even fp==NULL
    fclose(fp1);
    /*
     * �H�U�O�����ʧ@
     */
    setbfile(buf, bh->brdname, FN_TICKET_USER);
    if ((bet == 98 || ticket[bet]) && (fp1 = fopen(buf, "r"))) {
	int             mybet, uid;
	char            userid[IDLEN + 1];

	while (fscanf(fp1, "%s %d %d\n", userid, &mybet, &i) != EOF) {
	    if (bet == 98 && mybet >= 0 && mybet < count) {
		if (fp)
		    fprintf(fp, "%-*s �R�F %3d �i %s, �h�^ %5lld "
                            MONEYNAME "\n",
			    IDLEN, userid, i, betname[mybet], money * i);
		snprintf(buf, sizeof(buf),
			 "%s �ֳz�h�O! $ %lld", bh->brdname, money * i);
	    } else if (mybet == bet) {
		if (fp)
		    fprintf(fp, "���� %-*s �R�F %3d �i %s, ��o %5lld "
			    MONEYNAME "\n",
			    IDLEN, userid, i, betname[mybet], money * i);
		snprintf(buf, sizeof(buf), "%s ������! $ %lld",
                         bh->brdname, money * i);
	    } else {
		if (fp)
		    fprintf(fp, "     %-*s �R�F %3d �i %s\n" ,
			    IDLEN, userid, i, betname[mybet]);
		continue;
            }
	    if ((uid = searchuser(userid, userid)) == 0)
		continue;
            pay_as_uid(uid, -(money * i), BBSMNAME "�m�� - [%s]",
                       betname[mybet]);
	    mail_id(userid, buf, "etc/ticket.win", BBSMNAME "�m��");
	}
	fclose(fp1);
    }
    if (fp) {
	fprintf(fp, "\n--\n�� �}���� :" BBSNAME "(" MYHOSTNAME
		") \n�� From: %s\n", fromhost);
	fclose(fp);
    }

    if (bet != 98)
	snprintf(buf, sizeof(buf), TN_ANNOUNCE " %s �ֳz�}��", bh->brdname);
    else
	snprintf(buf, sizeof(buf), TN_ANNOUNCE " %s �ֳz����", bh->brdname);
    post_file(bh->brdname, buf, outcome, "[�m��]");
    post_file("Record", buf + 7, outcome, "[�������l]");
    post_file(BN_SECURITY, buf + 7, outcome, "[�������l]");

    setbfile(buf, bh->brdname, FN_TICKET_RECORD);
    unlink(buf);

    setbfile(buf, bh->brdname, FN_TICKET_USER);
    post_file(BN_SECURITY, bh->brdname, buf, "[�U�`����]");
    unlink(buf);

    setbfile(buf, bh->brdname, FN_TICKET_LOCK);
    unlink(buf);
    exit(1);
    return 0;
}

int
stop_gamble(void)
{
    boardheader_t  *bp = getbcache(currbid);
    char            fn_ticket[128], fn_ticket_end[128];
    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
    if (!bp->endgamble || bp->endgamble > now)
	return 0;

    setbfile(fn_ticket, currboard, FN_TICKET);
    setbfile(fn_ticket_end, currboard, FN_TICKET_END);

    rename(fn_ticket, fn_ticket_end);
    if (bp->endgamble) {
	bp->endgamble = 0;
	assert(0<=currbid-1 && currbid-1<MAX_BOARD);
	substitute_record(fn_board, bp, sizeof(boardheader_t), currbid);
    }
    return 1;
}
#endif

int
join_gamble(int eng GCC_UNUSED, const fileheader_t * fhdr GCC_UNUSED,
            const char *direct GCC_UNUSED)
{
#ifdef NO_GAMBLE
    return DONOTHING;
#else

    if (!HasBasicUserPerm(PERM_LOGINOK))
	return DONOTHING;
    if (stop_gamble()) {
	vmsg("�ثe���|��μֳz�w�}��");
	return DONOTHING;
    }
    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
    ticket(currbid);
    return FULLUPDATE;
#endif
}

int
hold_gamble(void)
{
#ifdef NO_GAMBLE
    return DONOTHING;
#else
    char            fn_ticket[128], fn_ticket_end[128], genbuf[128], msg[256] = "",
                    yn[10] = "";
    char tmp[128];
    boardheader_t  *bp = getbcache(currbid);
    int             i;
    FILE           *fp = NULL;

    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
    if (!(currmode & MODE_BOARD))
	return 0;
    if (bp->brdattr & BRD_NOCREDIT ) {
        vmsg("���ݪO�ثe�Q�]�w���o��L���y�A�L�k�ϥμֳz");
        return 0;
    }

    setbfile(fn_ticket, currboard, FN_TICKET);
    setbfile(fn_ticket_end, currboard, FN_TICKET_END);
    setbfile(genbuf, currboard, FN_TICKET_LOCK);

    if (dashf(fn_ticket)) {
	getdata(b_lines - 1, 0, "�w�g���|��ֳz, "
		"�O�_�n [����U�`]?(N/y)�G", yn, 3, LCECHO);
	if (yn[0] != 'y')
	    return FULLUPDATE;
	rename(fn_ticket, fn_ticket_end);
	if (bp->endgamble) {
	    bp->endgamble = 0;
	    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
	    substitute_record(fn_board, bp, sizeof(boardheader_t), currbid);

	}
	return FULLUPDATE;
    }
    if (dashf(fn_ticket_end)) {
	getdata(b_lines - 1, 0, "�w�g���|��ֳz, "
		"�O�_�n�}�� [�_/�O]?(N/y)�G", yn, 3, LCECHO);
	if (yn[0] != 'y')
	    return FULLUPDATE;
        if(cpuload(NULL) > MAX_CPULOAD/4)
            {
	        vmsg("�t���L�� �Щ�t�έt���C�ɶ}��..");
		return FULLUPDATE;
	    }
	openticket(currbid);
	return FULLUPDATE;
    } else if (dashf(genbuf)) {
	vmsg(" �ثe�t�Υ��b�B�z�}���Ʃy, �е��G�X�l��A�|��.......");
	return FULLUPDATE;
    }
    getdata(b_lines - 2, 0, "�n�|��ֳz (N/y):", yn, 3, LCECHO);
    if (yn[0] != 'y')
	return FULLUPDATE;
    getdata(b_lines - 1, 0, "�п�J�D�D (��J��s�褺�e):",
	    msg, 20, DOECHO);
    if (msg[0] == 0 || veditfile(fn_ticket_end) < 0) {
        // �p�G���H race condition �N... �ܸӦ��C
        unlink(fn_ticket_end);
	return FULLUPDATE;
    }

    clear();
    showtitle("�|��ֳz", BBSNAME);
    setbfile(tmp, currboard, FN_TICKET_ITEMS ".tmp");

    //sprintf(genbuf, "%s/" FN_TICKET_ITEMS, direct);

    if (!(fp = fopen(tmp, "w")))
	return FULLUPDATE;
    do {
	getdata(2, 0, "��J�m����� (����:10-10000):", yn, 6, NUMECHO);
	i = atoi(yn);
    } while (i < 10 || i > 10000);
    fprintf(fp, "%d\n", i);
    if (!getdata(3, 0, "�]�w�۰ʫʽL�ɶ�?(Y/n)", yn, 3, LCECHO) || yn[0] != 'n') {
	bp->endgamble = gettime(4, now, "�ʽL��");
	assert(0<=currbid-1 && currbid-1<MAX_BOARD);
	substitute_record(fn_board, bp, sizeof(boardheader_t), currbid);
    }
    move(6, 0);
    snprintf(genbuf, sizeof(genbuf),
	     "\n�Ш� %s �O ��'f'�ѻP�ֳz!\n\n"
	     "�@�i %d " MONEYNAME " (%s)\n%s%s\n",
	     currboard,
	     i, i < 100 ? "�g�A��" : i < 500 ? "������" :
	     i < 1000 ? "�Q�گ�" : i < 5000 ? "�I����" : "�ɮa����",
	     bp->endgamble ? "�ֳz�����ɶ�: " : "",
	     bp->endgamble ? Cdate(&bp->endgamble) : ""
	     );
    strcat(msg, genbuf);
    outs("�Ш̦���J�m��W��, �ݴ���2~8��. (�����K��, ��J������Enter)\n");
    //outs(ANSI_COLOR(1;33) "�`�N��J��L�k�ק�I\n");
    for( i = 0 ; i < 8 ; ++i ){
	snprintf(yn, sizeof(yn), " %d)", i + 1);
	getdata(7 + i, 0, yn, genbuf, MAX_ITEM_INPUT_LEN, DOECHO);
	if (!genbuf[0] && i > 1)
	    break;
	fprintf(fp, "%s\n", genbuf);
    }
    fclose(fp);

    setbfile(genbuf, currboard, FN_TICKET_RECORD);
    unlink(genbuf); // Ptt: �����Q�Τ��Pid�P���|��ֳz
    setbfile(genbuf, currboard, FN_TICKET_USER);
    unlink(genbuf); // Ptt: �����Q�Τ��Pid�P���|��ֳz

    setbfile(genbuf, currboard, FN_TICKET_ITEMS);
    setbfile(tmp, currboard, FN_TICKET_ITEMS ".tmp");
    if(!dashf(fn_ticket))
	Rename(tmp, genbuf);

    snprintf(genbuf, sizeof(genbuf), TN_ANNOUNCE " %s �O �}�l�|��ֳz!", currboard);
    post_msg(currboard, genbuf, msg, cuser.userid);
    post_msg("Record", genbuf + 7, msg, "[�������l]");
    /* Tim ����CS, �H�K���b����user���Ƥw�g�g�i�� */
    rename(fn_ticket_end, fn_ticket);
    /* �]�w���~���ɦW��L�� */

    vmsg("�ֳz�m��]�w����");
    return FULLUPDATE;
#endif
}

int
ticket_main(void)
{
    ticket(0);
    return 0;
}
