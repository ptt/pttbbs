/* $Id$ */
/* 開獎的 utility */
#define _UTIL_C_
#include "bbs.h"

static char *betname[8] = {"Ptt", "Jaky",  "Action",  "Heat",
			   "DUNK", "Jungo", "waiting", "wofe"};

#define MAX_DES 7		/* 最大保留獎數 */

int
sendalert_uid(int uid, int alert){
    userinfo_t     *uentp = NULL;
    int             n, i;

    n = count_logins(uid, 0);
    for (i = 1; i <= n; i++)
	if ((uentp = (userinfo_t *) search_ulistn(uid, i)))
	    uentp->alerts |= alert;

    return 0;
}

int main(int argc, char **argv)
{
    int money, bet, n, total = 0, ticket[8] =
    {0, 0, 0, 0, 0, 0, 0, 0};
    FILE *fp;
    time4_t now = (time4_t)time(NULL);
    char des[MAX_DES][200] =
    {"", "", "", ""};

    nice(10);
    attach_SHM();
    if(passwd_init())
	exit(1);

    chdir(BBSHOME);  // for sethomedir
    
    rename(BBSHOME "/etc/" FN_TICKET_RECORD,
           BBSHOME "/etc/" FN_TICKET_RECORD ".tmp");
    rename(BBSHOME "/etc/" FN_TICKET_USER,
           BBSHOME "/etc/" FN_TICKET_USER ".tmp");

    if (!(fp = fopen(BBSHOME "/etc/"FN_TICKET_RECORD ".tmp", "r")))
	return 0;
    fscanf(fp, "%9d %9d %9d %9d %9d %9d %9d %9d\n",
	   &ticket[0], &ticket[1], &ticket[2], &ticket[3],
	   &ticket[4], &ticket[5], &ticket[6], &ticket[7]);
    for (n = 0; n < 8; n++)
	total += ticket[n];
    fclose(fp);

    if (!total)
	return 0;

    if((fp = fopen(BBSHOME "/etc/" FN_TICKET , "r")))
    {
	for (n = 0; n < MAX_DES && fgets(des[n], 200, fp); n++) ;
	fclose(fp);
    }

    /* 現在開獎號碼並沒用到 random function.
     * 小站的 UTMPnumber 可視為定值, 且 UTMPnumber 預設一秒才更新一次
     * 開站一段時間的開獎 pid 應該無法預測.
     * 若是小站當站開獎前開站, 則有被猜中的可能 */
    attach_SHM();
    bet = (SHM->UTMPnumber+getpid()) % 8;



    money = ticket[bet] ? total * 95 / ticket[bet] : 9999999;

    if((fp = fopen(BBSHOME "/etc/" FN_TICKET, "w")))
    {
	if (des[MAX_DES - 1][0])
	    n = 1;
	else
	    n = 0;

	for (; n < MAX_DES && des[n][0] != 0; n++)
	{
	    fputs(des[n], fp);
	}

	printf("\n\n開獎時間： %s \n\n"
	       "開獎結果： %s \n\n"
	       "下注總金額： %d00 元 \n"
	       "中獎比例： %d張/%d張  (%f)\n"
	       "每張中獎彩票可得 %d 枚" MONEYNAME "幣 \n\n",
	       Cdatelite(&now), betname[bet], total, ticket[bet], total,
	       (float) ticket[bet] / total, money);

	fprintf(fp, "%s 賭盤開出:%s 總金額:%d00 元 獎金/張:%d 元 機率:%1.2f\n",
		Cdatelite(&now), betname[bet], total, money,
		(float) ticket[bet] / total);
	fclose(fp);

    }

    if (ticket[bet] && (fp = fopen(BBSHOME "/etc/" FN_TICKET_USER ".tmp", "r")))
    {
	int mybet, num, uid;
	char userid[20], genbuf[200];
	fileheader_t mymail;

	while (fscanf(fp, "%s %d %d\n", userid, &mybet, &num) != EOF)
	{
	    if (mybet == bet)
	    {
                int oldm, newm;
		printf("恭喜 %-15s買了%9d 張 %s, 獲得 %d 枚" MONEYNAME "幣\n"
		       ,userid, num, betname[mybet], money * num);
                if((uid=searchuser(userid, userid))==0 ||
                    !is_validuserid(userid)) 
                    continue;

                oldm = moneyof(uid);
		deumoney(uid, money * num);
                newm = moneyof(uid);
                {
                    char reason[256];
                    sprintf(reason, "賭盤中獎 (%s x %d)", betname[mybet], num);
                    sethomefile(genbuf, userid, FN_RECENTPAY);
                    log_payment(genbuf, -money * num, oldm, newm, reason, now);
                }
                sethomepath(genbuf, userid);
		stampfile(genbuf, &mymail);
		strcpy(mymail.owner, BBSNAME);
		sprintf(mymail.title, "[%s] 中獎囉! $ %d", Cdatelite(&now), money * num);
		unlink(genbuf);
		Link(BBSHOME "/etc/ticket", genbuf);
		sprintf(genbuf, BBSHOME "/home/%c/%s/.DIR", userid[0], userid);
		append_record(genbuf, &mymail, sizeof(mymail));
                sendalert_uid(uid, ALERT_NEW_MAIL);
	    } else {
		printf("     %-15s買了%9d 張 %s\n" ,userid, num, betname[mybet]);
            }
	}
    }
    unlink(BBSHOME "/etc/" FN_TICKET_RECORD ".tmp");
    unlink(BBSHOME "/etc/" FN_TICKET_USER ".tmp");
    return 0;
}
