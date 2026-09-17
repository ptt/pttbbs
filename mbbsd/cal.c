#include "bbs.h"

// XXX numposts itself is an integer, but some records (by del post!?) may
// create invalid records as -1... so we'd better make it signed for real
// comparison.
char *get_restriction_reason(
        unsigned int numlogindays,
        unsigned int badpost GCC_UNUSED,

        unsigned int limits_logins,
        unsigned int limits_badpost GCC_UNUSED,

        size_t sz_msg, char *msg) {

    syncnow();
    if (numlogindays / 10 < limits_logins) {
        snprintf(msg, sz_msg,
                 STR_LOGINDAYS "未滿 %d " STR_LOGINDAYS
                 "(目前%d" STR_LOGINDAYS_QTY ") ",
                 limits_logins * 10, numlogindays);
        return msg;
    }
#ifdef ASSESS
    if  (badpost > (255 - limits_badpost)) {
        snprintf(msg, sz_msg, "退文超過 %d 篇(目前%d篇)",
                 255 - limits_badpost, badpost);
        return msg;
    }
#endif
    return NULL;
}

/* 防堵 Multi play */
static int
is_playing(int unmode)
{
    register int    i;
    register userinfo_t *uentp;
    unsigned int p = StringHash(cuser.userid) % USHM_SIZE;

    for (i = 0; i < USHM_SIZE; i++, p++) { // XXX linear search
	if (p == USHM_SIZE)
	    p = 0;
	uentp = &(SHM->uinfo[p]);
	if (uentp->mode == DEBUGSLEEPING)
	    continue;
	if (uentp->uid == usernum &&
	    uentp->lockmode == unmode)
	    return 1;
    }
    return 0;
}

int
lockutmpmode(int unmode, int state)
{
    int             errorno = 0;

    if (currutmp->lockmode)
	errorno = LOCK_THIS;
    else if (state == LOCK_MULTI && is_playing(unmode))
	errorno = LOCK_MULTI;

    if (errorno) {
	clear();
	move(10, 20);
	if (errorno == LOCK_THIS)
	    prints("請先離開 %s 才能再 %s ",
		   ModeTypeTable[currutmp->lockmode],
		   ModeTypeTable[unmode]);
	else
	    prints("抱歉! 因為您目前多重登入所以無法使用 %s",
                   ModeTypeTable[unmode]);
	pressanykey();
	return errorno;
    }
    setutmpmode(unmode);
    currutmp->lockmode = unmode;
    return 0;
}

int
unlockutmpmode(void)
{
    currutmp->lockmode = 0;
    return 0;
}

static int
do_pay(int uid, int money, const char *item GCC_UNUSED, const char *reason)
{
    int oldm, newm;
    const char *userid;

    assert(money != 0);
    userid = getuserid(uid);
    assert(userid);
    assert(reason);

    // if we cannot find user, abort
    if (!userid)
        return -1;

    oldm = moneyof(uid);
    newm = deumoney(uid, -money);
    if (uid == usernum)
        reload_money();

    {
        char buf[PATHLEN];
        sethomefile(buf, userid, FN_RECENTPAY);
        rotate_text_logfile(buf, SZ_RECENTPAY, 0.2);
        syncnow();
        log_payment(buf, money, oldm, newm, reason, now);
    }

    return newm;
}

int
pay_as_uid(int uid, int money, const char *item, ...)
{
    va_list ap;
    char reason[STRLEN*3] ="";

    if (!money)
        return 0;

    if (item) {
        va_start(ap, item);
        vsnprintf(reason, sizeof(reason)-1, item, ap);
        va_end(ap);
    }

    return do_pay(uid, money, item, reason);
}

int
pay(int money, const char *item, ...)
{
    va_list ap;
    char reason[STRLEN*3] ="";

    if (!money)
        return 0;

    if (item) {
        va_start(ap, item);
        vsnprintf(reason, sizeof(reason)-1, item, ap);
        va_end(ap);
    }

    return do_pay(usernum, money, item, reason);
}

int
p_from(void)
{
    char tmp_from[sizeof(currutmp->from)];

    if (vans("確定要改故鄉?[y/N]") != 'y')
	return 0;

    STRLCPY(tmp_from, currutmp->from);
    if (getdata(b_lines - 1, 0, "請輸入新故鄉:",
		tmp_from, sizeof(tmp_from), DOECHO) &&
	strcmp(tmp_from, currutmp->from) != 0)
    {
	STRLCPY(currutmp->from, tmp_from);
    }
    return 0;
}

int
mail_redenvelop(const char *from, const char *to, int money, char *fpath)
{
    char            _fpath[PATHLEN], dirent[PATHLEN];
    fileheader_t    fhdr;
    FILE           *fp;

    if (!fpath) fpath = _fpath;

    sethomepath(fpath, to);
    stampfile(fpath, &fhdr);

    if (!(fp = fopen(fpath, "w")))
	return -1;

    fprintf(fp, "作者: %s\n"
	    "標題: 招財進寶\n"
	    "時間: %s\n"
	    ANSI_COLOR(1;33) "親愛的 %s ：\n\n" ANSI_RESET
	    ANSI_COLOR(1;31) "    我包給你一個 %d " MONEYNAME
                                 "的大紅包喔 ^_^\n\n"
	    "    禮輕情意重，請笑納...... ^_^" ANSI_RESET "\n"
#if defined(USE_RECENTPAY) || defined(LOG_RECENTPAY)
            "\n  您可於下列位置找到最近的交易記錄:\n"
            "  主選單 => (U)ser個人設定 => (L)MyLogs個人記錄 =>"
            " (P)RecentPay最近交易記錄\n"
#endif
            , from, ctime4(&now), to, money);
    fclose(fp);

    // colorize topic to make sure this is issued by system.
    SNPRINTF(fhdr.title, ANSI_COLOR(1;37;41) "[紅包]" ANSI_RESET " $%d", money);
    STRLCPY(fhdr.owner, from);
    sethomedir(dirent, to);
    append_record(dirent, &fhdr, sizeof(fhdr));
    return 0;
}

/* 給錢與贈與稅 */

int
give_tax(int money)
{
    int tax = money * 0.1;
    assert (money >= 0);
    if (money % 10)
	tax ++;
    return (tax < 1) ? 1 : tax;
}
int
cal_before_givetax(int taxed_money)
{
    int m = taxed_money / 9.0f * 10 + 1;
    if (m > 1 && taxed_money % 9 == 0)
	m--;
    return m;
}

int
cal_after_givetax(int money)
{
    return money - give_tax(money);
}

typedef struct {
    int is_before_tax;
    int count;
} GiveMoneyVgetCtx;

static const char * const alert_trade = "\n" ANSI_COLOR(0;1;31)
    "提醒您本站的虛擬 " MONEYNAME " 不應與其它虛擬或現實生活"
    "通用之貨幣進行交易\n"
    "若查獲有使用者經由不法途徑取得再與其它使用者進行貨幣間之交易時\n"
    "站方將直接扣回。為避免造成您個人損失，請三思而後行。"
    ANSI_RESET "\n";

static int
give_money_vget_changecb(int key GCC_UNUSED, VGET_RUNTIME *prt, void *instance)
{
    GiveMoneyVgetCtx *ctx = (GiveMoneyVgetCtx *)instance;
    int m1 = atoi(prt->buf), m2 = m1;
    char c1 = ' ', c2 = ' ';

    if (ctx->is_before_tax)
        m2 = cal_after_givetax(m1), c1 = '>';
    else
        m1 = cal_before_givetax(m2), c2 = '>';

    if (m1 <= 0 || m2 <= 0)
        m1 = m2 = 0;

    move(4, 0);
    if (ctx->count <= 1) {
        prints(" %c 你要付出 (稅前): %-10d\n", c1, m1);
        prints(" %c 對方收到 (稅後): %-10d\n", c2, m2);
    } else {
        clrtobot();
        prints(" %c 每人付出 (稅前): %-7d (總計支出: $%lld = %d x %d人)\n",
               c1, m1, (long long)m1 * ctx->count, m1, ctx->count);
        prints(" %c 每人收到 (稅後): %-7d (總計實得: $%lld = %d x %d人)\n",
               c2, m2, (long long)m2 * ctx->count, m2, ctx->count);
        mvouts(15, 0, alert_trade);
    }
    return VGETCB_NONE;
}

static int
give_money_vget_peekcb(int key, VGET_RUNTIME *prt, void *instance)
{
    GiveMoneyVgetCtx *ctx = (GiveMoneyVgetCtx *)instance;

    if (key >= '0' && key <= '9')
        return VGETCB_NONE;
    if (key != KEY_TAB)
        return VGETCB_NONE;

    ctx->is_before_tax = !ctx->is_before_tax;
    give_money_vget_changecb(key, prt, instance);
    return VGETCB_NEXT;
}

static int
give_money_ui_list(struct Vector *namelist)
{
    int count = Vector_length(namelist);
    int can_send_mail = 1;
    char money_buf[20] = "";
    char passbuf[PW_PLAIN_SIZE];
    char custom_msg[STRLEN] = "";
    int m = 0, mtax = 0, tries = 3, skipauth = 0;
    int i, valid_count = 0;
    int *uids = NULL;
    static time4_t lastauth = 0;
    const char *myid = cuser.userid;
    const char *first_id = Vector_get(namelist, 0);
    int64_t total_cost = 0;

    if (count <= 0)
        return -1;

    if (count == 1) {
        move(1, 0);
        clrtobot();
        prints("這位幸運兒的id: " ANSI_COLOR(1) "%s\n", first_id);
    } else {
        vs_hdr("給予" MONEYNAME " - 設定金額");
        prints("收款對象: 共 %d 人 (%s 等人)\n", count, first_id);
    }
    mvouts(15, 0, alert_trade);

    mvprints(2, 0,
            "要給%s多少" MONEYNAME "呢? (可按 TAB 切換輸入稅前/稅後金額, 稅率固定 10%%)\n",
            count > 1 ? "每人" : "");
    outs(count == 1 ? " 請輸入金額: " : " 請輸入每人金額: ");
    {
        GiveMoneyVgetCtx ctx = { 1, count };
        const VGET_CALLBACKS cb = {
            give_money_vget_peekcb,
            NULL,
            give_money_vget_changecb,
        };
        if (vgetstring(money_buf, 7, VGET_DIGITS, "", &cb, &ctx))
            m = atoi(money_buf);
        if (m > 0 && !ctx.is_before_tax)
            m = cal_before_givetax(m);
    }

    mtax = give_tax(m);
    if (m < 2 || m - mtax <= 0) {
        vmsg("金額過少，交易取消!");
        return -1;
    }

    total_cost = (int64_t)m * count;
    reload_money();
    if (total_cost > cuser.money) {
        vmsgf("你沒有那麼多" MONEYNAME "喔! (需要 $%lld，目前僅有 $%d)",
                (long long)total_cost, cuser.money);
        return -1;
    }

    move(4, 0);
    if (count == 1) {
        prints("交易內容: %s 將給予 %s : [未稅] $%d (稅金 $%d )\n"
               "對方實得: $%d\n",
               cuser.userid, first_id, m, mtax, m - mtax);

        if (HAS_ANGEL && HasUserPerm(PERM_ANGEL)) {
            userec_t xuser = {0};
            getuser(first_id, &xuser);
            while (strcmp(xuser.myangel, cuser.userid) == 0) {
                char yn[3];
                mvouts(6, 0, "他是你的小主人，是否匿名？[y/n]: ");
                vgets(yn, sizeof(yn), VGET_LOWERCASE);
                if (yn[0] == 'y') {
                    myid = "小天使";
                    break;
                } else if (yn[0] == 'n') {
                    break;
                }
            }
        }

        if (is_rejected(first_id)) {
            move(13, 0);
            outs(ANSI_COLOR(1;35)
                 "對方拒絕收信，完成交易後將不寄送紅包袋。" ANSI_RESET);
            can_send_mail = 0;
        }
    } else {
        clrtobot();
        prints("交易內容: %s 將發送紅包給 %d 人 (每人 [未稅] $%d / 實得 $%d)\n"
               "總計支出: " ANSI_COLOR(1;33) "$%lld" ANSI_RESET " (您目前餘額: $%d)\n",
               cuser.userid, count, m, m - mtax, (long long)total_cost, cuser.money);
        if (HAS_ANGEL && HasUserPerm(PERM_ANGEL)) {
            mvouts(3, 0, "注意多人名單內若有小主人是不會匿名的。\n");
        }
        getdata(6, 0, "紅包袋附言(可直接按 Enter 略過): ",
                custom_msg, sizeof(custom_msg), DOECHO);
    }

    move(7, 0);
    if (time4_diff(now, lastauth) >= 15 * 60) {
        outs(ANSI_COLOR(1;31) "為了避免誤按或是惡意詐騙，"
             "在完成交易前要重新確認您的身份。" ANSI_RESET);
    } else {
        outs("你的認證尚未過期，可暫時跳過密碼認證程序。\n");
        if (vans("確定進行交易嗎？ (y/N): ") == 'y')
            skipauth = 1;
        else
            tries = -1;
        move(7, 0);
    }

    while (!skipauth && tries-- > 0) {
        getdata(8, 0, MSG_PASSWD, passbuf, sizeof(passbuf), NOECHO);
        if (checkuser_passwd(cuser_ref, passbuf)) {
            lastauth = now;
            break;
        }
        if (tries > 0 &&
            vmsg("密碼錯誤，請重試或按 n 取消交易。") == 'n')
            return -1;
    }

    if (tries < 0) {
        vmsg("交易取消!");
        return -1;
    }

    outs("\n交易正在進行中，請稍候...\n");
    refresh();

    uids = (int *)malloc(sizeof(int) * count);
    if (!uids) {
        vmsg("系統記憶體不足，交易取消!");
        return -1;
    }

    const char *first_valid_id = first_id;
    for (i = 0; i < count; i++) {
        const char *id = Vector_get(namelist, i);
        int u = searchuser(id, NULL);
        if (u > 0 && strcasecmp(id, cuser.userid) != 0 && strcasecmp(id, STR_GUEST) != 0) {
            if (valid_count == 0)
                first_valid_id = id;
            uids[i] = u;
            valid_count++;
        } else {
            uids[i] = 0;
        }
    }

    total_cost = (int64_t)m * valid_count;
    reload_money();
    if (valid_count <= 0 || total_cost > cuser.money) {
        free(uids);
        outs(ANSI_COLOR(1;31) "錢不夠了，交易失敗！" ANSI_RESET "\n");
        vmsg("錢不夠了，交易失敗。");
        return -1;
    }

    sigset_t block_set, old_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGHUP);
    sigaddset(&block_set, SIGPIPE);
    sigaddset(&block_set, SIGTERM);
#ifdef SIGXCPU
    sigaddset(&block_set, SIGXCPU);
#endif
    sigprocmask(SIG_BLOCK, &block_set, &old_set);

    FILE *fp_money = fopen(FN_MONEY, "a");
    FILE *fp_receipt = NULL;
    if (count > 1) {
        char receipt_fpath[PATHLEN];
        fileheader_t receipt_fhdr;
        char dirent[PATHLEN];

        sethomepath(receipt_fpath, cuser.userid);
        stampfile(receipt_fpath, &receipt_fhdr);
        if ((fp_receipt = fopen(receipt_fpath, "w"))) {
            fprintf(fp_receipt, "作者: %s\n"
                                "標題: [紅包明細] 發送給 %s 等 %d 人\n"
                                "時間: %s\n\n"
                                "您於 %s 發送了多人紅包：\n"
                                "  每人金額：稅前 $%d / 稅後實得 $%d\n"
                                "  預計人數：共 %d 人 (總計支出 $%lld)\n",
                    cuser.userid, first_valid_id, valid_count,
                    ctime4(&now), Cdate(&now), m, m - mtax,
                    valid_count, (long long)total_cost);
            if (custom_msg[0])
                fprintf(fp_receipt, "  紅包附言：%s\n", custom_msg);
            fprintf(fp_receipt, "\n【發送明細紀錄】：\n");
            fflush(fp_receipt);

            SNPRINTF(receipt_fhdr.title, ANSI_COLOR(1;37;44) "[明細]" ANSI_RESET " 紅包發送給 %s 等 %d 人",
                     first_valid_id, valid_count);
            STRLCPY(receipt_fhdr.owner, cuser.userid);
            sethomedir(dirent, cuser.userid);
            append_record(dirent, &receipt_fhdr, sizeof(receipt_fhdr));
        }
    }

    // 實際給予金錢：一律先扣發錢者總額，再逐一發給收款人
    if (valid_count == 1) {
        if (strcasecmp(myid, cuser.userid) != 0)
            pay(m, "以 %s 的名義轉帳給 %s (稅後 $%d)", myid, first_valid_id, m - mtax);
        else
            pay(m, "轉帳給 %s (稅後 $%d)", first_valid_id, m - mtax);
    } else {
        pay((int)total_cost, "多人紅包給 %s 等 %d 人 (每人稅後 $%d, 詳見信箱明細)",
            first_valid_id, valid_count, m - mtax);
    }

    for (i = 0; i < count; i++) {
        const char *id = Vector_get(namelist, i);
        if (uids[i] <= 0 ||
            pay_as_uid(uids[i], -(m - mtax), "來自 %s 的轉帳 (稅前 $%d)", myid, m) < 0) {
            if (uids[i] > 0)
                pay(-m, "退還發錢失敗款項 (%s)", id);
            if (count == 1) {
                if (fp_money)
                    fclose(fp_money);
                free(uids);
                sigprocmask(SIG_SETMASK, &old_set, NULL);
                outs(ANSI_COLOR(1;31) "交易失敗！" ANSI_RESET "\n");
                vmsg("交易失敗。");
                return -1;
            }
            if (fp_receipt) {
                fprintf(fp_receipt, "%-12s: 發錢失敗\n", id);
                fflush(fp_receipt);
            }
            continue;
        }

        if (fp_money)
            fprintf(fp_money, "%-12s 給 %-12s %d\t(稅後 %d)\t%s\n",
                    cuser.userid, id, m, m - mtax, Cdate(&now));

        if (!can_send_mail || is_rejected(id)) {
            if (fp_receipt) {
                fprintf(fp_receipt, "%-12s: 收到稅後 %d (對方拒收信件)\n", id, m - mtax);
                fflush(fp_receipt);
            }
        } else {
            char fpath[PATHLEN];
            if (mail_redenvelop(myid, id, m - mtax, fpath) < 0) {
                if (count == 1)
                    outs(ANSI_COLOR(1;31) "已轉入對方帳戶但紅包袋寄送失敗。" ANSI_RESET);
                if (fp_receipt) {
                    fprintf(fp_receipt, "%-12s: 收到稅後 %d (紅包袋寄送失敗)\n", id, m - mtax);
                    fflush(fp_receipt);
                }
            } else {
                if (count == 1) {
                    if (vans("交易已完成，要修改紅包袋嗎？[y/N] ") == 'y')
                        veditfile(fpath);
                } else if (custom_msg[0]) {
                    file_appendf(fpath, "\n" ANSI_COLOR(1;36)
                                 "    【紅包附言】：%s" ANSI_RESET "\n", custom_msg);
                }
                if (dashf(fpath))
                    file_append(fpath, alert_trade);
                sendalert(id, ALERT_NEW_MAIL);
                if (fp_receipt) {
                    fprintf(fp_receipt, "%-12s: 收到稅後 %d (紅包袋寄送成功\)\n", id, m - mtax);
                    fflush(fp_receipt);
                }
            }
        }
    }

    if (fp_money)
        fclose(fp_money);

    if (fp_receipt) {
        fprintf(fp_receipt, "\n-- 發送作業完成 (%s) --\n", Cdate(&now));
        fclose(fp_receipt);
        sendalert(cuser.userid, ALERT_NEW_MAIL);
    }

    sigprocmask(SIG_SETMASK, &old_set, NULL);

    free(uids);

    outs(ANSI_COLOR(1;33) "交易完成！" ANSI_RESET "\n");
#ifdef USE_RECENTPAY
    move(b_lines - 6, 0);
    clrtobot();
    if (count > 1)
        outs("\n多人紅包詳細收款名單與狀態已寄至您的私人信箱 ([明細] 信件)。\n");
    outs("您可於下列位置找到最近的交易記錄:\n"
         "主選單 => (U)ser個人設定 => (L)MyLogs個人記錄 => "
         "(P)RecentPay最近交易記錄\n");
#endif
    if (count == 1)
        vmsg("交易完成。");
    else
        vmsgf("多人紅包發送完成！共發送給 %d 人，總計支出 $%lld。",
              valid_count, (long long)total_cost);
    return 0;
}

int
p_give(void)
{
    give_money_ui(NULL);
    return -1;
}

int
give_money_ui(const char *userid)
{
    char id[IDLEN + 1];
    struct Vector namelist;
    int recipient = 0, ret;

    vs_hdr("給予" MONEYNAME);
    if (!HasSendMailUserPerm())
        return -1;

    if (!userid || !*userid || strcmp(userid, cuser.userid) == 0)
        userid = NULL;

    Vector_init(&namelist, IDLEN + 1);
    if (userid) {
        usercomplete2("這位幸運兒的id: ", id, userid);
        if (!id[0] || strcasecmp(cuser.userid, id) == 0) {
            vmsg("交易取消!");
            Vector_delete(&namelist);
            return -1;
        }
        if (searchuser(id, id) == 0) {
            vmsg("查無此人!");
            Vector_delete(&namelist);
            return -1;
        }
        Vector_add(&namelist, id);
    } else {
        usercomplete2("這位幸運兒的id (直接按 ENTER 設定多人名單): ", id, NULL);
        if (id[0]) {
            if (strcasecmp(cuser.userid, id) == 0) {
                vmsg("交易取消!");
                Vector_delete(&namelist);
                return -1;
            }
            if (searchuser(id, id) == 0) {
                vmsg("查無此人!");
                Vector_delete(&namelist);
                return -1;
            }
            Vector_add(&namelist, id);
        } else {
            multi_user_list(&namelist, &recipient,
                            "給予" MONEYNAME " - 設定收款名單",
                            "收款人名單：", MULTILIST_EXCLUDE_SELF);
            if (Vector_length(&namelist) <= 0) {
                vmsg("交易取消!");
                Vector_delete(&namelist);
                return -1;
            }
        }
    }

    ret = give_money_ui_list(&namelist);
    Vector_delete(&namelist);
    return ret;
}

// in vars.c
extern const char * const build_remote;
extern const char * const build_origin;
extern const char * const build_hash;
extern const char * const build_time;

int
p_sysinfo(void)
{
    const char *cpuloadstr;
    int             load;
#ifdef DETECT_CLIENT
    extern Fnv32_t  client_code;
#endif

    load = cpuload(NULL);
#ifdef USE_FANCY_LOAD
    // You have to define your own fancy_load function.
    cpuloadstr = fancy_load(load);
#else
    cpuloadstr = (load < 5 ? "良好" : (load < 20 ? "尚可" : "過重"));
#endif

    clear();
    showtitle("系統資訊", BBSNAME);
    move(2, 0);
    prints("您現在位於 " TITLE_COLOR BBSNAME ANSI_RESET " (" MYIP ")\n"
	   "系統負載: %s\n"
	   "線上人數: %d/%d\n"
#ifdef DETECT_CLIENT
	   "ClientCode: %8.8X\n"
#endif
	   "起始時間: %s\n"
	   "編譯時間: %s\n",
	   cpuloadstr, SHM->UTMPnumber,
#ifdef DYMAX_ACTIVE
	   // XXX check the related logic in mbbsd.c
	   (SHM->GV2.e.dymaxactive > 2000 && SHM->GV2.e.dymaxactive < MAX_ACTIVE) ?
	    SHM->GV2.e.dymaxactive : MAX_ACTIVE,
#else
	   MAX_ACTIVE,
#endif
#ifdef DETECT_CLIENT
	   client_code,
#endif
           Cdatelite(&start_time),
	   build_time);
    if (*build_remote) {
      prints("編譯版本: %s %s %s\n", build_remote, build_origin, build_hash);
    }

#ifdef REPORT_PIAIP_MODULES
    outs("\n" ANSI_COLOR(1;30)
	    "Modules powered by piaip:\n"
	    "\ttelnet/vkbd protocol, vtuikit, BRC v3, ...\n"
	    "\tpiaip's Common Chat Window (CCW)\n"
#if defined(USE_PIAIP_MORE) || defined(USE_PMORE)
	    "\tpmore (piaip's more) 2007 w/Movie\n"
#endif
#ifdef EDITPOST_SMARTMERGE
	    "\tSmart Merge 修文自動合併\n"
#endif
#if defined(USE_PFTERM)
	    "\t(EXP) pfterm (piaip's flat terminal, Perfect Term)\n"
#endif
#if defined(USE_BBSLUA)
	    "\t(EXP) BBS-Lua\n"
#endif
	    ANSI_RESET
	    );
#endif // REPORT_PIAIP_MODULES

    if (HasUserPerm(PERM_SYSOP)) {
	struct rusage ru;
	char usage[80];
	get_memusage(sizeof(usage), usage);
	getrusage(RUSAGE_SELF, &ru);
	prints("記憶體用量: %s\n", usage);
	prints("CPU 用量:   %ld.%06ldu %ld.%06lds",
	       (long int)ru.ru_utime.tv_sec,
	       (long int)ru.ru_utime.tv_usec,
	       (long int)ru.ru_stime.tv_sec,
	       (long int)ru.ru_stime.tv_usec);
#ifdef CPULIMIT_PER_DAY
	prints(" (limit %d secs per day)", CPULIMIT_PER_DAY);
#endif
	outs("\n特別參數:"
#ifdef CRITICAL_MEMORY
		" CRITICAL_MEMORY"
#endif
#ifdef UTMPD
		" UTMPD"
#endif
#ifdef FROMD
		" FROMD"
#endif
#ifdef USE_MBBSD_CXX
		" CXX"
#endif
		);
    }
    pressanykey();
    return 0;
}

