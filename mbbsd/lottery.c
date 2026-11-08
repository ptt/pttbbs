#include "bbs.h"

#define MAX_ITEM	8	//最大 彩券項目(item) 個數
#define MAX_ITEM_LEN	30	//最大 每一項目名字長度
#define MAX_ITEM_INPUT_LEN  9   //最大 每一項目(手動輸入時)名字長度
#define MAX_SUBJECT_LEN 650	//8*81 = 648 最大 主題長度
#define NARROW_ITEM_WIDTH   8   // old (narrow) item width
#define LOTTERY_MIN_PRICE   10      // the price range hold_gamble() allows
#define LOTTERY_MAX_PRICE   10000

#ifndef GAMBLE_ACTION_DELAY_US
#define GAMBLE_ACTION_DELAY_US  (0) // 每個動作之前的 delay
#endif

// Use "%lld" format string whenever you access variables in bignum_t.
typedef long long bignum_t;

static bignum_t
load_ticket_record(const char *direct, bignum_t ticket[])
{
    FILE *fp;
    int i;
    bignum_t total = 0;

    AUTO_STR(buf, "%s/" FN_TICKET_RECORD, direct);
    if (!(fp = fopen(buf, "r")))
	return 0;
    for (i = 0; i < MAX_ITEM && fscanf(fp, "%lld ", &ticket[i]) == 1; i++)
	total += ticket[i];

    fclose(fp);
    return total;
}

/* Parses ticket.items' first line, its price. Returns the price, or 0 if
 * it is missing or outside what hold_gamble() allows: a broken file, with
 * no lottery to buy into or draw. */
static int
parse_ticket_header(const char *line)
{
    int price = atoi(line);

    return (price >= LOTTERY_MIN_PRICE && price <= LOTTERY_MAX_PRICE) ? price : 0;
}

/* Shows a lottery: returns its item count, negated once betting is closed,
 * or 0 if there is none to buy into or draw. *price gets its price, 0 if
 * ticket.items was read but is broken, or -1 if it could not be opened. */
static int
show_ticket_data(char betname[MAX_ITEM][MAX_ITEM_LEN],
                 const char *direct, int *price,
                 const boardheader_t * bh)
{
    int i, count, wide = 0, end = 0;
    FILE *fp;
    char            genbuf[256];
    bignum_t total = 0, ticket[MAX_ITEM] = {0};

    clear();
    if (bh) {
	SNPRINTF(genbuf, "%s 樂透", bh->brdname);
	if (bh->endgamble && time4_lt(now, bh->endgamble) &&
	    time4_diff(bh->endgamble, now) < 3600) {
	    showtitle(genbuf, TEMPFORMAT(25, "截止倒數 %d 秒", (int)time4_diff(bh->endgamble, now)));
	} else
	    showtitle(genbuf, BBSNAME);
    } else
	showtitle(BBSMNAME "彩券", BBSNAME);
    move(2, 0);
    *price = -1;
    SNPRINTF(genbuf, "%s/" FN_TICKET_ITEMS, direct);
    if (!(fp = fopen(genbuf, "r"))) {
	outs("\n目前並沒有舉辦樂透\n");
	SNPRINTF(genbuf, "%s/" FN_TICKET_OUTCOME, direct);
	more(genbuf, NA);
	return 0;
    }
    *price = 0;
    if (fgets(genbuf, sizeof(genbuf), fp))
	*price = parse_ticket_header(genbuf);
    if (!*price) {
	// Malformed or absent price line: treat as no open lottery rather than
	// charging a bogus price (and later dividing by zero at money/price),
	// and say so.
	outs("\n樂透的設定有誤，目前無法投注或開獎，請洽板主或站長。\n");
	fclose(fp);
	return 0;
    }
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
	   bh ? "此樂透由板主負責舉辦並決定開獎時間結果, 站方不管, 結果自負。" :
	        "系統每天 2:00 11:00 16:00 21:00 開獎。",
	   bh ? ", 其中 0.05% 分給開獎板主, 最多 500" : "",
	   bh ? "板主自訂規則及說明" : "前幾次開獎結果");

    SNPRINTF(genbuf, "%s/" FN_TICKET, direct);
    if (!dashf(genbuf)) {
	SNPRINTF(genbuf, "%s/" FN_TICKET_END, direct);
	end = 1;
    }
    show_file(genbuf, 8, -1, SHOWFILE_ALLOW_ALL);
    move(15, 0); clrtobot();
    outs(ANSI_COLOR(1;32) "目前投注狀況:" ANSI_RESET "\n");

    total = load_ticket_record(direct, ticket);

    for (i = 0; i < count && !wide; i++) {
        if (stream_width(betname[i]) > NARROW_ITEM_WIDTH ||
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
    prints(ANSI_RESET "\n已投注總額: "
           ANSI_COLOR(1;33) "%lld" ANSI_RESET, total * (*price));
    if (end) {
	outs("，已經截止投注\n");
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

    SNPRINTF(genbuf, "%s/" FN_TICKET, direct);
    if (!dashf(genbuf))
	return -1;

    SNPRINTF(genbuf, "%s/" FN_TICKET_USER, direct);
    file_appendf(genbuf, "%s %d %d\n", cuser.userid, ch, n);

    SNPRINTF(genbuf, "%s/" FN_TICKET_RECORD, direct);

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

/* Asks how many tickets to buy at price each: the number, or 0 if none, or
 * if the money isn't there. ticket() charges for them. */
static int
buy_ticket_ui(int price)
{
    int             num = 0;
    char            buf[5];

    // XXX defaults to 1?
    getdata_str(b_lines - 1, 0, "要買多少份呢:",
		buf, sizeof(buf), NUMECHO, "1");
    num = atoi(buf);
    if (num < 1)
	return 0;

    reload_money();
    if (cuser.money/price < num) {
	vmsg("現金不夠 !!!");
	return 0;
    }
    return num;
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

    if (bid <= 0)
        return -1;

    STATINC(STAT_GAMBLE);
    bh = getbcache(bid);
    setbpath(path, bh->brdname);
    setbfile(fn_ticket, bh->brdname, FN_TICKET);
    currbid = bid;

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
	n = buy_ticket_ui(price);

	// Closed meanwhile: nothing is charged yet.
	if (!dashf(fn_ticket))
	    goto doesnt_catch_up;

	if (n > 0) {
	    // Charge and record together, refunding if the record fails: a
	    // kick (SIGHUP) or a shutdown (SIGTERM) between them would leave
	    // the buyer charged without a bet. The receipt waits for a key,
	    // so it comes after.
	    // The debit is taken only if it is all there: pay() stops at zero,
	    // so two racing buys could both pass buy_ticket_ui()'s check and
	    // get tickets for one payment.
	    sigset_t old_set;
	    block_abort_signals(&old_set);
	    int paid = pay_if_enough(price * n, "%s彩券[種類%d,張數%d]",
				     bh->brdname, ch + 1, n) >= 0;
	    int recorded = paid && append_ticket_record(path, ch, n, count) >= 0;
	    if (paid && !recorded)
		pay(-price * n, "投注失敗退費");
	    restore_abort_signals(&old_set);
	    if (!paid) {
		vmsg("現金不夠 !!!");
	    } else if (!recorded) {
		goto doesnt_catch_up;
	    } else {
		show_file("etc/buyticket", 5, 14, SHOWFILE_ALLOW_ALL);
		pressanykey();
		usleep(100000); // sleep 0.1s
	    }
	}
        usleep(GAMBLE_ACTION_DELAY_US);
    }
    unlockutmpmode();
    return 0;

doesnt_catch_up:
    // Closed before the charge, or refunded above when the record failed.
    vmsg("板主已經截止投注了");
    unlockutmpmode();
    return -1;
}

int
openticket(int bid)
{
    if (!IS_ENABLED(CONFIG_LOTTERY))
        return 0;
    char            path[PATHLEN], buf[PATHLEN], outcome[PATHLEN];
    boardheader_t  *bh = getbcache(bid);
    char            betname[MAX_ITEM][MAX_ITEM_LEN];
    int             bet, price;
    bignum_t        count;

    setbpath(path, bh->brdname);
    count = -show_ticket_data(betname, path, &price, bh);

    if (count == 0) {
	setbfile(buf, bh->brdname, FN_TICKET_ITEMS);
	if (price < 0 && dashf(buf)) {
	    // There but not opened: not known to be broken, so keep it.
	    vmsg("系統忙碌中，請稍後再試。");
	    return 0;
	}
	// A ticket.items show_ticket_data() read but found broken (its message
	// is on the screen) can be neither drawn nor refunded: ask before
	// ending the lottery, rather than end it unasked or leave the board
	// stuck. Set aside as ticket.items.bad, with ticket.user.bad (who bet
	// what, which the next lottery would clear) for a sysop, and logged.
	if (price >= 0) {
	    if (vans("要結束這期無法開獎的樂透嗎？已投注的" MONEYNAME
		     "無法退還 [y/N] ") != 'y')
		return 0;
	    char bad[PATHLEN];
	    setbfile(bad, bh->brdname, FN_TICKET_ITEMS ".bad");
	    if (rename(buf, bad) < 0) {
		vmsg("系統忙碌中，請稍後再試。");
		return 0;
	    }
	    setbfile(buf, bh->brdname, FN_TICKET_USER);
	    setbfile(bad, bh->brdname, FN_TICKET_USER ".bad");
	    rename(buf, bad);
	    modlog_t log;
	    modlog(bh->brdname, NULL, modlog_init(&log, "結束無法開獎的樂透"));
	}
	// No lottery (any more): drop a leftover ticket.end.
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
            reload_money();
            if (cuser.money < price * 10) {
                move(22, 0); clrtoeol();
                vmsg("現金不足以支付取消退費手續費！");
                continue;
            }
            move(22, 0); clrtoeol();
            prints(ANSI_COLOR(1;31) "請注意: 取消要扣手續費 $%d (您目前有 $%d)" ANSI_RESET,
                    price * 10, cuser.money);
        } else {
            betname_sel = betname[bet - 1];
        }
        move(20, 0); clrtoeol();
        prints("預計開獎項目: 編號:%d，名稱:%s\n", bet, betname_sel);
        getdata(21, 0, "輸入項目名稱以確認你的意識清醒(開錯無法回溯): ",
                buf, MAX_ITEM_INPUT_LEN, DOECHO);
        if (strcasecmp(buf, betname_sel) == 0) {
            SNPRINTF(buf, "%d", bet);
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
        move(21, 0); clrtoeol();
    } while (bet != atoi(buf));

    if (bet == 99) {
        reload_money();
        if (cuser.money < price * 10) {
            unlockutmpmode();
            vmsg("現金不足以支付取消退費手續費！");
            return 0;
        }
    }

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

    char bet_str[16];
    snprintf(bet_str, sizeof(bet_str), "%d", bet);

    pid_t pid = fork();
    if (pid < 0) {
        unlockutmpmode();
        vmsg("系統資源不足，無法執行開獎程式。");
        return 0;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, 0);
            dup2(devnull, 1);
            dup2(devnull, 2);
            if (devnull > 2)
                close(devnull);
        }
        execl(BBSHOME "/bin/openticket", "openticket", bh->brdname, bet_str, cuser.userid, (char *)NULL);
        execl("bin/openticket", "openticket", bh->brdname, bet_str, cuser.userid, (char *)NULL);
        setbfile(buf, bh->brdname, FN_TICKET_END);
        setbfile(outcome, bh->brdname, FN_TICKET_LOCK);
        rename(outcome, buf);
        exit(1);
    }
    unlockutmpmode();
    vmsg("系統稍後將自動公佈於中獎結果看板(參加者多時要數分鐘)..");
    return 0;
}

int
stop_gamble(void)
{
    boardheader_t  *bp = getbcache(currbid);
    char            fn_ticket[128], fn_ticket_end[128];
    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
    if (!bp->endgamble || time4_gt(bp->endgamble, now))
	return 0;

    setbfile(fn_ticket, currboard, FN_TICKET);
    setbfile(fn_ticket_end, currboard, FN_TICKET_END);

    rename(fn_ticket, fn_ticket_end);
    if (bp->endgamble) {
	bp->endgamble = 0;
	assert(0<=currbid-1 && currbid-1<MAX_BOARD);
	substitute_record(FN_BOARD, bp, sizeof(boardheader_t), currbid);
    }
    return 1;
}

int
join_gamble(int eng GCC_UNUSED, const fileheader_t * fhdr GCC_UNUSED,
            const char *direct GCC_UNUSED)
{
    if (!IS_ENABLED(CONFIG_LOTTERY))
        return DONOTHING;

    if (!HasBasicUserPerm(PERM_LOGINOK))
	return DONOTHING;
    if (stop_gamble()) {
	vmsg("目前未舉辦或樂透已開獎");
	return DONOTHING;
    }
    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
    ticket(currbid);
    return FULLUPDATE;
}

// The form to hold a lottery.
typedef struct {
    char title[20];
    int  price;
    int  days;          // Stops in days.
    char at[6];         // At HHMM or HH:MM; empty for the time now.
    char items[MAX_ITEM][MAX_ITEM_INPUT_LEN];
} lottery_form_t;

static const char *
lottery_price_class(int price)
{
    return price < 100 ? "迷你級" : price < 500 ? "平民級" :
           price < 1000 ? "貴族級" : price < 5000 ? "富豪級" : "傾家蕩產";
}

// Parses HHMM or HH:MM into minutes of the day, or -1.
static int
lottery_parse_at(const char *at)
{
    int h, m;
    char c;
    if (sscanf(at, "%d:%d%c", &h, &m, &c) != 2) {
        int v;
        if (strlen(at) < 3 || strspn(at, "0123456789") != strlen(at) ||
            sscanf(at, "%d", &v) != 1)
            return -1;
        h = v / 100;
        m = v % 100;
    }
    if (h < 0 || h > 23 || m < 0 || m > 59)
        return -1;
    return h * 60 + m;
}

// When the lottery of form stops, at second 0, or 0 for not stopping by
// itself (no days and no time).
static time4_t
lottery_end_time(const lottery_form_t *form)
{
    if (!form->days && !form->at[0])
        return 0;

    struct tm tm;
    localtime4_r(&now, &tm);
    tm.tm_mday += form->days;
    int at = form->at[0] ? lottery_parse_at(form->at) : -1;
    if (at >= 0) {
        tm.tm_hour = at / 60;
        tm.tm_min = at % 60;
    }
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    time_t t = mktime(&tm);
    return t > 0 ? (time4_t)t : 0;
}

static void
lottery_format_price(const vform_field_t *field GCC_UNUSED, const void *data,
                     char *buf, size_t bufsz)
{
    const lottery_form_t *form = data;
    snprintf(buf, bufsz, "%d (%s)", form->price,
             lottery_price_class(form->price));
}

// Shows when it stops, once the days are entered.
static void
lottery_format_days(const vform_field_t *field GCC_UNUSED, const void *data,
                    char *buf, size_t bufsz)
{
    const lottery_form_t *form = data;
    time4_t t = lottery_end_time(form);
    if (!t)
        strlcpy(buf, "0 (不自動截止)", bufsz);
    else if (form->days)
        snprintf(buf, bufsz, "%d 天後 (%s)", form->days, CdateHM(&t));
    else
        snprintf(buf, bufsz, "今天 (%s)", CdateHM(&t));
}

static void
lottery_format_at(const vform_field_t *field GCC_UNUSED, const void *data,
                  char *buf, size_t bufsz)
{
    const lottery_form_t *form = data;
    int at = form->at[0] ? lottery_parse_at(form->at) : -1;
    if (at >= 0)
        snprintf(buf, bufsz, "%02d:%02d", at / 60, at % 60);
    else
        strlcpy(buf, form->at, bufsz);
}

static const char *
lottery_at_validator(const vform_field_t *field GCC_UNUSED,
                     const void *val_ptr, void *priv GCC_UNUSED)
{
    const char *val = val_ptr;
    if (!val || !*val || lottery_parse_at(val) >= 0)
        return NULL;
    return "請輸入 HHMM 或 HH:MM，例如 2200";
}

static const char *
lottery_title_validator(const vform_field_t *field GCC_UNUSED,
                        const void *val_ptr, void *priv GCC_UNUSED)
{
    const char *val = val_ptr;
    return (val && *val) ? NULL : "請輸入樂透主題";
}

static const char *
lottery_form_validator(const vform_def_t *def GCC_UNUSED, const void *data,
                       void *priv GCC_UNUSED)
{
    const lottery_form_t *form = data;
    int n = 0;
    for (int i = 0; i < MAX_ITEM; i++)
        if (form->items[i][0])
            n++;
    if (n < 2)
        return "至少需要 2 個彩券項目";
    time4_t t = lottery_end_time(form);
    if (t && !time4_gt(t, now))
        return "截止時間必須晚於現在";
    return NULL;
}

#define LOTTERY_ITEM_FIELD(n) \
    VFORM_FIELD_TEXT("項目 " #n, "彩券項目名稱 (需提供 2~8 項，可留空)", \
                     NULL, lottery_form_t, items[n - 1], 0, NULL)

static const vform_field_t lottery_fields[] = {
    VFORM_FIELD_TEXT("主題", "樂透的主題 (確定後再編輯內容)", NULL,
                     lottery_form_t, title, 0, lottery_title_validator),
    { .label = "彩券價格", .hint = "一張彩券的價格 (10~10000)",
      .type = VFORM_INT, .offset = offsetof(lottery_form_t, price),
      .size = MEMBER_SIZE(lottery_form_t, price),
      .min_val = LOTTERY_MIN_PRICE, .max_val = LOTTERY_MAX_PRICE,
      .format_fn = lottery_format_price },
    { .label = "幾天後截止", .hint = "幾天後自動截止投注 (0 且不設時刻為不自動截止)",
      .type = VFORM_INT, .offset = offsetof(lottery_form_t, days),
      .size = MEMBER_SIZE(lottery_form_t, days),
      .min_val = 0, .max_val = 365, .format_fn = lottery_format_days },
    { .label = "截止時刻", .hint = "HHMM 或 HH:MM，例如 2200 (留空為現在的時刻)",
      .type = VFORM_TEXT, .offset = offsetof(lottery_form_t, at),
      .size = MEMBER_SIZE(lottery_form_t, at),
      .validator = lottery_at_validator, .format_fn = lottery_format_at },
    LOTTERY_ITEM_FIELD(1), LOTTERY_ITEM_FIELD(2), LOTTERY_ITEM_FIELD(3),
    LOTTERY_ITEM_FIELD(4), LOTTERY_ITEM_FIELD(5), LOTTERY_ITEM_FIELD(6),
    LOTTERY_ITEM_FIELD(7), LOTTERY_ITEM_FIELD(8),
};

static const vform_def_t lottery_form_def = {
    .title = "舉辦樂透",
    .subtitle = "設定主題項目後按 ^X, 儲存後再編輯說明內容, ^C q 放棄跳出。",
    .mode = VFORM_MODE_INPLACE,
    .fields = lottery_fields,
    .num_fields = ARRAY_SIZE(lottery_fields),
    .form_validator = lottery_form_validator,
    .submit_prompt = "確定設定並開始編輯樂透內容嗎？[Y/n] ",
};

int
hold_gamble(void)
{
    if (!IS_ENABLED(CONFIG_LOTTERY))
        return DONOTHING;
    char            fn_ticket[128], fn_ticket_end[128], genbuf[128], msg[256] = "",
                    yn[10] = "";
    char tmp[128];
    boardheader_t  *bp = getbcache(currbid);
    int             i;
    FILE           *fp = NULL;

    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
    if (!CBRD_IS_BM())
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
		"是否要 [截止投注]?(N/y)：", yn, 3, LCECHO);
	if (yn[0] != 'y')
	    return FULLUPDATE;
	rename(fn_ticket, fn_ticket_end);
	modlog_t log;
	modlog(currboard, NULL, modlog_init(&log, "樂透截止投注"));
	if (bp->endgamble) {
	    bp->endgamble = 0;
	    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
	    substitute_record(FN_BOARD, bp, sizeof(boardheader_t), currbid);

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
	modlog_t log;
	modlog(currboard, NULL, modlog_init(&log, "樂透開獎"));
	openticket(currbid);
	return FULLUPDATE;
    } else if (dashf(genbuf)) {
	vmsg(" 目前系統正在處理開獎事宜, 請結果出爐後再舉辦.......");
	return FULLUPDATE;
    }
    lottery_form_t form = { .price = 100 };
    if (vform_run(&lottery_form_def, &form, NULL) <= 0)
	return FULLUPDATE;

    // Another moderator may have started meanwhile.
    if (dashf(fn_ticket) || dashf(fn_ticket_end) || dashf(genbuf)) {
	vmsg("其他板主已在處理樂透，請稍後再試");
	return FULLUPDATE;
    }
    if (veditfile(fn_ticket_end) < 0) {
        // 如果有人 race condition 就... 很該死。
        unlink(fn_ticket_end);
	vmsg("取消舉辦樂透");
	return FULLUPDATE;
    }

    // Saved to the board once everything is set: the lottery stops
    // when it is due. Checked again after editing.
    time4_t endgamble = lottery_end_time(&form);
    if (endgamble && !time4_gt(endgamble, now)) {
	unlink(fn_ticket_end);
	vmsg("截止時間已過，請重新舉辦");
	return FULLUPDATE;
    }

    setbfile(tmp, currboard, FN_TICKET_ITEMS ".tmp");
    if (!(fp = fopen(tmp, "w"))) {
	unlink(fn_ticket_end);
	vmsg("系統錯誤，無法舉辦樂透");
	return FULLUPDATE;
    }
    fprintf(fp, "%d\n", form.price);
    for (i = 0; i < MAX_ITEM; i++) {
	if (form.items[i][0])
	    fprintf(fp, "%s\n", form.items[i]);
    }
    fclose(fp);

    STRLCPY(msg, form.title);
    SNPRINTF(genbuf, "\n請到 %s 板 按'f'參與樂透!\n\n"
	     "一張 %d " MONEYNAME " (%s)\n%s%s\n",
	     currboard, form.price, lottery_price_class(form.price),
	     endgamble ? "樂透結束時間: " : "",
	     endgamble ? Cdate(&endgamble) : ""
	     );
    strcat(msg, genbuf);

    if (bp->endgamble != endgamble) {
	bp->endgamble = endgamble;
	assert(0<=currbid-1 && currbid-1<MAX_BOARD);
	substitute_record(FN_BOARD, bp, sizeof(boardheader_t), currbid);
    }

    setbfile(genbuf, currboard, FN_TICKET_RECORD);
    unlink(genbuf); // Ptt: 防堵利用不同id同時舉辦樂透
    setbfile(genbuf, currboard, FN_TICKET_USER);
    unlink(genbuf); // Ptt: 防堵利用不同id同時舉辦樂透

    setbfile(genbuf, currboard, FN_TICKET_ITEMS);
    setbfile(tmp, currboard, FN_TICKET_ITEMS ".tmp");
    if(!dashf(fn_ticket))
	Rename(tmp, genbuf);

    SNPRINTF(genbuf, TN_ANNOUNCE " %s 板 開始舉辦樂透!", currboard);
    post_msg(currboard, genbuf, msg, cuser.userid);
    post_msg("Record", genbuf + 7, msg, "[馬路探子]");
    /* Tim 控制CS, 以免正在玩的user把資料已經寫進來 */
    rename(fn_ticket_end, fn_ticket);
    /* 設定完才把檔名改過來 */

    modlog_t log;
    modlog(currboard, NULL, modlog_init(&log, "舉辦樂透"));
    vmsg("樂透彩券設定完成");
    return FULLUPDATE;
}

