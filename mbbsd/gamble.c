/* $Id$ */
#include "bbs.h"

#define MAX_ITEM	8	//最大 賭項(item) 個數
#define MAX_ITEM_LEN	30	//最大 每一賭項名字長度
#define MAX_SUBJECT_LEN 650	//8*81 = 648 最大 主題長度

static int
load_ticket_record(const char *direct, int ticket[])
{
    char            buf[256];
    int             i, total = 0;
    FILE           *fp;
    snprintf(buf, sizeof(buf), "%s/" FN_TICKET_RECORD, direct);
    if (!(fp = fopen(buf, "r")))
	return 0;
    for (i = 0; i < MAX_ITEM && fscanf(fp, "%9d ", &ticket[i])==1; i++)
	total = total + ticket[i];
    fclose(fp);
    return total;
}

static int
show_ticket_data(char betname[MAX_ITEM][MAX_ITEM_LEN],const char *direct, int *price, const boardheader_t * bh)
{
    int             i, count, total = 0, end = 0, ticket[MAX_ITEM] = {0, 0, 0, 0, 0, 0, 0, 0};
    FILE           *fp;
    char            genbuf[256], t[25];

    clear();
    if (bh) {
	snprintf(genbuf, sizeof(genbuf), "%s 賭盤", bh->brdname);
	if (bh->endgamble && now < bh->endgamble &&
	    bh->endgamble - now < 3600) {
	    snprintf(t, sizeof(t),
		     "封盤倒數 %d 秒", (int)(bh->endgamble - now));
	    showtitle(genbuf, t);
	} else
	    showtitle(genbuf, BBSNAME);
    } else
	showtitle(BBSMNAME "賭盤", BBSNAME);
    move(2, 0);
    snprintf(genbuf, sizeof(genbuf), "%s/" FN_TICKET_ITEMS, direct);
    if (!(fp = fopen(genbuf, "r"))) {
	outs("\n目前並沒有舉辦賭盤\n");
	snprintf(genbuf, sizeof(genbuf), "%s/" FN_TICKET_OUTCOME, direct);
	more(genbuf, NA);
	return 0;
    }
    fgets(genbuf, MAX_ITEM_LEN, fp);
    *price = atoi(genbuf);
    for (count = 0; fgets(betname[count], MAX_ITEM_LEN, fp) && count < MAX_ITEM; count++) {
	chomp(betname[count]);
    }
    fclose(fp);

    prints(ANSI_COLOR(32) "站規:" ANSI_RESET 
     " 1.可購買以下不同類型的彩票。每張要花 " ANSI_COLOR(32) "%d" 
	 ANSI_RESET " 元。\n"
"      2.%s\n"
"      3.開獎時只有一種彩票中獎, 有購買該彩票者, 則可依購買的張數均分總賭金。\n"
"      4.每筆獎金由系統抽取 5%% 之稅金%s。\n"
"      5." ANSI_COLOR(1;31) "如遇系統故障造成帳號回溯等各種問題，"
	 "原則上不予以賠償，風險自擔。" ANSI_RESET "\n"
	    ANSI_COLOR(32) "%s:" ANSI_RESET, *price,
	   bh ? "此賭盤由板主負責舉辦並決定開獎時間結果, 站方不管, 願賭服輸。" :
	        "系統每天 2:00 11:00 16:00 21:00 開獎。",
	   bh ? ", 其中 2% 分給開獎板主" : "",
	   bh ? "板主自訂規則及說明" : "前幾次開獎結果");

    snprintf(genbuf, sizeof(genbuf), "%s/" FN_TICKET, direct);
    if (!dashf(genbuf)) {
	snprintf(genbuf, sizeof(genbuf), "%s/" FN_TICKET_END, direct);
	end = 1;
    }
    show_file(genbuf, 8, -1, SHOWFILE_ALLOW_ALL);
    move(15, 0);
    outs(ANSI_COLOR(1;32) "目前下注狀況:" ANSI_RESET "\n");

    total = load_ticket_record(direct, ticket);

    outs(ANSI_COLOR(33));
    for (i = 0; i < count; i++) {
	prints("%d.%-8s: %-7d", i + 1, betname[i], ticket[i]);
	if (i == 3)
	    outc('\n');
    }
    prints(ANSI_RESET "\n" ANSI_COLOR(42) " 下注總金額:" ANSI_COLOR(31) " %d 元 " ANSI_RESET, total * (*price));
    if (end) {
	outs("\n賭盤已經停止下注\n");
	return -count;
    }
    return count;
}

static int 
append_ticket_record(const char *direct, int ch, int n, int count)
{
    FILE           *fp;
    int             ticket[8] = {0, 0, 0, 0, 0, 0, 0, 0}, i;
    char            genbuf[256];

    snprintf(genbuf, sizeof(genbuf), "%s/" FN_TICKET, direct);
    if (!dashf(genbuf))
	return -1;

    snprintf(genbuf, sizeof(genbuf), "%s/" FN_TICKET_USER, direct);
    if ((fp = fopen(genbuf, "a"))) {
	fprintf(fp, "%s %d %d\n", cuser.userid, ch, n);
	fclose(fp);
    }

    snprintf(genbuf, sizeof(genbuf), "%s/" FN_TICKET_RECORD, direct);

    if (!dashf(genbuf)) {
	creat(genbuf, S_IRUSR | S_IWUSR);
    }

    if ((fp = fopen(genbuf, "r+"))) {

	flock(fileno(fp), LOCK_EX);

	for (i = 0; i < MAX_ITEM; i++)
	    if (fscanf(fp, "%9d ", &ticket[i]) != 1)
		break;
	ticket[ch] += n;

	ftruncate(fileno(fp), 0);
	rewind(fp);
	for (i = 0; i < count; i++)
	    fprintf(fp, "%d ", ticket[i]);
	fflush(fp);

	flock(fileno(fp), LOCK_UN);
	fclose(fp);
    }
    return 0;
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
	reload_money();
	prints(ANSI_COLOR(44) "錢: %-10d  " ANSI_RESET "\n" ANSI_COLOR(1) "請選擇要購買的種類(1~%d)"
	       "[Q:離開]" ANSI_RESET ":", cuser.money, count);
	ch = igetch();
	/*--
	  Tim011127
	  為了控制CS問題 但是這邊還不能完全解決這問題,
	  若user通過檢查下去, 剛好板主開獎, 還是會造成user的這次紀錄
	  很有可能跑到下次賭盤的紀錄去, 也很有可能被板主新開賭盤時洗掉
	  不過這邊至少可以做到的是, 頂多只會有一筆資料是錯的
	--*/
	if (ch == 'q' || ch == 'Q')
	    break;
	ch -= '1';
	if (end || ch >= count || ch < 0)
	    continue;
	n = 0;
	ch_buyitem(price, "etc/buyticket", &n, 0);

	if (bid && !dashf(fn_ticket))
	    goto doesnt_catch_up;

	if (n > 0) {
	    if (append_ticket_record(path, ch, n, count) < 0)
		goto doesnt_catch_up;
	}
    }
    unlockutmpmode();
    return 0;

doesnt_catch_up:

    price = price * n;
    if (price > 0)
	deumoney(currutmp->uid, price);
    vmsg("板主已經停止下注了 不能賭嚕");
    unlockutmpmode();
    return -1;
}

int
openticket(int bid)
{
    char            path[PATHLEN], buf[PATHLEN], outcome[PATHLEN];
    int             i, money = 0, count, bet, price, total = 0, 
		    ticket[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    boardheader_t  *bh = getbcache(bid);
    FILE           *fp, *fp1;
    char            betname[MAX_ITEM][MAX_ITEM_LEN];

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
	do {
	    getdata(20, 0,
		    ANSI_COLOR(1) "選擇中獎的號碼(0:不開獎 99:取消退錢)" ANSI_RESET ":", buf, 3, LCECHO);
	    bet = atoi(buf);
	    move(0, 0);
	    clrtoeol();
	} while ((bet < 0 || bet > count) && bet != 99);
	if (bet == 0) {
	    unlockutmpmode();
	    return 0;
	}
	getdata(21, 0, ANSI_COLOR(1) "再次確認輸入號碼" ANSI_RESET ":", buf, 3, LCECHO);
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
#ifdef CPULIMIT
    {
	struct rlimit   rml;
	rml.rlim_cur = RLIM_INFINITY;
	rml.rlim_max = RLIM_INFINITY;
	setrlimit(RLIMIT_CPU, &rml);
    }
#endif


    bet--;			/* 轉成矩陣的index */

    total = load_ticket_record(path, ticket);
    setbfile(buf, bh->brdname, FN_TICKET_LOCK);
    if (!(fp1 = fopen(buf, "r")))
	exit(1);

    /* 還沒開完獎不能賭博 只要mv一項就好 */
    if (bet != 98) {
	money = total * price;
	demoney(money * 0.02);
	mail_redenvelop("[賭場抽頭]", cuser.userid, money * 0.02, 'n');
	money = ticket[bet] ? money * 0.95 / ticket[bet] : 9999999;
    } else {
	vice(price * 10, "賭盤退錢手續費");
	money = price;
    }
    setbfile(outcome, bh->brdname, FN_TICKET_OUTCOME);
    if ((fp = fopen(outcome, "w"))) {
	fprintf(fp, "賭盤說明\n");
	while (fgets(buf, sizeof(buf), fp1)) {
	    buf[sizeof(buf)-1] = 0;
	    fputs(buf, fp);
	}
	fprintf(fp, "下注情況\n");

	fprintf(fp, ANSI_COLOR(33));
	for (i = 0; i < count; i++) {
	    fprintf(fp, "%d.%-8s: %-7d", i + 1, betname[i], ticket[i]);
	    if (i == 3)
		fprintf(fp, "\n");
	}
	fprintf(fp, ANSI_RESET "\n");

	if (bet != 98) {
	    fprintf(fp, "\n\n開獎時間： %s \n\n"
		    "開獎結果： %s \n\n"
		    "所有金額： %d 元 \n"
		    "中獎比例： %d張/%d張  (%f)\n"
		    "每張中獎彩票可得 %d " MONEYNAME "幣 \n\n",
	    Cdatelite(&now), betname[bet], total * price, ticket[bet], total,
		    (float)ticket[bet] / total, money);

	    fprintf(fp, "%s 賭盤開出:%s 所有金額:%d 元 獎金/張:%d 元 機率:%1.2f\n\n",
		    Cdatelite(&now), betname[bet], total * price, money,
		    total ? (float)ticket[bet] / total : 0);
	} else
	    fprintf(fp, "\n\n賭盤取消退錢： %s \n\n", Cdatelite(&now));

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
		    fprintf(fp, "%s 買了 %d 張 %s, 退回 %d 枚" MONEYNAME "幣\n"
			    ,userid, i, betname[mybet], money * i);
		snprintf(buf, sizeof(buf),
			 "%s 賭場退錢! $ %d", bh->brdname, money * i);
	    } else if (mybet == bet) {
		if (fp)
		    fprintf(fp, "恭喜 %s 買了%d 張 %s, 獲得 %d 枚" MONEYNAME "幣\n"
			    ,userid, i, betname[mybet], money * i);
		snprintf(buf, sizeof(buf), "%s 中獎咧! $ %d", bh->brdname, money * i);
	    } else
		continue;
	    if ((uid = searchuser(userid, userid)) == 0)
		continue;
	    deumoney(uid, money * i);
	    mail_id(userid, buf, "etc/ticket.win", BBSMNAME "賭場");
	}
	fclose(fp1);
    }
    if (fp) {
	fprintf(fp, "\n--\n※ 開獎站 :" BBSNAME "(" MYHOSTNAME
		") \n◆ From: %s\n", fromhost);
	fclose(fp);
    }

    if (bet != 98)
	snprintf(buf, sizeof(buf), "[公告] %s 賭盤開獎", bh->brdname);
    else
	snprintf(buf, sizeof(buf), "[公告] %s 賭盤取消", bh->brdname);
    post_file(bh->brdname, buf, outcome, "[賭神]");
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
ticket_main(void)
{
    ticket(0);
    return 0;
}
