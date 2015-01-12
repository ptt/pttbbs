/* 開獎的 utility */
#define _UTIL_C_
#include "bbs.h"

#define MAX_ITEM	8	//最大 賭項(item) 個數
#define MAX_ITEM_LEN	30	//最大 每一賭項名字長度
#define MAX_DISP_LEN    8       //每項顯示最大長度
#define PRICE           100     //Default price
#define MAX_DES 7		/* 最大保留獎數 */
#define VALID_UNUM(u) ((u) > 0 && (u) < MAX_USERS)

#if (MAX_ITEM_LEN < IDLEN)
# error MAX_ITEM_LEN must be greater than IDLEN.
#endif

const char *FN_LOGFILE = "log/openticket.log";

const char unique_betnames[MAX_ITEM][MAX_ITEM_LEN] = {
    "Ptt", "Jaky",  "Action",  "Heat",
    "DUNK", "Jungo", "waiting", "wofe",
};

#if (MAX_USERS > 10000)
#define USE_SERIOUS_ID_CHECK_FOR_TICKETS
#endif

int
sendalert_uid(int uid, int alert){
    userinfo_t     *uentp = NULL;

    if ((uentp = (userinfo_t *) search_ulistn(uid, 1)))
        uentp->alerts |= alert;
    return 0;
}

int load_ticket_items(const char *filename, int n_items,
                      int *pPrice, char items[MAX_ITEM][MAX_ITEM_LEN]) {
    FILE *fp = fopen(filename, "rt");
    char genbuf[MAX_ITEM_LEN] = "";
    int i;

    if (!fp)
        return 0;

    if (fgets(genbuf, sizeof(genbuf), fp) && *genbuf) {
        if (pPrice)
            *pPrice = atoi(genbuf);
    }

    for (i = 0; fgets(items[i], MAX_ITEM_LEN, fp) && i < n_items; i++) {
        chomp(items[i]);
    }
    fclose(fp);
    return 1;
}

int save_ticket_items(const char *filename, int n_items,
                      int price, char items[MAX_ITEM][MAX_ITEM_LEN]) {
    FILE *fp = fopen(filename, "wt");
    int i;
    if (!fp)
        return 0;
    fprintf(fp, "%d\n", price);
    for (i = 0; i < n_items; i++) {
        fprintf(fp, "%s\n", items[i]);
    }
    fclose(fp);
    return 1;
}

void create_new_items(char items[MAX_ITEM][MAX_ITEM_LEN],
                      char ref_items[MAX_ITEM][MAX_ITEM_LEN],
                      int n_items) {
    int i;
    int unum = 0;
    time4_t now = (time4_t)time(NULL);

    // initialize names by duplicating
    for (i = 0; i < n_items; i++) {
        strlcpy(items[i], ref_items[i], MAX_ITEM_LEN);
    }

    // find last user index 
    for (i = n_items - 1; i >= 0; i--) {
        unum = searchuser(items[i], NULL);
        if (VALID_UNUM(unum)) {
            unum++;
            break;
        }
    }
    log_filef(FN_LOGFILE, LOG_CREAT,
             "%s last user index: %d (%s)\n",
             Cdatelite(&now), unum,
             VALID_UNUM(unum) ? SHM->userid[unum - 2] : "-unkonwn-");

    // pickup new names
    for (i = 0; i < n_items; i++) {
        // find next valid unum
        for (; VALID_UNUM(unum) && !*SHM->userid[unum - 1]; unum++) {
            // find next one
        }
        if (!VALID_UNUM(unum)) {
            strlcpy(items[i], unique_betnames[i], MAX_ITEM_LEN);
            // rewind
            unum = 1;
            log_filef(FN_LOGFILE, LOG_CREAT,
                      "%s rewind user index.\n", Cdatelite(&now));
            continue;
        }
#ifdef USE_SERIOUS_ID_CHECK_FOR_TICKETS
        {
            const int need_exactly_perm = PERM_LOGINOK | PERM_POST;
            userec_t u = {0};
            passwd_query(unum, &u);
            if ((u.userlevel & need_exactly_perm) != need_exactly_perm ||
                u.numposts < 100 || u.numlogindays < 365) {
                i--;
                unum++;
                continue;
            }
            log_filef(FN_LOGFILE, LOG_CREAT, "%d: [unum:%d] "
                      "%-*s (lvl=0x%08X, %d days, %d posts, shm=%s)\n",
                      i + 1, unum, IDLEN, u.userid, u.userlevel,
                      u.numlogindays, u.numposts, SHM->userid[unum - 1]);
        }
#endif

        strlcpy(items[i], SHM->userid[unum - 1], MAX_ITEM_LEN);
        unum++;
    }
}
    

int main()
{
    int  money, bet, n, total = 0;
    int  ticket[MAX_ITEM] = {0};
    FILE *fp;
    char des[MAX_DES][200] = {"", "", "", ""};

    char newbetname[MAX_ITEM][MAX_ITEM_LEN] = {{0}};
    char betname[MAX_ITEM][MAX_ITEM_LEN] = {
        "Ptt", "Jaky",  "Action",  "Heat",
        "DUNK", "Jungo", "waiting", "wofe",
    };

    time4_t now = (time4_t)time(NULL);

    nice(10);

    chdir(BBSHOME);  // for sethomedir
    attach_SHM();

    load_ticket_items("etc/" FN_TICKET_ITEMS, MAX_ITEM, NULL, betname);
    create_new_items(newbetname, betname, MAX_ITEM);

    rename("etc/" FN_TICKET_RECORD, "etc/" FN_TICKET_RECORD ".tmp");
    rename("etc/" FN_TICKET_USER,   "etc/" FN_TICKET_USER   ".tmp");

    save_ticket_items("etc/" FN_TICKET_ITEMS, MAX_ITEM, PRICE, newbetname);

    if (!(fp = fopen("etc/"FN_TICKET_RECORD ".tmp", "r")))
	return 0;

    fscanf(fp, "%9d %9d %9d %9d %9d %9d %9d %9d\n",
	   &ticket[0], &ticket[1], &ticket[2], &ticket[3],
	   &ticket[4], &ticket[5], &ticket[6], &ticket[7]);
    for (n = 0; n < 8; n++)
	total += ticket[n];
    fclose(fp);

    if (!total)
	return 0;

    if((fp = fopen("etc/" FN_TICKET , "r")))
    {
	for (n = 0; n < MAX_DES && fgets(des[n], 200, fp); n++) ;
	fclose(fp);
    }

    /* 現在開獎號碼並沒用到 random function.
     * 小站的 UTMPnumber 可視為定值, 且 UTMPnumber 預設一秒才更新一次
     * 開站一段時間的開獎 pid 應該無法預測.
     * 若是小站當站開獎前開站, 則有被猜中的可能 */
    bet = (SHM->UTMPnumber + getpid()) % MAX_ITEM;

    log_filef(FN_LOGFILE, LOG_CREAT, "%s bet=%d\n", Cdatelite(&now), bet);

    money = ticket[bet] ? total * 95 / ticket[bet] : 9999999;
    if((fp = fopen("etc/" FN_TICKET, "w")))
    {
	if (des[MAX_DES - 1][0])
	    n = 1;
	else
	    n = 0;

	for (; n < MAX_DES && des[n][0] != 0; n++)
	{
	    fputs(des[n], fp);
	}

        printf("\n\n開獎時間: %s\n彩券項目:\n", Cdatelite(&now));
        for (n = 0; n < MAX_ITEM; n++) {
            printf(" %d.%-*s", n + 1, IDLEN, betname[n]);
            if ((n + 1) % (MAX_ITEM / 2) == 0)
                printf("\n");
        }

	printf("\n"
               "開獎結果: %d. %s\n\n"
	       "下注總額: %d00\n"
	       "中獎比例: %d張/%d張  (%f)\n"
	       "每張中獎彩票可得 %d " MONEYNAME "\n\n",
	       bet + 1, betname[bet], total, ticket[bet], total,
	       (float) ticket[bet] / total, money);

	fprintf(fp, "%s 開出:%d.%s 總額:%d00 彩金/張:%d 機率:%1.2f\n",
		Cdatelite(&now), bet + 1, betname[bet], total, money,
		(float) ticket[bet] / total);
	fclose(fp);

    }

    if (ticket[bet] && (fp = fopen("etc/" FN_TICKET_USER ".tmp", "r")))
    {
	int mybet, num, uid;
	char userid[20], genbuf[200];
	fileheader_t mymail;

	while (fscanf(fp, "%s %d %d\n", userid, &mybet, &num) != EOF)
	{
	    if (mybet == bet)
	    {
                int oldm, newm;
		printf("恭喜 %-*s 買了%9d 張 %s, 獲得 %d " MONEYNAME "\n",
                       IDLEN, userid, num, betname[mybet], money * num);
                if((uid=searchuser(userid, userid))==0 ||
                    !is_validuserid(userid)) 
                    continue;

                oldm = moneyof(uid);
		deumoney(uid, money * num);
                newm = moneyof(uid);
                {
                    char reason[256];
                    sprintf(reason, "彩券中獎 (%s x %d)", betname[mybet], num);
                    sethomefile(genbuf, userid, FN_RECENTPAY);
                    log_payment(genbuf, -money * num, oldm, newm, reason, now);
                }
                sethomepath(genbuf, userid);
		stampfile(genbuf, &mymail);
		strcpy(mymail.owner, BBSNAME);
		sprintf(mymail.title, "[%s] 中獎囉! $ %d", Cdatelite(&now), money * num);
		unlink(genbuf);
		Link("etc/ticket", genbuf);
                sethomedir(genbuf, userid);
		append_record(genbuf, &mymail, sizeof(mymail));
                sendalert_uid(uid, ALERT_NEW_MAIL);
	    } else {
                printf("     %-*s 買了%9d 張 %s\n", IDLEN, userid, num, betname[mybet]);
            }
	}
    }
    unlink("etc/" FN_TICKET_RECORD ".tmp");
    unlink("etc/" FN_TICKET_USER ".tmp");
    return 0;
}
