/* $Id: openticket.c,v 1.7 2002/06/12 12:27:04 lwms Exp $ */
/* 開獎的 utility */
#include "bbs.h"

static char *betname[8] = {"Ptt", "Jaky",  "Action",  "Heat",
			   "DUNK", "Jungo", "waiting", "wofe"};

#define MAX_DES 7		/* 最大保留獎數 */

extern userec_t xuser;

int Link(char *src, char *dst)
{
    char cmd[200];

    if (link(src, dst) == 0)
	return 0;

    sprintf(cmd, "/bin/cp -R %s %s", src, dst);
    return system(cmd);
}

char *
 Cdatelite(clock)
time_t *clock;
{
    static char foo[18];
    struct tm *mytm = localtime(clock);

    strftime(foo, 18, "%D %T", mytm);
    return (foo);
}


int main()
{
    int money, bet, n, total = 0, ticket[8] =
    {0, 0, 0, 0, 0, 0, 0, 0};
    FILE *fp;
    time_t now = time(NULL);
    char des[MAX_DES][200] =
    {"", "", "", ""};

    if(passwd_mmap())
	exit(1);
    
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

  srandom(33); //   固定一個  seed 但盡量要避免跟別人的seed同
 
  for( n = (now / (60*60*3)) - 62820; n >0; n--) random();
 

/* 
 * 正確的random number generator的用法
 * 是用同一個 seed後取 第一個 第二個 地三個.... 數
 * srand() 設完seed後
 * 每呼叫一次rand()就取下一個數
 *
 * 但因為我們沒有記錄上次取到第幾個
 * 所以用每增四小時()就多取一次 => now / (60*60*4) (每五小時開一次獎)
 * (減 61820 是減少 loop 數)
 *
 * 本來是用srand(time(0))  不是正確的用法
 * 因為開獎時間有規率 所以會被找出規律
 *
 *                                     ~Ptt
 */

 bet=random() % 8;
 

    resolve_utmp();
    bet = SHM->UTMPnumber % 8;

/*

 * 在C中 srand 跟 srandom 一樣 rand 跟 random 一樣
 * 不同的是 rand   是傳回一個 double 給非整數的亂數用
 *          random 是傳回一個 int    給整數的亂數用
 *
 * 若要以rand inplement 整數的亂數 要注意以下 (man page中有)
 *
 *     In Numerical Recipes in C: The Art of Scientific Computing
 *     (William  H.  Press, Brian P. Flannery, Saul A. Teukolsky,
 *     William T.  Vetterling;  New  York:  Cambridge  University
 *     Press,  1990 (1st ed, p. 207)), the following comments are
 *     made:
 *            "If you want to generate a random integer between 1
 *            and 10, you should always do it by
 *
 *                   j=1+(int) (10.0*rand()/(RAND_MAX+1.0));
 *
 *            and never by anything resembling
 *
 *                   j=1+((int) (1000000.0*rand()) % 10);
 *
 *            (which uses lower-order bits)."
 *
 *     Random-number  generation is a complex topic.  The Numeri-
 *     cal Recipes in C book (see reference  above)  provides  an
 *     excellent discussion of practical random-number generation
 *     issues in Chapter 7 (Random Numbers).
 *                                                     ~ Ptt
 */


    money = ticket[bet] ? total * 95 / ticket[bet] : 9999999;

    if((fp = fopen(BBSHOME "/etc/" FN_TICKET, "w")))
    {
	if (des[MAX_DES - 1][0])
	    n = 1;
	else
	    n = 0;

	for (; n < MAX_DES && des[n][0] != 0; n++)
	{
	    fprintf(fp, des[n]);
	}

	printf("\n\n開獎時間： %s \n\n"
	       "開獎結果： %s \n\n"
	       "下注總金額： %d00 元 \n"
	       "中獎比例： %d張/%d張  (%f)\n"
	       "每張中獎彩票可得 %d 枚Ｐ幣 \n\n",
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
		printf("恭喜 %-15s買了%9d 張 %s, 獲得 %d 枚Ｐ幣\n"
		       ,userid, num, betname[mybet], money * num);
                if((uid=getuser(userid))==0) continue;
		deumoney(uid, money * num);
		sprintf(genbuf, BBSHOME "/home/%c/%s", userid[0], userid);
		stampfile(genbuf, &mymail);
		strcpy(mymail.owner, BBSNAME);
		sprintf(mymail.title, "[%s] 中獎囉! $ %d", Cdatelite(&now), money * num);
		unlink(genbuf);
		Link(BBSHOME "/etc/ticket", genbuf);
		sprintf(genbuf, BBSHOME "/home/%c/%s/.DIR", userid[0], userid);
		append_record(genbuf, &mymail, sizeof(mymail));
	    }
	}
    }
    unlink(BBSHOME "/etc/" FN_TICKET_RECORD ".tmp");
    unlink(BBSHOME "/etc/" FN_TICKET_USER ".tmp");
    return 0;
}
