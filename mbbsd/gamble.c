#include "bbs.h"

#define MAX_ITEM	8	//最大 彩券項目(item) 個數
#define MAX_ITEM_LEN	30	//最大 每一項目名字長度
#define MAX_ITEM_INPUT_LEN  9   //最大 每一項目(手動輸入時)名字長度
#define MAX_SUBJECT_LEN 650	//8*81 = 648 最大 主題長度
#define NARROW_ITEM_WIDTH   8   // old (narrow) item width

#ifndef GAMBLE_ACTION_DELAY_US
#define GAMBLE_ACTION_DELAY_US  (0) // 每個動作之前的 delay
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
	snprintf(genbuf, sizeof(genbuf), "%s 樂透", bh->brdname);
	if (bh->endgamble && now < bh->endgamble &&
	    bh->endgamble - now < 3600) {
	    snprintf(t, sizeof(t),
		     "封盤倒數 %d 秒", (int)(bh->endgamble - now));
	    showtitle(genbuf, t);
	} else
	    showtitle(genbuf, BBSNAME);
    } else
	showtitle(BBSMNAME "彩券", BBSNAME);
    move(2, 0);
    snprintf(genbuf, sizeof(genbuf), "%s/" FN_TICKET_ITEMS, direct);
    if (!(fp = fopen(genbuf, "r"))) {
	outs("\n目前並沒有舉辦樂透\n");
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

    prints(ANSI_COLOR(32) "站規:" ANSI_RESET
     " 1.可購買以下不同類型的彩券。每張要花 " ANSI_COLOR(32) "%d"
	 ANSI_RESET " " MONEYNAME "。\n"
"      2.%s\n"
"      3.開獎時只有一種彩券中獎, 有購買該彩券者, 則可依購買的張數均分總彩金。\n"
"      4.每筆獎金由系統抽取 5%% 之稅金%s。\n"
"      5." ANSI_COLOR(1;31) "如遇系統故障造成帳號回溯等各種問題，"
	 "原則上不予以賠償，風險自擔。" ANSI_RESET "\n"
	    ANSI_COLOR(32) "%s:" ANSI_RESET, *price,
	   bh ? "此樂透由板主負責舉辦並決定開獎時間結果, 站方不管, 願賭服輸。" :
	        "系統每天 2:00 11:00 16:00 21:00 開獎。",
	   bh ? ", 其中 0.05% 分給開獎板主, 最多 500" : "",
	   bh ? "板主自訂規則及說明" : "前幾次開獎結果");

    snprintf(genbuf, sizeof(genbuf), "%s/" FN_TICKET, direct);
    if (!dashf(genbuf)) {
	snprintf(genbuf, sizeof(genbuf), "%s/" FN_TICKET_END, direct);
	end = 1;
    }
    show_file(genbuf, 8, -1, SHOWFILE_ALLOW_ALL);
    move(15, 0); clrtobot();
    outs(ANSI_COLOR(1;32) "目前下注狀況:" ANSI_RESET "\n");

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
    prints(ANSI_RESET "\n已下注總額: "
           ANSI_COLOR(1;33) "%lld" ANSI_RESET, total * (*price));
    if (end) {
	outs("，已經停止下注\n");
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
    getdata_str(b_lines - 1, 0, "要買多少份呢:",
		buf, sizeof(buf), NUMECHO, "1");
    num = atoi(buf);
    if (num < 1)
	return;

    reload_money();
    if (cuser.money/money < num) {
	vmsg("現金不夠 !!!");
	return;
    }

    *item += num;
    pay(money * num, "%s彩券[種類%d,張數%d]", title, type+1, num);

    // XXX magic numbers 5, 14...
    show_file(picture, 5, 14, SHOWFILE_ALLOW_ALL);
    pressanykey();
    usleep(100000); // sleep 0.1s
}

#define lockreturn0(unmode, state) if(lockutmpmode(unmode, state)) return 0
int
ticket(int bid)
{
    int             ch, end = 0;
    int	            n, price, count; /* 購買張數、單價、選項數 */
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
	prints("現有 " MONEYNAME ": " ANSI_COLOR(1;31) "%d" ANSI_RESET "\n"
               "請選擇要購買的種類(1~%d)[Q:離開]" ANSI_RESET ":",
               cuser.money, count);
	ch = vkey();
	/*--
	  Tim011127
	  為了控制CS問題 但是這邊還不能完全解決這問題,
	  若user通過檢查下去, 剛好板主開獎, 還是會造成user的這次紀錄
	  很有可能跑到下次樂透的紀錄去, 也很有可能被板主新開樂透時洗掉
	  不過這邊至少可以做到的是, 頂多只會有一筆資料是錯的
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
    // XXX 這是因為停止下注所以退錢？ 感覺好危險+race condition
    if (price > 0) {
        pay_as_uid(currutmp->uid, -price, "下注失敗退費");
    }
    vmsg("板主已經停止下注了");
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
        //Ptt:	有bug
        return 0;
    }
    lockreturn0(TICKET, LOCK_MULTI);

    do {
        const char *betname_sel = "取消退費";
	do {
	    getdata(20, 0, "選擇中獎的號碼(0:不開獎 99:取消退費):",
                    buf, 3, LCECHO);
	    bet = atoi(buf);
	} while ((bet < 0 || bet > count) && bet != 99);
	if (bet == 0) {
	    unlockutmpmode();
	    return 0;
	}
        if (bet == 99) {
            move(22, 0); SOLVE_ANSI_CACHE(); clrtoeol();
            prints(ANSI_COLOR(1;31) "請注意: 取消要扣手續費 $%d" ANSI_RESET,
                    price * 10);
        } else {
            betname_sel = betname[bet - 1];
        }
        move(20, 0); SOLVE_ANSI_CACHE(); clrtoeol();
        prints("預計開獎項目: 編號:%d，名稱:%s\n", bet, betname_sel);
        getdata(21, 0, "輸入項目名稱以確認你的意識清醒(開錯無法回溯): ",
                buf, MAX_ITEM_INPUT_LEN, DOECHO);
        if (strcasecmp(buf, betname_sel) == 0) {
            snprintf(buf, sizeof(buf), "%d", bet);
        } else {
            getdata(21, 0, "項目名稱不合。要改輸入項目編號嗎[y/N]? ",
                    buf, 3, LCECHO);
            if (buf[0] != 'y') {
                move(20, 0); clrtobot();
                continue;
            }
            getdata(21, 0, "輸入號碼以確認(無回溯機制，開錯責任自負):",
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
	vmsg("已另有人開獎，系統稍後將自動公佈中獎結果於看板");
	return 0;
    }
    if(rename(buf, outcome) != 0)
    {
	unlockutmpmode();
	vmsg("無法準備開獎... 請至 " BN_BUGREPORT " 報告並附上板名。");
	return 0;

    }

    if (fork()) {
	/* Ptt: 用 fork() 防止不正常斷線洗錢 */
	unlockutmpmode();
	vmsg("系統稍後將自動公佈於中獎結果看板(參加者多時要數分鐘)..");
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


    bet--;			/* 轉成矩陣的index */
    /* 取消樂透由 bet == 99 變成 bet == 98 */

    total = load_ticket_record(path, ticket);
    setbfile(buf, bh->brdname, FN_TICKET_LOCK);
    if (!(fp1 = fopen(buf, "r")))
	exit(1);

    /* 還沒開完獎不能下注 只要mv一項就好 */
    if (bet != 98) {
	int forBM;
	money = total * price;

	forBM = money * 0.0005;
	if(forBM > 500) forBM = 500;
        pay(-forBM, "%s 彩金抽成", bh->brdname);

	mail_redenvelop("[彩金抽成]", cuser.userid, forBM, NULL);
	money = ticket[bet] ? money * 0.95 / ticket[bet] : 9999999;
    } else {
	pay(price * 10, "樂透退費手續費");
	money = price;
    }
    setbfile(outcome, bh->brdname, FN_TICKET_OUTCOME);
    if ((fp = fopen(outcome, "w"))) {
        int wide = 0;
	fprintf(fp, "樂透說明\n");
	while (fgets(buf, sizeof(buf), fp1)) {
	    buf[sizeof(buf)-1] = 0;
	    fputs(buf, fp);
	}

	fprintf(fp, "\n下注情況\n");
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
                    "開獎時間: %s\n"
		    "開獎結果: %s\n"
		    "下注總額: %lld\n"
		    "中獎比例: %lld張/%lld張  (%f)\n"
		    "每張中獎彩券可得 %lld " MONEYNAME "\n\n",
                    Cdatelite(&now), betname[bet],
                    total * price,
                    ticket[bet], total,
                    total ? (double)ticket[bet] / total : (double)0,
                    money);

	    fprintf(fp, "%s 開出:%s 總額:%lld 彩金/張:%lld 機率:%1.2f\n\n",
		    Cdatelite(&now), betname[bet], total * price, money,
		    total ? (double)ticket[bet] / total : (double)0);
	} else
	    fprintf(fp, "樂透取消退回: %s\n\n", Cdatelite(&now));

    } // XXX somebody may use fp even fp==NULL
    fclose(fp1);
    /*
     * 以下是給錢動作
     */
    setbfile(buf, bh->brdname, FN_TICKET_USER);
    if ((bet == 98 || ticket[bet]) && (fp1 = fopen(buf, "r"))) {
	int             mybet, uid;
	char            userid[IDLEN + 1];

	while (fscanf(fp1, "%s %d %d\n", userid, &mybet, &i) != EOF) {
	    if (bet == 98 && mybet >= 0 && mybet < count) {
		if (fp)
		    fprintf(fp, "%-*s 買了 %3d 張 %s, 退回 %5lld "
                            MONEYNAME "\n",
			    IDLEN, userid, i, betname[mybet], money * i);
		snprintf(buf, sizeof(buf),
			 "%s 樂透退費! $ %lld", bh->brdname, money * i);
	    } else if (mybet == bet) {
		if (fp)
		    fprintf(fp, "恭喜 %-*s 買了 %3d 張 %s, 獲得 %5lld "
			    MONEYNAME "\n",
			    IDLEN, userid, i, betname[mybet], money * i);
		snprintf(buf, sizeof(buf), "%s 中獎咧! $ %lld",
                         bh->brdname, money * i);
	    } else {
		if (fp)
		    fprintf(fp, "     %-*s 買了 %3d 張 %s\n" ,
			    IDLEN, userid, i, betname[mybet]);
		continue;
            }
	    if ((uid = searchuser(userid, userid)) == 0)
		continue;
            pay_as_uid(uid, -(money * i), BBSMNAME "彩券 - [%s]",
                       betname[mybet]);
	    mail_id(userid, buf, "etc/ticket.win", BBSMNAME "彩券");
	}
	fclose(fp1);
    }
    if (fp) {
	fprintf(fp, "\n--\n※ 開獎站 :" BBSNAME "(" MYHOSTNAME
		") \n◆ From: %s\n", fromhost);
	fclose(fp);
    }

    if (bet != 98)
	snprintf(buf, sizeof(buf), TN_ANNOUNCE " %s 樂透開獎", bh->brdname);
    else
	snprintf(buf, sizeof(buf), TN_ANNOUNCE " %s 樂透取消", bh->brdname);
    post_file(bh->brdname, buf, outcome, "[彩券]");
    post_file("Record", buf + 7, outcome, "[馬路探子]");
    post_file(BN_SECURITY, buf + 7, outcome, "[馬路探子]");

    setbfile(buf, bh->brdname, FN_TICKET_RECORD);
    unlink(buf);

    setbfile(buf, bh->brdname, FN_TICKET_USER);
    post_file(BN_SECURITY, bh->brdname, buf, "[下注紀錄]");
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
	vmsg("目前未舉辦或樂透已開獎");
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
        vmsg("本看板目前被設定為發文無獎勵，無法使用樂透");
        return 0;
    }

    setbfile(fn_ticket, currboard, FN_TICKET);
    setbfile(fn_ticket_end, currboard, FN_TICKET_END);
    setbfile(genbuf, currboard, FN_TICKET_LOCK);

    if (dashf(fn_ticket)) {
	getdata(b_lines - 1, 0, "已經有舉辦樂透, "
		"是否要 [停止下注]?(N/y)：", yn, 3, LCECHO);
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
	getdata(b_lines - 1, 0, "已經有舉辦樂透, "
		"是否要開獎 [否/是]?(N/y)：", yn, 3, LCECHO);
	if (yn[0] != 'y')
	    return FULLUPDATE;
        if(cpuload(NULL) > MAX_CPULOAD/4)
            {
	        vmsg("負荷過高 請於系統負荷低時開獎..");
		return FULLUPDATE;
	    }
	openticket(currbid);
	return FULLUPDATE;
    } else if (dashf(genbuf)) {
	vmsg(" 目前系統正在處理開獎事宜, 請結果出爐後再舉辦.......");
	return FULLUPDATE;
    }
    getdata(b_lines - 2, 0, "要舉辦樂透 (N/y):", yn, 3, LCECHO);
    if (yn[0] != 'y')
	return FULLUPDATE;
    getdata(b_lines - 1, 0, "請輸入主題 (輸入後編輯內容):",
	    msg, 20, DOECHO);
    if (msg[0] == 0 || veditfile(fn_ticket_end) < 0) {
        // 如果有人 race condition 就... 很該死。
        unlink(fn_ticket_end);
	return FULLUPDATE;
    }

    clear();
    showtitle("舉辦樂透", BBSNAME);
    setbfile(tmp, currboard, FN_TICKET_ITEMS ".tmp");

    //sprintf(genbuf, "%s/" FN_TICKET_ITEMS, direct);

    if (!(fp = fopen(tmp, "w")))
	return FULLUPDATE;
    do {
	getdata(2, 0, "輸入彩券價格 (價格:10-10000):", yn, 6, NUMECHO);
	i = atoi(yn);
    } while (i < 10 || i > 10000);
    fprintf(fp, "%d\n", i);
    if (!getdata(3, 0, "設定自動封盤時間?(Y/n)", yn, 3, LCECHO) || yn[0] != 'n') {
	bp->endgamble = gettime(4, now, "封盤於");
	assert(0<=currbid-1 && currbid-1<MAX_BOARD);
	substitute_record(fn_board, bp, sizeof(boardheader_t), currbid);
    }
    move(6, 0);
    snprintf(genbuf, sizeof(genbuf),
	     "\n請到 %s 板 按'f'參與樂透!\n\n"
	     "一張 %d " MONEYNAME " (%s)\n%s%s\n",
	     currboard,
	     i, i < 100 ? "迷你級" : i < 500 ? "平民級" :
	     i < 1000 ? "貴族級" : i < 5000 ? "富豪級" : "傾家蕩產",
	     bp->endgamble ? "樂透結束時間: " : "",
	     bp->endgamble ? Cdate(&bp->endgamble) : ""
	     );
    strcat(msg, genbuf);
    outs("請依次輸入彩券名稱, 需提供2~8項. (未滿八項, 輸入直接按Enter)\n");
    //outs(ANSI_COLOR(1;33) "注意輸入後無法修改！\n");
    for( i = 0 ; i < 8 ; ++i ){
	snprintf(yn, sizeof(yn), " %d)", i + 1);
	getdata(7 + i, 0, yn, genbuf, MAX_ITEM_INPUT_LEN, DOECHO);
	if (!genbuf[0] && i > 1)
	    break;
	fprintf(fp, "%s\n", genbuf);
    }
    fclose(fp);

    setbfile(genbuf, currboard, FN_TICKET_RECORD);
    unlink(genbuf); // Ptt: 防堵利用不同id同時舉辦樂透
    setbfile(genbuf, currboard, FN_TICKET_USER);
    unlink(genbuf); // Ptt: 防堵利用不同id同時舉辦樂透

    setbfile(genbuf, currboard, FN_TICKET_ITEMS);
    setbfile(tmp, currboard, FN_TICKET_ITEMS ".tmp");
    if(!dashf(fn_ticket))
	Rename(tmp, genbuf);

    snprintf(genbuf, sizeof(genbuf), TN_ANNOUNCE " %s 板 開始舉辦樂透!", currboard);
    post_msg(currboard, genbuf, msg, cuser.userid);
    post_msg("Record", genbuf + 7, msg, "[馬路探子]");
    /* Tim 控制CS, 以免正在玩的user把資料已經寫進來 */
    rename(fn_ticket_end, fn_ticket);
    /* 設定完才把檔名改過來 */

    vmsg("樂透彩券設定完成");
    return FULLUPDATE;
#endif
}

int
ticket_main(void)
{
    ticket(0);
    return 0;
}
