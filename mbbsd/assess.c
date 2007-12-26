/* $Id$ */
#include "bbs.h"

#ifdef ASSESS

/* do (*num) + n, n is integer. */
inline static void inc(unsigned char *num, int n)
{
    if (n >= 0 && SALE_MAXVALUE - *num <= n)
	(*num) = SALE_MAXVALUE;
    else if (n < 0 && *num < -n)
	(*num) = 0;
    else
	(*num) += n;
}

#define modify_column(_attr) \
int inc_##_attr(const char *userid, int num) \
{ \
    userec_t xuser; \
    int uid = getuser(userid, &xuser);\
    if( uid > 0 ){ \
	inc(&xuser._attr, num); \
	passwd_update(uid, &xuser); \
	return xuser._attr; }\
    return 0;\
}

modify_column(goodpost); /* inc_goodpost */
modify_column(badpost);  /* inc_badpost */
modify_column(goodsale); /* inc_goodsale */
modify_column(badsale);  /* inc_badsale */

#if 0 //unused function
void set_assess(const char *userid, unsigned char num, int type)
{
    userec_t xuser;
    int uid = getuser(userid, &xuser);
    if(uid<=0) return;
    switch (type){
	case GOODPOST:
	    xuser.goodpost = num;
	    break;
	case BADPOST:
	    xuser.badpost = num;
	    break;
	case GOODSALE:
	    xuser.goodsale = num;
	    break;
	case BADSALE:
	    xuser.badsale = num;
	    break;
    }
    passwd_update(uid, &xuser);
}
#endif

// how long is AID? see read.c...
#ifndef AIDC_LEN
#define AIDC_LEN (20)
#endif // AIDC_LEN

#define MAXGP (100)

int 
u_fixgoodpost(void)
{
    char endinput = 0, newgp = 0;
    int bid;
    char bname[IDLEN+1];
    char xaidc[AIDC_LEN+1];

    aidu_t gpaids[MAXGP+1];
    int    gpbids[MAXGP+1];
    int    cgps = 0;

    clear();
    stand_title("自動優文修正程式");
    
    outs("開始修正優文之前，有些功\課要麻煩您先查好：\n\n"
	 "請先找到你所有的優文文章的看版與" AID_DISPLAYNAME "\n"
	 AID_DISPLAYNAME "的查詢方法是在該篇文章前面按下大寫 Q 。\n"
	 "查好後請把這些資料放在手邊，等下會請您輸入。\n\n");
    outs("如果一切都準備好了，請按下 y 開始，或其它任意鍵跳出。\n\n");
    if (getans("優文的資料都查好了嗎？") != 'y')
    {
	vmsg("跳出修正程式。");
	return 0;
    }
    while (!endinput && newgp < MAXGP)
    {
	int y, x = 0;
	boardheader_t *bh = NULL;

	move(1, 0); clrtobot();
	outs("請依序輸入優文資訊，全部完成後按 ENTER 即可停止。\n");

	move(b_lines-2, 0); clrtobot();
	prints("目前已確認優文數目: %d" ANSI_RESET "\n\n", newgp);

	if (!getdata(5, 0, "請輸入優文文章所在看板名稱: ", 
		    bname, sizeof(bname), DOECHO))
	{
	    move(5, 0); 
	    if (getans(ANSI_COLOR(1;33)"確定全部輸入完成了嗎？ "
			ANSI_RESET "[y/N]: ") != 'y')
		continue;
	    endinput = 1; break;
	}
	move (6, 0);
	outs("確認看板... ");
	if (bname[0] == '\0' || !(bid = getbnum(bname)))
	{
	    outs(ANSI_COLOR(1;31) "看板不存在！");
	    vmsg("請重新輸入。");
	    continue;
	}
	assert(0<=bid-1 && bid-1<MAX_BOARD);
	bh = getbcache(bid);
	strlcpy(bname, bh->brdname, sizeof(bname));
	prints("已找到看板 --> %s\n", bname);
	getyx(&y, &x);

	// loop AID query
	while (newgp < MAXGP)
	{
	    int n;
	    int fd;
	    char dirfile[PATHLEN];
	    char *sp;
	    aidu_t aidu = 0;
	    fileheader_t fh;

	    move(y, 0); clrtobot();
	    move(b_lines-2, 0); clrtobot();
	    prints("目前已確認優文數目: %d" ANSI_RESET "\n\n", newgp);

	    if (getdata(y, 0, "請輸入" AID_DISPLAYNAME ": #",
		    xaidc, AIDC_LEN, DOECHO) == 0)
		break;

	    sp = xaidc;
	    while(*sp == ' ') sp ++;
	    if(*sp == '#') sp ++;

	    if((aidu = aidc2aidu(sp)) <= 0)
	    {
		outs(ANSI_COLOR(1;31) AID_DISPLAYNAME "格式不正確！");
		vmsg("請重新輸入。");
		continue;
	    }

	    // check repeated input of same board+AID.
	    for (n = 0; n < cgps; n++)
	    {
		if (gpaids[n] == aidu && gpbids[n] == bid)
		{
		    vmsg("您已輸入過此優文了，請重新輸入。");
		    aidu = 0;
		    break;
		}
	    }

	    if (aidu <= 0)
		continue;
	    
	    // find aidu in board
	    n = -1;
	    // see read.c, search .bottom first.
	    if (n < 0)
	    {
		outs("搜尋置底文章...");
		setbfile(dirfile, bname, FN_DIR ".bottom");
		n = search_aidu(dirfile, aidu);
	    }
	    if (n < 0) {
		// search board
		outs("未找到。\n搜尋看板文章..");
		setbfile(dirfile, bname, FN_DIR);
		n = search_aidu(dirfile, aidu);
	    }
	    if (n < 0)
	    {
		// search digest
		outs("未找到。\n搜尋文摘..");
		setbfile(dirfile, currboard, fn_mandex);
		n = search_aidu(dirfile, aidu);
	    } 
	    if (n < 0) 
	    {
		// search failed...
		outs("未找到\n" ANSI_COLOR(1;31) "找不到文章！");
		vmsg("請確認後重新輸入。");
		continue;
	    }

	    // found something
	    fd = open(dirfile, O_RDONLY);
	    if (fd < 0)
	    {
		outs(ANSI_COLOR(1;31) "系統錯誤。 請稍候再重試。\n");
		vmsg("若持續發生請至" GLOBAL_BUGREPORT "報告。");
		continue;
	    }

	    lseek(fd, n*sizeof(fileheader_t), SEEK_SET);
	    memset(&fh, 0, sizeof(fh));
	    read(fd, &fh, sizeof(fh));
	    outs("\n開始核對資料...\n");
	    n = 1;
	    if (strcmp(fh.owner, cuser.userid) != 0)
		n = 0;
	    prints("作者: %s (%s)\n", fh.owner, n ? "正確" : 
		    ANSI_COLOR(1;31) "錯誤" ANSI_RESET);
	    if (!(fh.filemode & FILE_MARKED))
		n = 0;
	    prints("Ｍ文: %s\n", (fh.filemode & FILE_MARKED) ? "正確" : 
		    ANSI_COLOR(1;31) "錯誤" ANSI_RESET);
	    prints("推薦: %d\n", fh.recommend);
	    close(fd);
	    if (!n)
	    {
		vmsg("輸入的文章並非優文，請重新輸入。");
		continue;
	    }
	    n = fh.recommend / 10;
	    prints("計算優文數值: %+d\n", n);

	    if (n > 0)
	    {
		// log new data
		newgp += n;
		gpaids[cgps] = aidu;
		gpbids[cgps] = bid;
		cgps ++;
	    }

	    clrtobot();


	    vmsg("優文已確認。若要輸入其它看板文章請在AID欄空白按 ENTER");
	}
	vmsgf("%s 看板輸入完成。", bname);
    }
    if (newgp <= cuser.goodpost)
    {
	vmsg("確認優文數目低於已有優文數，不調整。");
    } else {
	if (newgp > MAXGP)
	    newgp = MAXGP;
	log_filef("log/fixgoodpost.log", LOG_CREAT,
	        "%s %s 自動修正優文數: 由 %d 變為 %d\n", Cdate(&now), cuser.userid,
		cuser.goodpost, newgp);
	cuser.goodpost = newgp;
	// update passwd file here?
	passwd_update(usernum, &cuser);
	vmsgf("更新優文數目為%d。", newgp);
    }

    return 0;
}

#endif
