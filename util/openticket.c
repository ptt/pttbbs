/*
 * openticket.c - Board lottery payout and announcement utility
 */
#define _UTIL_C_
#include "bbs.h"

#define MAX_ITEM          8
#define MAX_ITEM_LEN      30
#define NARROW_ITEM_WIDTH 10
typedef long long bignum_t;

int sendalert_uid(int uid, int alert)
{
    userinfo_t *uentp = NULL;
    if ((uentp = (userinfo_t *)search_ulistn(uid, 1)))
        uentp->alerts |= alert;
    return 0;
}

static int post_result_file(const char *bname, const char *title, const char *filename, const char *author)
{
    char fname[PATHLEN];
    FILE *fp, *fin;
    int bid;
    fileheader_t fhdr;
    char dirfn[PATHLEN];
    char buf[512];
    time4_t now = (time4_t)time(NULL);

    fin = fopen(filename, "r");
    if (!fin)
        return -1;

    setbpath(fname, bname);
    stampfile(fname, &fhdr);
    fp = fopen(fname, "w");
    if (!fp) {
        fclose(fin);
        return -1;
    }

    fprintf(fp, "作者: %s 看板: %s\n標題: %s\n時間: %s\n\n", author, bname, title, ctime4(&now));
    while (fgets(buf, sizeof(buf), fin))
        fputs(buf, fp);
    fclose(fin);
    fclose(fp);

    STRLCPY(fhdr.title, title);
    STRLCPY(fhdr.owner, author);
    setbfile(dirfn, bname, FN_DIR);
    if (append_fileheader(dirfn, &fhdr) != -1) {
        if ((bid = getbnum(bname)) > 0)
            touchbtotal(bid);
    }
    return 0;
}

static int send_winner_mail(const char *userid, const char *title, const char *body_text)
{
    char dirfn[PATHLEN], fpath[PATHLEN];
    fileheader_t mymail;
    FILE *out;

    sethomepath(fpath, userid);
    stampfile(fpath, &mymail);

    out = fopen(fpath, "w");
    if (out) {
        if (body_text)
            fputs(body_text, out);
        fputc('\n', out);
        fclose(out);
    }

    STRLCPY(mymail.owner, BBSMNAME "彩券");
    STRLCPY(mymail.title, title);
    mymail.filemode = FILE_READ;

    sethomedir(dirfn, userid);
    append_fileheader(dirfn, &mymail);

    int uid = searchuser(userid, NULL);
    if (uid > 0)
        sendalert_uid(uid, ALERT_NEW_MAIL);
    return 0;
}

static int load_ticket_items_data(const char *direct, char betname[MAX_ITEM][MAX_ITEM_LEN], int *pPrice)
{
    char genbuf[PATHLEN];
    FILE *fp;
    int i;
    int count = 0;

    *pPrice = 100;
    snprintf(genbuf, sizeof(genbuf), "%s/" FN_TICKET_ITEMS, direct);
    if ((fp = fopen(genbuf, "r"))) {
        if (fgets(genbuf, sizeof(genbuf), fp) && *genbuf)
            *pPrice = atoi(genbuf);
        for (i = 0; fgets(betname[i], MAX_ITEM_LEN, fp) && i < MAX_ITEM; i++) {
            chomp(betname[i]);
            count++;
        }
        fclose(fp);
    }
    return count;
}

static bignum_t load_records(const char *direct, bignum_t ticket[MAX_ITEM])
{
    char buf[PATHLEN];
    FILE *fp;
    bignum_t total = 0;
    int i;

    snprintf(buf, sizeof(buf), "%s/" FN_TICKET_RECORD, direct);
    if (!(fp = fopen(buf, "r")))
        return 0;
    for (i = 0; i < MAX_ITEM && fscanf(fp, "%lld ", &ticket[i]) == 1; i++)
        total += ticket[i];
    fclose(fp);
    return total;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <board> <winning_bet_number (1..8 or 99)> [bm_userid]\n", argv[0]);
        return 1;
    }

    const char *board_name = argv[1];
    int bet = atoi(argv[2]);
    const char *bm_userid = (argc >= 4) ? argv[3] : "";

    chdir(BBSHOME);
    attach_SHM();
    resolve_boards();

    int bid = getbnum(board_name);
    if (bid <= 0) {
        fprintf(stderr, "Error: board '%s' not found.\n", board_name);
        return 1;
    }
    boardheader_t *bh = getbcache(bid);
    const char *brdname = bh->brdname;

    char path[PATHLEN];
    setbpath(path, brdname);

    char betname[MAX_ITEM][MAX_ITEM_LEN] = {{0}};
    int price = 100;
    int count = load_ticket_items_data(path, betname, &price);
    if (count <= 0) {
        fprintf(stderr, "Error: no ticket items on board '%s'.\n", brdname);
        return 1;
    }

    if ((bet < 1 || bet > count) && bet != 99) {
        fprintf(stderr, "Error: invalid winning bet %d (valid: 1..%d, or 99 for refund).\n", bet, count);
        return 1;
    }

    bignum_t ticket[MAX_ITEM] = {0};
    bignum_t total = load_records(path, ticket);

    char buf[PATHLEN], outcome[PATHLEN];
    setbfile(buf, brdname, FN_TICKET_LOCK);
    FILE *fp_lock = fopen(buf, "r");
    if (!fp_lock) {
        setbfile(buf, brdname, FN_TICKET_END);
        fp_lock = fopen(buf, "r");
        if (!fp_lock) {
            fprintf(stderr, "Error: neither lock file nor end file exists for '%s'.\n", brdname);
            return 1;
        }
    }

    int win_idx = bet - 1; /* 0..count-1, or 98 for cancel/refund */
    if (bet == 99)
        win_idx = 98;

    bignum_t money = 0;
    time4_t now = (time4_t)time(NULL);

    if (win_idx != 98) {
        money = total * price;
        int forBM = (int)(money * 0.0005);
        if (forBM > 500)
            forBM = 500;
        if (forBM > 0 && bm_userid && *bm_userid) {
            int bmuid = searchuser((char *)bm_userid, NULL);
            if (bmuid > 0) {
                int oldm = moneyof(bmuid);
                deumoney(bmuid, forBM);
                int newm = moneyof(bmuid);
                char reason[256];
                snprintf(reason, sizeof(reason), "%s 彩金抽成", brdname);
                char fn_pay[PATHLEN];
                sethomefile(fn_pay, bm_userid, FN_RECENTPAY);
                log_payment(fn_pay, -forBM, oldm, newm, reason, now);
            }
        }
        money = ticket[win_idx] ? (bignum_t)(money * 0.95 / ticket[win_idx]) : 9999999;
    } else {
        money = price;
        int fee = price * 10;
        if (fee > 0 && bm_userid && *bm_userid) {
            int bmuid = searchuser((char *)bm_userid, NULL);
            if (bmuid <= 0 || moneyof(bmuid) < fee) {
                fprintf(stderr, "Error: BM '%s' does not have enough money (%d < %d) for refund fee.\n",
                        bm_userid, bmuid > 0 ? moneyof(bmuid) : 0, fee);
                fclose(fp_lock);
                return 1;
            }
            if (bmuid > 0) {
                int oldm = moneyof(bmuid);
                deumoney(bmuid, -fee);
                int newm = moneyof(bmuid);
                char reason[256];
                snprintf(reason, sizeof(reason), "%s 樂透退費手續費", brdname);
                char fn_pay[PATHLEN];
                sethomefile(fn_pay, bm_userid, FN_RECENTPAY);
                log_payment(fn_pay, fee, oldm, newm, reason, now);
            }
        }
    }

    setbfile(outcome, brdname, FN_TICKET_OUTCOME);
    FILE *fp = fopen(outcome, "w");
    if (fp) {
        int wide = 0;
        fprintf(fp, "樂透說明\n");
        while (fgets(buf, sizeof(buf), fp_lock)) {
            buf[sizeof(buf) - 1] = 0;
            fputs(buf, fp);
        }

        fprintf(fp, "\n下注情況\n");
        for (int i = 0; i < count && !wide; i++) {
            if (stream_width(betname[i]) > NARROW_ITEM_WIDTH || ticket[i] > 999999)
                wide = 1;
        }
        for (int i = 0; i < count; i++) {
            if (i % (wide ? 3 : 4) == 0)
                fputc('\n', fp);
            fprintf(fp, "%d.%-*s: %-7lld%s",
                    i + 1, (wide ? IDLEN : 8), betname[i],
                    ticket[i], wide ? " " : "");
        }
        fputs("\n\n", fp);

        if (win_idx != 98) {
            fprintf(fp,
                    "開獎時間: %s\n"
                    "開獎結果: %s\n"
                    "下注總額: %lld\n"
                    "中獎比例: %lld張/%lld張  (%f)\n"
                    "每張中獎彩券可得 %lld " MONEYNAME "\n\n",
                    Cdatelite(&now), betname[win_idx],
                    total * price,
                    ticket[win_idx], total,
                    total ? (double)ticket[win_idx] / total : (double)0,
                    money);

            fprintf(fp, "%s 開出:%s 總額:%lld 彩金/張:%lld 機率:%1.2f\n\n",
                    Cdatelite(&now), betname[win_idx], total * price, money,
                    total ? (double)ticket[win_idx] / total : (double)0);
        } else {
            fprintf(fp, "樂透取消退回: %s\n\n", Cdatelite(&now));
        }
    }
    fclose(fp_lock);

    /* Pay winners */
    setbfile(buf, brdname, FN_TICKET_USER);
    FILE *fp1 = fopen(buf, "r");
    if ((win_idx == 98 || ticket[win_idx]) && fp1) {
        int mybet, num;
        char userid[IDLEN + 1];

        while (fscanf(fp1, "%s %d %d\n", userid, &mybet, &num) != EOF) {
            if (win_idx == 98 && mybet >= 0 && mybet < count) {
                if (fp)
                    fprintf(fp, "%-*s 買了 %3d 張 %s, 退回 %5lld " MONEYNAME "\n",
                            IDLEN, userid, num, betname[mybet], money * num);
                snprintf(buf, sizeof(buf), "%s 樂透退費! $ %lld", brdname, money * num);
            } else if (mybet == win_idx) {
                if (fp)
                    fprintf(fp, "恭喜 %-*s 買了 %3d 張 %s, 獲得 %5lld " MONEYNAME "\n",
                            IDLEN, userid, num, betname[mybet], money * num);
                snprintf(buf, sizeof(buf), "%s 中獎咧! $ %lld", brdname, money * num);
            } else {
                if (fp)
                    fprintf(fp, "     %-*s 買了 %3d 張 %s\n",
                            IDLEN, userid, num, betname[mybet]);
                continue;
            }

            int uid = searchuser(userid, NULL);
            if (uid <= 0)
                continue;

            int oldm = moneyof(uid);
            deumoney(uid, money * num);
            int newm = moneyof(uid);
            char reason[256];
            snprintf(reason, sizeof(reason), "彩券中獎 (%s x %d)", betname[mybet], num);
            char fn_pay[PATHLEN];
            sethomefile(fn_pay, userid, FN_RECENTPAY);
            log_payment(fn_pay, -(money * num), oldm, newm, reason, now);

            send_winner_mail(userid, buf, "恭喜中獎！");
        }
        fclose(fp1);
    }

    if (fp) {
        fprintf(fp, "\n--\n※ 開獎站 : %s (%s)\n", BBSNAME, MYHOSTNAME);
        fclose(fp);
    }

    /* Post announcements */
    if (win_idx != 98)
        snprintf(buf, sizeof(buf), TN_ANNOUNCE " %s 樂透開獎", brdname);
    else
        snprintf(buf, sizeof(buf), TN_ANNOUNCE " %s 樂透取消", brdname);

    post_result_file(brdname, buf, outcome, "[彩券]");
    post_result_file("Record", buf + 7, outcome, "[馬路探子]");
    post_result_file(BN_SECURITY, buf + 7, outcome, "[馬路探子]");

    /* Backup bets log to Security */
    setbfile(buf, brdname, FN_TICKET_USER);
    post_result_file(BN_SECURITY, brdname, buf, "[下注紀錄]");
    unlink(buf);

    setbfile(buf, brdname, FN_TICKET_RECORD);
    unlink(buf);

    setbfile(buf, brdname, FN_TICKET_LOCK);
    unlink(buf);

    setbfile(buf, brdname, FN_TICKET_END);
    unlink(buf);

    log_filef("log/openticket.log", "[%s] Board %s ticket opened by %s: bet=%d, total=%lld\n",
              Cdatelite(&now), brdname, bm_userid, bet, total);

    return 0;
}
