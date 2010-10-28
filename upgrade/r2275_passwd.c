#include "bbs.h"

/* userec_t before revision 2275 */
typedef struct old_userec_t {
    char    userid[IDLEN + 1];
    char    realname[20];
    char    nickname[24];
    char    passwd[PASSLEN];
    unsigned char   uflag;
    unsigned int    userlevel;
    unsigned short  numlogins;
    unsigned short  numposts;
    time4_t firstlogin;
    time4_t lastlogin;
    char    lasthost[16];
    int     money;
    char    remoteuser[3];           /* 保留 目前沒用到的 */
    char    proverb;
    char    email[50];
    char    address[50];
    char    justify[REGLEN + 1];
    unsigned char   month;
    unsigned char   day;
    unsigned char   year;
    unsigned char   sex;
    unsigned char   state;
    unsigned char   pager;
    unsigned char   invisible;
    unsigned int    exmailbox;
    chicken_t       mychicken;
    time4_t lastsong;
    unsigned int    loginview;
    unsigned char   channel;      /* 動態看板 (unused?) */
    unsigned short  vl_count;     /* ViolateLaw counter */
    unsigned short  five_win;
    unsigned short  five_lose;
    unsigned short  five_tie;
    unsigned short  chc_win;
    unsigned short  chc_lose;
    unsigned short  chc_tie;
    int     mobile;
    char    mind[4];
    char    ident[11];
    unsigned int    uflag2;
    unsigned char   signature;

    unsigned char   goodpost;		/* 評價為好文章數 */
    unsigned char   badpost;		/* 評價為壞文章數 */
    unsigned char   goodsale;		/* 競標 好的評價  */
    unsigned char   badsale;		/* 競標 壞的評價  */
    char    myangel[IDLEN+1];           /* 我的小天使 */
    unsigned short  chess_elo_rating;	/* 象棋等級分 */
    unsigned int    withme;
    char    pad[48];
} old_userec_t;

void transform(userec_t *new, old_userec_t *old)
{
    new->version = PASSWD_VERSION;

    strlcpy(new->userid, old->userid, IDLEN + 1);
    strlcpy(new->realname, old->realname, 20);
    strlcpy(new->nickname, old->nickname, 24);
    strlcpy(new->passwd, old->passwd, PASSLEN);
    new->uflag = old->uflag;
    new->userlevel = old->userlevel;
    new->numlogins = old->numlogins;
    new->numposts = old->numposts;
    new->firstlogin = old->firstlogin;
    new->lastlogin = old->lastlogin;
    strlcpy(new->lasthost, old->lasthost, 16);
    new->money = old->money;
    strlcpy(new->remoteuser, old->remoteuser, 3);
    new->proverb = old->proverb;
    strlcpy(new->email, old->email, 50);
    strlcpy(new->address, old->address, 50);
    strlcpy(new->justify, old->justify, REGLEN + 1);
    new->month = old->month;
    new->day = old->day;
    new->year = old->year;
    new->sex = old->sex;
    new->state = old->state;
    new->pager = old->pager;
    new->invisible = old->invisible;
    new->exmailbox = old->exmailbox;
    new->mychicken = old->mychicken;
    new->lastsong = old->lastsong;
    new->loginview = old->loginview;
    new->channel = old->channel;
    new->vl_count = old->vl_count;
    new->five_win = old->five_win;
    new->five_lose = old->five_lose;
    new->five_tie = old->five_tie;
    new->chc_win = old->chc_win;
    new->chc_lose = old->chc_lose;
    new->chc_tie = old->chc_tie;
    new->mobile = old->mobile;
    memcpy(new->mind, old->mind, 4);
    memset(new->pad0, 0, sizeof(new->pad0));	// ident is not used anymore
    new->uflag2 = old->uflag2;
    new->signature = old->signature;

    new->goodpost = old->goodpost;
    new->badpost = old->badpost;
    new->goodsale = old->goodsale;
    new->badsale = old->badsale;
    strlcpy(new->myangel, old->myangel, IDLEN+1);
    new->chess_elo_rating = old->chess_elo_rating;
    new->withme = old->withme;
    memset(new->pad, 0, sizeof(new->pad));
}

int main(void)
{
    int fd, fdw;
    userec_t new;
    old_userec_t old;

    printf("You're going to convert your .PASSWDS\n");
    printf("The new file will be named .PASSWDS.trans.tmp\n");
    printf("old size of userec_t is %d, and the new one is %d\n", sizeof(old_userec_t), sizeof(userec_t));
/*
    printf("Press any key to continue\n");
    getchar();
*/

    if (chdir(BBSHOME) < 0) {
	perror("chdir");
	exit(-1);
    }

    if ((fd = open(FN_PASSWD, O_RDONLY)) < 0 ||
	 (fdw = open(FN_PASSWD".trans.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0 ) {
	perror("open");
	exit(-1);
    }

    while (read(fd, &old, sizeof(old)) > 0) {
	transform(&new, &old);
	write(fdw, &new, sizeof(new));
    }

    close(fd);
    close(fdw);
    return 0;
}
