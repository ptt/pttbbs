/* $Id$ */
#include "bbs.h"

/* ------------------------------------- */
/* 特別名單                              */
/* ------------------------------------- */

/* Ptt 其他特別名單的檔名 */
char     special_list[] = "list.0";
char     special_des[] = "ldes.0";

/* 特別名單的上限 */
const static unsigned int friend_max[8] = {
    MAX_FRIEND,
    MAX_REJECT,
    MAX_LOGIN_INFO,
    MAX_POST_INFO,
    MAX_NAMELIST,
    MAX_FRIEND,
    MAX_NAMELIST,
    MAX_FRIEND,
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
friend_count(char *fname)
{
    return file_count_line(fname);
}

void
friend_add(char *uident, int type, char* des)
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

    friend_file[FRIEND_SPECIAL] = special_list;
    for (i = 0; i <= 9; i++) {
	snprintf(genbuf, sizeof(genbuf), "  (\033[36m%d\033[m)  .. ", i);
	special_des[5] = i + '0';
	setuserfile(fname, special_des);
	if (dashf(fname)) {
	    /* XXX no NULL check?? */
	    FILE           *fp = fopen(fname, "r");

	    assert(fp);
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

    setfriendfile(fpath, type);

    do {
	move(2, 0);
	clrtobot();
	outs("要引入哪一個名單?\n");
	for (j = i = 0; i <= 4; i++)
	    if (i != type) {
		++j;
		prints("  (%d) %-s\n", j, friend_list[(int)i]);
	    }
	if (HAVE_PERM(PERM_SYSOP) || currmode & MODE_BOARD)
	    for (; i < 8; ++i)
		if (i != type) {
		    ++j;
		    prints("  (%d) %s 板的 %s\n", j, currboard,
			     friend_list[(int)i]);
		}
	outs("  (S) 選擇其他看板的特別名單");
	getdata(11, 0, "請選擇 或 直接[Enter] 放棄:", buf, 3, LCECHO);
	if (!buf[0])
	    return;
	if (buf[0] == 's')
	    Select();
	j = buf[0] - '1';
	if (j >= type)
	    j++;
	if (!(HAVE_PERM(PERM_SYSOP) || currmode & MODE_BOARD) && j >= 5)
	    return;
    } while (buf[0] < '1' || buf[0] > '9');

    if (j == FRIEND_SPECIAL)
	friend_special();

    setfriendfile(sfile, j);

    if ((fp = fopen(sfile, "r")) != NULL) {
	while (fgets(buf, 80, fp) && (unsigned)count <= friend_max[type]) {
	    char            the_id[15];

	    sscanf(buf, "%s", the_id); // XXX check buffer size
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
}

void
friend_delete(char *uident, int type)
{
    char            fn[80];
    setfriendfile(fn, type);
    file_delete_line(fn, uident, 0);
}

static void
delete_user_friend(char *uident, char *friend, int type)
{
    char fn[80];
#if 0
    if (type == FRIEND_ALOHA) {
#endif
	sethomefile(fn, uident, "aloha");
	file_delete_line(fn, friend, 0);
#if 0
    }
    else {
    }
#endif
}

void
friend_delete_all(char *uident, int type)
{
    char buf[80], line[80];
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
friend_editdesc(char *uident, int type)
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
			     short *destn, int *destar, char *fn)
{
    char    genbuf[200];
    FILE    *fp;
    short   nFriends = 0;
    int     uid, *tarray;

    setuserfile(genbuf, fn);
    if( (fp = fopen(genbuf, "r")) == NULL ){
	destar[0] = 0;
	if( destn )
	    *destn = 0;
    }
    else{
	tarray = (int *)malloc(sizeof(int) * maxf);
	--maxf; /* 因為最後一個要填 0, 所以先扣一個回來 */
	while( fgets(genbuf, STRLEN, fp) && nFriends < maxf )
	    if( strtok(genbuf, str_space) &&
		(uid = searchuser(genbuf)) )
		tarray[nFriends++] = uid;
	fclose(fp);

	if( tosort )
	    qsort(tarray, nFriends, sizeof(int), qsort_intcompar);
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
			 currutmp->friend, fn_overrides);

    if (!type || type & FRIEND_REJECT)
	friend_load_real(0, MAX_REJECT, NULL, currutmp->reject, fn_reject);

    if (currutmp->friendtotal)
	logout_friend_online(currutmp);
    login_friend_online();
}

static void
friend_water(char *message, int type)
{				/* 群體水球 added by Ptt */
    char            fpath[80], line[80], userid[IDLEN + 1];
    FILE           *fp;

    setfriendfile(fpath, type);
    if ((fp = fopen(fpath, "r"))) {
	while (fgets(line, 80, fp)) {
	    userinfo_t     *uentp;
	    int             tuid;

	    sscanf(line, "%s", userid); // XXX check buffer size
	    if ((tuid = searchuser(userid)) && tuid != usernum &&
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
    char            fpath[80], line[80], uident[20];
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
	move(0, 40);
	prints("(名單上限:%d個人)", friend_max[type]);
	count = 0;
	CreateNameList();

	if ((fp = fopen(fpath, "r"))) {
	    move(3, 0);
	    column = 0;
	    while (fgets(genbuf, STRLEN, fp)) {
		if (genbuf[0] <= ' ')
		    continue;
		strtok(genbuf, str_space);
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
		       "(K)刪除整個名單(W)丟水球(Q)結束？[Q]" :
		       "(A)增加 (P)引入其他名單 (Q)結束？[Q]"),
		uident, 3, LCECHO);
	if (*uident == 'a') {
	    move(1, 0);
	    usercomplete(msg_uid, uident);
	    if (uident[0] && searchuser(uident) && !InNameList(uident)) {
		friend_add(uident, type, NULL);
		dirty = 1;
	    }
	} else if (*uident == 'p') {
	    friend_append(type, count);
	    dirty = 1;
	} else if (*uident == 'e' && count) {
	    move(1, 0);
	    namecomplete(msg_uid, uident);
	    if (uident[0] && InNameList(uident)) {
		friend_editdesc(uident, type);
	    }
	} else if (*uident == 'd' && count) {
	    move(1, 0);
	    namecomplete(msg_uid, uident);
	    if (uident[0] && InNameList(uident)) {
		friend_delete(uident, type);
		dirty = 1;
	    }
	} else if (*uident == 'l' && count)
	    more(fpath, YEA);
	else if (*uident == 'k' && count) {
	    getdata(2, 0, "整份名單將會被刪除,您確定嗎 (a/N)?", uident, 3,
		    LCECHO);
	    if (*uident == 'a')
		unlink(fpath);
	    dirty = 1;
	} else if (*uident == 'w' && count) {
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
		    sscanf(line, "%s", uident); // XXX check buffer size
		    sethomefile(genbuf, uident,
			     type == FRIEND_ALOHA ? "aloha" : "postnotify");
		    del_distinct(genbuf, cuser.userid);
		}
		fclose(fp);
	    }
	    strlcpy(genbuf, fpath, sizeof(genbuf));
	    if ((fp = fopen(genbuf, "r"))) {
		while (fgets(line, 80, fp)) {
		    sscanf(line, "%s", uident); // XXX check buffer size
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
	}
	friend_load(0);
    }
}

int
t_override()
{
    friend_edit(FRIEND_OVERRIDE);
    return 0;
}

int
t_reject()
{
    friend_edit(FRIEND_REJECT);
    return 0;
}
