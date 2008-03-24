/* $Id$ */
#include "bbs.h"

/* ------------------------------------- */
/* 特別名單                              */
/* ------------------------------------- */

/* Ptt 其他特別名單的檔名 */
char     special_list[] = "list.0";
char     special_des[] = "ldes.0";

/* 特別名單的上限 */
static const unsigned int friend_max[8] = {
    MAX_FRIEND,     /* FRIEND_OVERRIDE */
    MAX_REJECT,     /* FRIEND_REJECT   */
    MAX_LOGIN_INFO, /* FRIEND_ALOHA    */
    MAX_POST_INFO,  /* FRIEND_POST     */
    MAX_NAMELIST,   /* FRIEND_SPECIAL  */
    MAX_FRIEND,     /* FRIEND_CANVOTE  */
    MAX_FRIEND,     /* BOARD_WATER     */
    MAX_FRIEND,     /* BOARD_VISABLE   */
};
/* 雖然好友跟壞人名單都是 * 2 但是一次最多load到shm只能有128 */


/* Ptt 各種特別名單的補述 */
static char    * const friend_desc[8] = {
    "友誼描述：",
    "惡形惡狀：",
    "",
    "",
    "描述一下：",
    "投票者描述：",
    "惡形惡狀：",
    "看板好友描述"
};

/* Ptt 各種特別名單的中文敘述 */
static char    * const friend_list[8] = {
    "好友名單",
    "壞人名單",
    "上線通知",
    "新文章通知",
    "其它特別名單",
    "私人投票名單",
    "看板禁聲名單",
    "看板好友名單"
};

void
setfriendfile(char *fpath, int type)
{
    if (type <= 4)		/* user list Ptt */
	setuserfile(fpath, friend_file[type]);
    else			/* board list */
	setbfile(fpath, currboard, friend_file[type]);
}

inline static int
friend_count(const char *fname)
{
    return file_count_line(fname);
}

void
friend_add(const char *uident, int type, const char* des)
{
    char            fpath[80];

    setfriendfile(fpath, type);
    if (friend_count(fpath) > friend_max[type])
	return;

    if ((uident[0] > ' ') && !belong(fpath, uident)) {
	char            buf[40] = "", buf2[256];
	char            t_uident[IDLEN + 1];

	/* Thor: avoid uident run away when get data */
	strlcpy(t_uident, uident, sizeof(t_uident));

	if (type != FRIEND_ALOHA && type != FRIEND_POST){
           if(!des)
	    getdata(2, 0, friend_desc[type], buf, sizeof(buf), DOECHO);
           else
	    getdata_str(2, 0, friend_desc[type], buf, sizeof(buf), DOECHO, des);
	}

    	sprintf(buf2, "%-13s%s\n", t_uident, buf);
     	file_append_line(fpath, buf2);
    }
}

void
friend_special(void)
{
    char            genbuf[70], i, fname[70];
    FILE           *fp;
    friend_file[FRIEND_SPECIAL] = special_list;
    for (i = 0; i <= 9; i++) {
	snprintf(genbuf, sizeof(genbuf), "  (" ANSI_COLOR(36) "%d" ANSI_RESET ")  .. ", i);
	special_des[5] = i + '0';
	setuserfile(fname, special_des);
	if( (fp = fopen(fname, "r")) != NULL ){
	    fgets(genbuf + 15, 40, fp);
	    genbuf[47] = 0;
	    fclose(fp);
	}
	move(i + 12, 0);
	clrtoeol();
	outs(genbuf);
    }
    getdata(22, 0, "請選擇第幾號特別名單 (0~9)[0]?", genbuf, 3, LCECHO);
    if (genbuf[0] >= '0' && genbuf[0] <= '9') {
	special_list[5] = genbuf[0];
	special_des[5] = genbuf[0];
    } else {
	special_list[5] = '0';
	special_des[5] = '0';
    }
}

static void
friend_append(int type, int count)
{
    char            fpath[80], i, j, buf[80], sfile[80];
    FILE           *fp, *fp1;
    char	    myboard[IDLEN+1] = "";
    int		    boardChanged = 0;

    setfriendfile(fpath, type);

    if (currboard && *currboard) 
	strcpy(myboard, currboard);

    do {
	move(2, 0);
	clrtobot();
	outs("要引入哪一個名單?\n");
	for (j = i = 0; i <= 4; i++)
	    if (i != type) {
		++j;
		prints("  (%d) %-s\n", j, friend_list[(int)i]);
	    }
	if (HasUserPerm(PERM_SYSOP) || currmode & MODE_BOARD)
	    for (; i < 8; ++i)
		if (i != type) {
		    ++j;
		    prints("  (%d) %s 板的 %s\n", j, currboard,
			     friend_list[(int)i]);
		}
	if (HasUserPerm(PERM_SYSOP))
	    outs("  (S) 選擇其他看板的特別名單");

	getdata(11, 0, "請選擇 或 直接[Enter] 放棄:", buf, 3, LCECHO);
	if (!buf[0])
	    return;

	if (HasUserPerm(PERM_SYSOP) && buf[0] == 's')
	{
	    Select();
	    boardChanged = 1;
	}

	j = buf[0] - '1';
	if (j >= type)
	    j++;
	if (!(HasUserPerm(PERM_SYSOP) || currmode & MODE_BOARD) && j >= 5)
	{
	    if (boardChanged)
		enter_board(myboard);
	   return;
	}
    } while (buf[0] < '1' || buf[0] > '9');

    if (j == FRIEND_SPECIAL)
	friend_special();

    setfriendfile(sfile, j);

    if ((fp = fopen(sfile, "r")) != NULL) {
	while (fgets(buf, 80, fp) && (unsigned)count <= friend_max[type]) {
	    char            the_id[IDLEN + 1];

	    sscanf(buf, "%" toSTR(IDLEN) "s", the_id);
	    if (!file_exist_record(fpath, the_id)) {
		if ((fp1 = fopen(fpath, "a"))) {
		    flock(fileno(fp1), LOCK_EX);
		    fputs(buf, fp1);
		    flock(fileno(fp1), LOCK_UN);
		    fclose(fp1);
		}
	    }
	}
	fclose(fp);
    }
    if (boardChanged)
	enter_board(myboard);
}

static int
delete_friend_from_file(const char *file, const char *string, int  case_sensitive)
{
    FILE *fp = NULL, *nfp = NULL;
    char fnew[PATHLEN];
    char genbuf[STRLEN + 1], buf[STRLEN];
    int ret = 0;

    snprintf(fnew, sizeof(fnew), "%s.%3.3X", file, (unsigned int)(random() & 0xFFF));
    if ((fp = fopen(file, "r")) && (nfp = fopen(fnew, "w"))) {
	while (fgets(genbuf, sizeof(genbuf), fp))
	    if ((genbuf[0] > ' ')) {
		// prevent buffer overflow
		genbuf[sizeof(genbuf)-1] =0;
		sscanf(genbuf, " %s", buf);
		genbuf[sizeof(buf)-1] =0;
		if (((case_sensitive && strcmp(buf, string)) ||
		    (!case_sensitive && strcasecmp(buf, string))))
    		    fputs(genbuf, nfp);
		else
		    ret = 1;
	    }
	Rename(fnew, file);
    }
    if(fp)
	fclose(fp);
    if(nfp)
	fclose(nfp);
    return ret;
}

void
friend_delete(const char *uident, int type)
{
    char            fn[STRLEN];
    setfriendfile(fn, type);
    delete_friend_from_file(fn, uident, 0);
}

static void
delete_user_friend(const char *uident, const char *thefriend, int type)
{
    char fn[PATHLEN];
    sethomefile(fn, uident, "aloha");
    delete_friend_from_file(fn, thefriend, 0);
}

void
friend_delete_all(const char *uident, int type)
{
    char buf[PATHLEN], line[PATHLEN];
    FILE *fp;

    sethomefile(buf, uident, friend_file[type]);

    if ((fp = fopen(buf, "r")) == NULL)
	return;

    while (fgets(line, sizeof(line), fp)) {
	sscanf(line, "%s", buf);
	delete_user_friend(buf, uident, type);
    }

    fclose(fp);
}

static void
friend_editdesc(const char *uident, int type)
{
    FILE           *fp=NULL, *nfp=NULL;
    char            fnnew[200], genbuf[STRLEN], fn[200];
    setfriendfile(fn, type);
    snprintf(fnnew, sizeof(fnnew), "%s-", fn);
    if ((fp = fopen(fn, "r")) && (nfp = fopen(fnnew, "w"))) {
	int             length = strlen(uident);

	while (fgets(genbuf, STRLEN, fp)) {
	    if ((genbuf[0] > ' ') && strncmp(genbuf, uident, length))
		fputs(genbuf, nfp);
	    else if (!strncmp(genbuf, uident, length)) {
		char            buf[50] = "";
		getdata(2, 0, "修改描述：", buf, 40, DOECHO);
		fprintf(nfp, "%-13s%s\n", uident, buf);
	    }
	}
	Rename(fnnew, fn);
    }
    if(fp)
	fclose(fp);
    if(nfp)
	fclose(nfp);
}

inline void friend_load_real(int tosort, int maxf,
			     short *destn, int *destar, const char *fn)
{
    char    genbuf[200];
    FILE    *fp;
    short   nFriends = 0;
    int     uid, *tarray;
    char *p;

    setuserfile(genbuf, fn);
    if( (fp = fopen(genbuf, "r")) == NULL ){
	destar[0] = 0;
	if( destn )
	    *destn = 0;
    }
    else{
	char *strtok_pos;
	tarray = (int *)malloc(sizeof(int) * maxf);
	--maxf; /* 因為最後一個要填 0, 所以先扣一個回來 */
	while( fgets(genbuf, STRLEN, fp) && nFriends < maxf )
	    if( (p = strtok_r(genbuf, str_space, &strtok_pos)) &&
		(uid = searchuser(p, NULL)) )
		tarray[nFriends++] = uid;
	fclose(fp);

	if( tosort )
	    qsort(tarray, nFriends, sizeof(int), cmp_int);
	if( destn )
	    *destn = nFriends;
	tarray[nFriends] = 0;
	memcpy(destar, tarray, sizeof(int) * (nFriends + 1));
	free(tarray);
    }
}

/* type == 0 : load all */
void friend_load(int type)
{
    if (!type || type & FRIEND_OVERRIDE)
	friend_load_real(1, MAX_FRIEND, &currutmp->nFriends,
			 currutmp->myfriend, fn_overrides);

    if (!type || type & FRIEND_REJECT)
	friend_load_real(0, MAX_REJECT, NULL, currutmp->reject, fn_reject);

    if (currutmp->friendtotal)
	logout_friend_online(currutmp);

    login_friend_online();
}

static void
friend_water(const char *message, int type)
{				/* 群體水球 added by Ptt */
    char            fpath[80], line[80], userid[IDLEN + 1];
    FILE           *fp;

    setfriendfile(fpath, type);
    if ((fp = fopen(fpath, "r"))) {
	while (fgets(line, 80, fp)) {
	    userinfo_t     *uentp;
	    int             tuid;

	    sscanf(line, "%" toSTR(IDLEN) "s", userid);
	    if ((tuid = searchuser(userid, NULL)) && tuid != usernum &&
		(uentp = (userinfo_t *) search_ulist(tuid)) &&
		isvisible_uid(tuid))
		my_write(uentp->pid, message, uentp->userid, WATERBALL_PREEDIT, NULL);
	}
	fclose(fp);
    }
}

void
friend_edit(int type)
{
    char            fpath[80], line[80], uident[IDLEN + 1];
    int             count, column, dirty;
    FILE           *fp;
    char            genbuf[200];

    if (type == FRIEND_SPECIAL)
	friend_special();
    setfriendfile(fpath, type);

    if (type == FRIEND_ALOHA || type == FRIEND_POST) {
	if (dashf(fpath)) {
            sprintf(genbuf,"%s.old",fpath);
            Copy(fpath, genbuf);
	}
    }
    dirty = 0;
    while (1) {
	stand_title(friend_list[type]);
	/* TODO move (0, 40) just won't really work as it hints.
	 * The ANSI secapes will change x coordinate. */
	move(0, 40);
	prints("(名單上限: %d 人)", friend_max[type]);
	count = 0;
	CreateNameList();

	if ((fp = fopen(fpath, "r"))) {
	    move(3, 0);
	    column = 0;
	    while (fgets(genbuf, STRLEN, fp)) {
		char *space;
		if (genbuf[0] <= ' ')
		    continue;
		space = strpbrk(genbuf, str_space);
		if (space) *space = '\0';
		AddNameList(genbuf);
		prints("%-13s", genbuf);
		count++;
		if (++column > 5) {
		    column = 0;
		    outc('\n');
		}
	    }
	    fclose(fp);
	}
	getdata(1, 0, (count ?
		       "(A)增加(D)刪除(E)修改(P)引入(L)詳細列出"
		       "(K)刪除整個名單(W)丟水球(Q)結束?[Q] " :
		       "(A)增加 (P)引入其他名單 (Q)結束?[Q] "),
		uident, 3, LCECHO);
	if (uident[0] == 'a') {
	    move(1, 0);
	    usercomplete(msg_uid, uident);
	    if (uident[0] && searchuser(uident, uident) && !InNameList(uident)) {
		friend_add(uident, type, NULL);
		dirty = 1;
	    }
	} else if (uident[0] == 'p') {
	    friend_append(type, count);
	    dirty = 1;
	} else if (uident[0] == 'e' && count) {
	    move(1, 0);
	    namecomplete(msg_uid, uident);
	    if (uident[0] && InNameList(uident)) {
		friend_editdesc(uident, type);
	    }
	} else if (uident[0] == 'd' && count) {
	    move(1, 0);
	    namecomplete(msg_uid, uident);
	    if (uident[0] && InNameList(uident)) {
		friend_delete(uident, type);
		dirty = 1;
	    }
	} else if (uident[0] == 'l' && count)
	    more(fpath, YEA);
	else if (uident[0] == 'k' && count) {
	    getdata(2, 0, "刪除整份名單,確定嗎 (a/N)?", uident, 3,
		    LCECHO);
	    if (uident[0] == 'a')
		unlink(fpath);
	    dirty = 1;
	} else if (uident[0] == 'w' && count) {
	    char            wall[60];
	    if (!getdata(0, 0, "群體水球:", wall, sizeof(wall), DOECHO))
		continue;
	    if (getdata(0, 0, "確定丟出群體水球? [Y]", line, 4, LCECHO) &&
		*line == 'n')
		continue;
	    friend_water(wall, type);
	} else
	    break;
    }
    if (dirty) {
	move(2, 0);
	outs("更新資料中..請稍候.....");
	refresh();
	if (type == FRIEND_ALOHA || type == FRIEND_POST) {
	    snprintf(genbuf, sizeof(genbuf), "%s.old", fpath);
	    if ((fp = fopen(genbuf, "r"))) {
		while (fgets(line, 80, fp)) {
		    sscanf(line, "%" toSTR(IDLEN) "s", uident);
		    sethomefile(genbuf, uident,
			     type == FRIEND_ALOHA ? "aloha" : "postnotify");
		    del_distinct(genbuf, cuser.userid, 0);
		}
		fclose(fp);
	    }
	    strlcpy(genbuf, fpath, sizeof(genbuf));
	    if ((fp = fopen(genbuf, "r"))) {
		while (fgets(line, 80, fp)) {
		    sscanf(line, "%" toSTR(IDLEN) "s", uident);
		    sethomefile(genbuf, uident,
			     type == FRIEND_ALOHA ? "aloha" : "postnotify");
		    add_distinct(genbuf, cuser.userid);
		}
		fclose(fp);
	    }
	} else if (type == FRIEND_SPECIAL) {
	    genbuf[0] = 0;
	    setuserfile(line, special_des);
	    if ((fp = fopen(line, "r"))) {
		fgets(genbuf, 30, fp);
		fclose(fp);
	    }
	    getdata_buf(2, 0, " 請為此特別名單取一個簡短名稱:", genbuf, 30,
			DOECHO);
	    if ((fp = fopen(line, "w"))) {
		fputs(genbuf, fp);
		fclose(fp);
	    }
	} else if (type == BOARD_WATER) {
	    boardheader_t *bp = NULL;
	    currbid = getbnum(currboard);
	    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
	    bp = getbcache(currbid);
	    bp->perm_reload = now;
	    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
	    substitute_record(fn_board, bp, sizeof(boardheader_t), currbid);
	    // log_usies("SetBoard", bp->brdname);
	}
	friend_load(0);
    }
}

int
t_override(void)
{
    friend_edit(FRIEND_OVERRIDE);
    return 0;
}

int
t_reject(void)
{
    friend_edit(FRIEND_REJECT);
    return 0;
}

int
t_fix_aloha()
{
    char xid[IDLEN+1] = "";
    char fn[PATHLEN] = "";

    clear();
    stand_title("修正上站通知");

    outs("這是用來修正某些使用者遇到錯誤的上站通知的問題。\n"
	 ANSI_COLOR(1) "如果你沒遇到此類問題可直接離開。" ANSI_RESET "\n\n"
	 "▼如果你遇到有人沒在你的上站通知名單內但又會丟上站通知水球給你，\n"
	 "  請輸入他的 ID。\n");
    
    move(7, 0);
    usercomplete("有誰不在你的通知名單內但又會送上站通知水球給您呢？ ", xid);

    if (!xid[0])
    {
	vmsg("修正結束。");
	return 0;
    }

    // check by xid
    move(9, 0);
    outs("檢查中...\n");

    // xid in my override list?
    setuserfile(fn, "alohaed");
    if (belong(fn, xid))
    {
	prints(ANSI_COLOR(1;32) "[%s] 確實在你的上站通知名單內。"
		"請編輯 [上站通知名單]。" ANSI_RESET "\n", xid);
	vmsg("不需修正。");
	return 0;
    }

    sethomefile(fn, xid, "aloha");
    if (delete_friend_from_file(fn, cuser.userid, 0))
    {
	outs(ANSI_COLOR(1;33) "已找到錯誤並修復完成。" ANSI_RESET "\n");
    } else {
	outs(ANSI_COLOR(1;31) "找不到錯誤... 打錯 ID 了？" ANSI_RESET "\n");
    }

    vmsg("若上站通知錯誤仍持續發生請通知站方處理。");
    return 0;
}

