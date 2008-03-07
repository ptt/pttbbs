#include "bbs.h"

// warning: if you changed userec_t, you must use the old version.

typedef struct {
    unsigned int    version;	/* version number of this sturcture, we
    				 * use revision number of project to denote.*/

    char    userid[IDLEN + 1];	/* ID */
    char    realname[20];	/* 真實姓名 */
    char    nickname[24];	/* 暱稱 */
    char    passwd[PASSLEN];	/* 密碼 */
    char    padx;
    unsigned int    uflag;	/* 習慣1 , see uflags.h */
    unsigned int    uflag2;	/* 習慣2 , see uflags.h */
    unsigned int    userlevel;	/* 權限 */
    unsigned int    numlogins;	/* 上站次數 */
    unsigned int    numposts;	/* 文章篇數 */
    time4_t firstlogin;		/* 註冊時間 */
    time4_t lastlogin;		/* 最近上站時間 */
    char    lasthost[16];	/* 上次上站來源 */
    int     money;		/* Ptt幣 */
    char    remoteuser[3];	/* 保留 目前沒用到的 */
    char    proverb;		/* 座右銘 (unused) */
    char    email[50];		/* Email */
    char    address[50];	/* 住址 */
    char    justify[REGLEN + 1];    /* 審核資料 */
    unsigned char   month;	/* 生日 月 */
    unsigned char   day;	/* 生日 日 */
    unsigned char   year;	/* 生日 年 */
    unsigned char   sex;	/* 性別 */
    unsigned char   state;	/* TODO unknown (unused ?) */
    unsigned char   pager;	/* 呼叫器狀態 */
    unsigned char   invisible;	/* 隱形狀態 */
    char    padxx[2];
    unsigned int    exmailbox;	/* 購買信箱數 TODO short 就夠了 */
    chicken_t       mychicken;	/* 寵物 */
    time4_t lastsong;		/* 上次點歌時間 */
    unsigned int    loginview;	/* 進站畫面 */
    unsigned char   channel;	/* TODO unused */
    char    padxxx;
    unsigned short  vl_count;	/* 違法記錄 ViolateLaw counter */
    unsigned short  five_win;	/* 五子棋戰績 勝 */
    unsigned short  five_lose;	/* 五子棋戰績 敗 */
    unsigned short  five_tie;	/* 五子棋戰績 和 */
    unsigned short  chc_win;	/* 象棋戰績 勝 */
    unsigned short  chc_lose;	/* 象棋戰績 敗 */
    unsigned short  chc_tie;	/* 象棋戰績 和 */
    int     mobile;		/* 手機號碼 */
    char    mind[4];		/* 心情 not a null-terminate string */
    unsigned short  go_win;	/* 圍棋戰績 勝 */
    unsigned short  go_lose;	/* 圍棋戰績 敗 */
    unsigned short  go_tie;	/* 圍棋戰績 和 */
    char    pad0[5];		/* 從前放 ident 身份證字號，現在可以拿來做別的事了，
				   不過最好記得要先清成 0 */
    unsigned char   signature;	/* 慣用簽名檔 */

    unsigned char   goodpost;	/* 評價為好文章數 */
    unsigned char   badpost;	/* 評價為壞文章數 */
    unsigned char   goodsale;	/* 競標 好的評價  */
    unsigned char   badsale;	/* 競標 壞的評價  */
    char    myangel[IDLEN+1];	/* 我的小天使 */
    char    pad2;
    unsigned short  chess_elo_rating;	/* 象棋等級分 */
    unsigned int    withme;	/* 我想找人下棋，聊天.... */
    time4_t timeremovebadpost;  /* 上次刪除劣文時間 */
    time4_t timeviolatelaw; /* 被開罰單時間 */
    char    pad[28];
} old_userec_t;

int main()
{
    FILE *fp = fopen(FN_PASSWD, "rb"), *fp2 = NULL;
    char fn[PATHLEN];
    old_userec_t u;
    time4_t now=0;
    int i, cusr;

    if (!fp) {
	printf("cannot load password file. abort.\n");
	return -1;
    }
    now = time(NULL);

    i = 0; cusr = 0;
    while (fread(&u, sizeof(u), 1, fp) > 0)
    {
	/*
	cusr ++;
	if (cusr % (MAX_USERS / 100) == 0)
	{
	    fprintf(stderr, "%3d%%\r", cusr/(MAX_USERS/100));
	}
	*/
	if (!u.userid[0])
	    continue;

	// if dead and no records, // not possible to revive, 
	// then abort.
	if (!u.mychicken.name[0] &&
	     u.mychicken.cbirth == 0)
	     // u.lastvisit + 86400*7 < now
	    continue;

	// now, write this data to user home.
	// sethomefile(fn, u.userid, FN_CHICKEN);
	sprintf(fn, BBSHOME "/home/%c/%s/" FN_CHICKEN,
		u.userid[0], u.userid);

	// ignore created entries (if you are running live upgrade)
	if (access(fn, R_OK) == 0)//dashf(fn))
	{
	    // printf("\nfound created entry, ignore: %s\n", u.userid);
	    continue;
	}

	/*
	i++;
	continue;
	*/

	fp2 = fopen(fn, "wb");
	if (!fp2)
	{
	    printf("ERROR: cannot create chicken data: %s.\n", u.userid);
	    continue;
	}
	if (fwrite(&u.mychicken, sizeof(u.mychicken), 1, fp2) < 1)
	{
	    printf("ERROR: cannot write chicken data: %s.\n", u.userid);
	    unlink(fn);
	}
	else
	{
	    // printf("Transferred chicken data OK: %s.\n", u.userid);
	    i++;
	}
	fclose(fp2); fp2 = NULL;
    }
    fclose(fp);
    printf("\ntotal %d users updated.\n", i);
    return 0;
}
