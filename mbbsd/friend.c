#include "bbs.h"

/* ------------------------------------- */
/* 特別名單                              */
/* ------------------------------------- */

/* 好友名單的上限 */
static const unsigned int friend_max[FRIEND_MAX] = {
    [FRIEND_OVERRIDE] = MAX_FRIEND,
    [FRIEND_REJECT]   = MAX_REJECT,
    [FRIEND_ALOHA]    = MAX_LOGIN_INFO,
};
/* 雖然好友跟壞人名單都是 * 2 但是一次最多load到shm只能有128 */


/* Ptt 各種好友名單的補述 */
static const char * const friend_desc[FRIEND_MAX] = {
    [FRIEND_OVERRIDE] = "友誼描述：",
    [FRIEND_REJECT]   = "惡形惡狀：",
    [FRIEND_ALOHA]    = "",
};

/* Ptt 各種好友名單的中文敘述 */
static const char * const friend_list[FRIEND_MAX] = {
    [FRIEND_OVERRIDE] = "好友名單",
    [FRIEND_REJECT]   = "壞人名單",
    [FRIEND_ALOHA]    = "上線通知",
};

/* sized in screen width */
#define MAX_DESCLEN (40)
#define MAX_EXPIRE_MONTH (800)

void
setfriendfile(char *fpath, int type)
{
    if (type >= 0 && type < FRIEND_MAX)
	setuserfile(fpath, friend_file[type]);
    else
	fpath[0] = '\0';
}
static int
delete_friend_from_file(const char *file, const char *string, int  case_sensitive)
{
    FILE *fp = NULL, *nfp = NULL;
    char genbuf[STRLEN + 1], buf[STRLEN];
    int ret = 0;

    AUTO_STR(fnew, "%s.%3.3X", file,
             (unsigned int)(arc4random_uniform(0x1000)));
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

// (2^31)/DAY_SECONDS/30 = 828
#define MAX_EXPIRE_MONTH (800)


int
user_list_add(const char *fpath, const char *uident, const char *des,
              const char *desc_prompt, unsigned int max_entries)
{
    if ((unsigned int)file_count_line(fpath) >= max_entries)
	return 0;

    if ((uident[0] > ' ') && !file_exist_entry(fpath, uident)) {
	char buf[MAX_DESCLEN] = "", buf2[STRLEN];
	char t_uident[IDLEN + 1];

	/* Thor: avoid uident run away when get data */
	STRLCPY(t_uident, uident);

	if (desc_prompt && *desc_prompt) {
	    if (!des)
		getdata(2, 0, desc_prompt, buf, sizeof(buf), DOECHO);
	    else
		getdata_str(2, 0, desc_prompt, buf, sizeof(buf), DOECHO, des);
	}

    	SNPRINTF(buf2, "%-13s%s", t_uident, buf);
     	file_append_entry(fpath, buf2);
	return 1;
    }
    return 0;
}

static void
user_list_editdesc(const char *fn, const char *uident, const char *desc_prompt)
{
    FILE           *fp = NULL, *nfp = NULL;
    char            genbuf[STRLEN];
    AUTO_STR(fnnew, "%s-", fn);
    if ((fp = fopen(fn, "r")) && (nfp = fopen(fnnew, "w"))) {
	int             length = strlen(uident);

	while (fgets(genbuf, STRLEN, fp)) {
	    if ((genbuf[0] > ' ') && strncmp(genbuf, uident, length))
		fputs(genbuf, nfp);
	    else if (!strncmp(genbuf, uident, length)) {
		char buf[MAX_DESCLEN];
		getdata(2, 0, desc_prompt, buf, MAX_DESCLEN, DOECHO);
		fprintf(nfp, "%-13s%s\n", uident, buf);
	    }
	}
	Rename(fnnew, fn);
    }
    if (fp)
	fclose(fp);
    if (nfp)
	fclose(nfp);
}

static int
user_list_validate(const char *fpath, int expire, int badpost)
{
    FILE *fp = NULL, *nfp = NULL;
    char genbuf[STRLEN+1], buf[STRLEN+1];
    int ret = 0;
    userec_t u;

    // expire is measured in month
    if (expire > 0 && expire < MAX_EXPIRE_MONTH)
	expire *= DAY_SECONDS * 30;
    else
	expire = 0;
    if (badpost < 0 || (unsigned)badpost > (unsigned)UCHAR_MAX)
	badpost = 0;
    syncnow();

    nfp = tmpfile();
    if (!nfp)
	return ret;
    fp = fopen(fpath, "rt");
    if (!fp) {
	fclose(nfp);
	return ret;
    }

    while (fgets(genbuf, sizeof(genbuf), fp))
    {
	if (genbuf[0] <= ' ')
	    continue;

	// isolate userid
	sscanf(genbuf, " %s", buf);
	chomp(buf);

	if (searchuser(buf, NULL))
	{
	    if (expire > 0 || badpost > 0) {
		userec_t *pu = &u;
		// drop user if (now-lastlogin) longer than expire*month
		getuser(buf, &u);

		if (expire > 0)
		{
		    // XXX lastlogin was NOT counting people with PERM_HIDE...
		    // although we will have 'lastseen' in future,
		    // never count people with PERM_HIDE.
		    if (!(PERM_HIDE(pu)) &&
			    time4_diff(now, u.lastlogin) > expire)
			continue;
		}
		if (badpost > 0)
		{
		    if (u.badpost >= badpost)
			continue;
		}
	    }
	    fputs(genbuf, nfp);
	}
    }

    fclose(fp);
    // prepare to rebuild file.
    fp = fopen(fpath, "wt");
    if (!fp) {
	fclose(nfp);
	return ret;
    }

    rewind(nfp);
    while (fgets(buf, sizeof(buf), nfp))
	fputs(buf, fp);
    fclose(fp);
    fclose(nfp);

    return 1;
}

static void
user_list_water(const char *fpath, const char *message)
{				/* 群體水球 added by Ptt */
    char            line[STRLEN], userid[IDLEN + 1];
    FILE           *fp;

    if ((fp = fopen(fpath, "r"))) {
	while (fgets(line, STRLEN, fp)) {
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

int
edit_user_list(const char *fpath, const user_list_cfg_t *cfg)
{
    char            uident[IDLEN + 1];
    int             count, column, dirty;
    FILE           *fp;
    char            genbuf[PATHLEN];
    struct Vector   namelist;

    if (!fpath || !*fpath || !cfg)
	return 0;

    Vector_init(&namelist, IDLEN + 1);
    dirty = 0;
    while (1) {
	vs_hdr(cfg->title ? cfg->title : "");
	move(0, 40);
	if (cfg->max_entries >= MAX_USERS)
	    prints("(名單上限: 不限)");
	else
	    prints("(名單上限: %u 人)", cfg->max_entries);
	count = 0;
	Vector_clear(&namelist, IDLEN + 1);

	if ((fp = fopen(fpath, "r"))) {
	    move(3, 0);
	    column = 0;
	    while (fgets(genbuf, STRLEN, fp)) {
		char *space;
		if (genbuf[0] <= ' ')
		    continue;
		space = strpbrk(genbuf, str_space);
		if (space) *space = '\0';
		Vector_add(&namelist, genbuf);
		prints("%-13s", genbuf);
		count++;
		if (++column > 5) {
		    column = 0;
		    outc('\n');
		}
	    }
	    fclose(fp);
	}

	char prompt[128];
	if (count > 0) {
	    snprintf(prompt, sizeof(prompt),
		     "(A)增加(D)刪除%s%s(L)列出(K)清空%s%s(Q)結束?[Q] ",
		     (cfg->desc_prompt && *cfg->desc_prompt) ? "(E)修改" : "",
		     cfg->import_fn ? "(P)引入" : "",
		     cfg->allow_clean ? "(C)整理有效名單" : "",
		     cfg->allow_waterball ? "(W)水球" : "");
	} else {
	    snprintf(prompt, sizeof(prompt),
		     "(A)增加 %s(Q)結束?[Q] ",
		     cfg->import_fn ? "(P)引入其他名單 " : "");
	}
	getdata(1, 0, prompt, uident, 3, LCECHO);

	if (uident[0] == 'a') {
	    move(1, 0);
	    usercomplete(MSG_UID, uident);
	    if (uident[0] && searchuser(uident, uident) && Vector_search(&namelist, uident) < 0) {
		if (user_list_add(fpath, uident, NULL, cfg->desc_prompt, cfg->max_entries))
		    dirty = 1;
	    }
	} else if (uident[0] == 'c' && count && cfg->allow_clean) {
	    int expire = 0, badpost = 0;

            move(1, 0); clrtobot();
            outs("整理名單可清除已消失、過期、或已有退文的帳號。\n\n");

	    getdata(2, 0,
		    "要從名單中清除幾個月沒上站的使用者？ (0=不清除)[0] ",
		    uident, 4, NUMECHO);
	    expire = atoi(uident);
#ifdef ASSESS
	    getdata(3, 0,
		    "要從名單中清除有幾篇以上退文的使用者？ (0=不清除)[0] ",
		    uident, 4, NUMECHO);
	    badpost = atoi(uident);
#endif
            move(4, 0);
            outs("將清除下列類別的帳號:\n");
            outs(" * 已消失的帳號\n");
            if (expire)
                prints(" * %d 個月沒上站的使用者\n", expire);
            if (badpost)
                prints(" * 已有 %d 篇退文的使用者\n", badpost);

            getdata(9, 0,
                    "確定要執行嗎? [y/N] ",
                    uident, 3, LCECHO);
            if (uident[0] != 'y')
                continue;

	    if (user_list_validate(fpath, expire, badpost))
		dirty = 1;
	} else if (uident[0] == 'p' && cfg->import_fn) {
	    if (cfg->import_fn(fpath, cfg->max_entries, count))
		dirty = 1;
	} else if (uident[0] == 'e' && count && cfg->desc_prompt && *cfg->desc_prompt) {
	    move(1, 0);
	    namecomplete2(&namelist, MSG_UID, uident);
	    if (uident[0] && Vector_search(&namelist, uident) >= 0) {
		user_list_editdesc(fpath, uident, cfg->desc_prompt);
		dirty = 1;
	    }
	} else if (uident[0] == 'd' && count) {
	    move(1, 0);
	    namecomplete2(&namelist, MSG_UID, uident);
	    if (uident[0] && Vector_search(&namelist, uident) >= 0) {
		delete_friend_from_file(fpath, uident, 0);
		dirty = 1;
	    }
	} else if (uident[0] == 'l' && count) {
	    more(fpath, YEA);
	} else if (uident[0] == 'k' && count) {
	    getdata(2, 0, "刪除整份名單,確定嗎 (a/N)?", uident, 3, LCECHO);
	    if (uident[0] == 'a') {
		unlink(fpath);
		dirty = 1;
	    }
	} else if (uident[0] == 'w' && count && cfg->allow_waterball) {
	    char            wall[60];
	    char            ans[4];
	    if (!getdata(0, 0, "群體水球:", wall, sizeof(wall), DOECHO))
		continue;
	    if (getdata(0, 0, "確定丟出群體水球? [Y]", ans, sizeof(ans), LCECHO) &&
		*ans == 'n')
		continue;
	    user_list_water(fpath, wall);
	} else {
	    break;
	}
    }
    Vector_delete(&namelist);
    if (dirty) {
	move(2, 0);
	outs("更新資料中..請稍候.....");
	refresh();
    }
    return dirty;
}
int
is_rejected(const char *userid) {
    userinfo_t *uentp;
    /* friend_online may be truncated or stale; only trust a positive hit. */
    if (currutmp && (uentp = search_ulist_userid(userid)) &&
        (friend_stat(currutmp, uentp) & HRM))
        return 1;
    if (!file_exist_entry(TEMP_HOMEFILE(userid, FN_REJECT), cuser.userid))
        return 0;
    return !file_exist_entry(TEMP_HOMEFILE(userid, FN_OVERRIDES), cuser.userid);
}

void
special_list_path(char *fpath, int index)
{
    if (index < 0 || index >= MAX_SPECIAL_LIST) {
	fpath[0] = '\0';
	return;
    }
    char fn[16];
    snprintf(fn, sizeof(fn), "list.%d", index);
    setuserfile(fpath, fn);
}

void
special_list_desc_path(char *fpath, int index)
{
    if (index < 0 || index >= MAX_SPECIAL_LIST) {
	fpath[0] = '\0';
	return;
    }
    char fn[16];
    snprintf(fn, sizeof(fn), "ldes.%d", index);
    setuserfile(fpath, fn);
}

int
special_list_get_title(int index, char *title, size_t sz)
{
    if (index < 0 || index >= MAX_SPECIAL_LIST || !title || sz == 0)
	return -1;
    title[0] = '\0';
    char fpath[PATHLEN];
    special_list_desc_path(fpath, index);
    FILE *fp = fopen(fpath, "r");
    if (fp) {
	if (fgets(title, sz, fp)) {
	    char *p = strpbrk(title, "\r\n");
	    if (p)
		*p = '\0';
	}
	fclose(fp);
	return 0;
    }
    return -1;
}

int
special_list_set_title(int index, const char *title)
{
    if (index < 0 || index >= MAX_SPECIAL_LIST || !title)
	return -1;
    char fpath[PATHLEN];
    special_list_desc_path(fpath, index);
    FILE *fp = fopen(fpath, "w");
    if (fp) {
	fputs(title, fp);
	fclose(fp);
	return 0;
    }
    return -1;
}

int
select_special_list(void)
{
    char genbuf[STRLEN];
    char title[MAX_DESCLEN];

    for (int i = 0; i < MAX_SPECIAL_LIST; i++) {
	title[0] = '\0';
	special_list_get_title(i, title, sizeof(title));
	SNPRINTF(genbuf, "  (" ANSI_COLOR(36) "%d" ANSI_RESET ")  .. %s", i, title);
	move(i + 12, 0);
	clrtoeol();
	outs(genbuf);
    }
    int rc = getdata(22, 0, "請選擇第幾號特別名單 (0~9)[0]?", genbuf, 3, LCECHO);
    if (rc < 0 || genbuf[0] == 'q')
	return -1;
    if (genbuf[0] >= '0' && genbuf[0] <= '9')
	return genbuf[0] - '0';
    return 0;
}


static int
friend_append(const char *fpath, unsigned int max_entries, int count)
{
    char            buf[STRLEN], sfile[PATHLEN];
    FILE           *fp;

    do {
	move(2, 0);
	clrtobot();
	outs("要引入哪一個名單?\n");

	int j = 0;
	for (int i = 0; (size_t)i < ARRAY_SIZE(friend_list); i++) {
	    char cur_path[PATHLEN];
	    setfriendfile(cur_path, i);
	    if (strcmp(cur_path, fpath) != 0) {
		++j;
		prints("  (%d) %-s\n", j, friend_list[i]);
	    }
	}
	int special_choice = ++j;
	prints("  (%d) 自訂特別名單 (0~9)\n", special_choice);

	getdata(11, 0, "請選擇 或 直接[Enter] 放棄:", buf, 3, LCECHO);
	if (!buf[0])
	    return 0;

	if (buf[0] == 's' || (buf[0] - '0') == special_choice) {
	    int idx = select_special_list();
	    if (idx < 0)
		continue;
	    special_list_path(sfile, idx);
	    if (strcmp(sfile, fpath) == 0)
		continue;
	    break;
	}

	int choice = buf[0] - '1';
	int sel = -1;
	int cur_j = 0;
	for (int i = 0; (size_t)i < ARRAY_SIZE(friend_list); i++) {
	    char cur_path[PATHLEN];
	    setfriendfile(cur_path, i);
	    if (strcmp(cur_path, fpath) != 0) {
		if (cur_j == choice) {
		    sel = i;
		    break;
		}
		cur_j++;
	    }
	}
	if (sel < 0)
	    continue;

	setfriendfile(sfile, sel);
	break;
    } while (1);

    int added = 0;
    if ((fp = fopen(sfile, "r")) != NULL) {
	while (fgets(buf, sizeof(buf), fp) && (unsigned int)count < max_entries) {
	    char the_id[IDLEN + 1];
	    sscanf(buf, "%" toSTR(IDLEN) "s", the_id);
	    if (!file_exist_entry(fpath, the_id)) {
		file_append(fpath, buf);
		count++;
		added = 1;
	    }
	}
	fclose(fp);
    }
    return added;
}

void
friend_add(const char *uident, int type, const char* des)
{
    char fpath[PATHLEN];
    if (type < 0 || (size_t)type >= ARRAY_SIZE(friend_list))
	return;
    setfriendfile(fpath, type);
    user_list_add(fpath, uident, des, friend_desc[type], friend_max[type]);
}

void
friend_delete(const char *uident, int type)
{
    char fn[PATHLEN];
    if (type < 0 || (size_t)type >= ARRAY_SIZE(friend_list))
	return;
    setfriendfile(fn, type);
    delete_friend_from_file(fn, uident, 0);
}

int
friend_validate(int type, int expire, int badpost)
{
    char fpath[PATHLEN];
    if (type < 0 || (size_t)type >= ARRAY_SIZE(friend_list))
	return 0;
    setfriendfile(fpath, type);
    return user_list_validate(fpath, expire, badpost);
}

int
edit_special_list(void)
{
    int idx = select_special_list();
    if (idx < 0)
	return 0;

    char fpath[PATHLEN];
    special_list_path(fpath, idx);

    char list_title[MAX_DESCLEN] = "";
    special_list_get_title(idx, list_title, sizeof(list_title));

    char title_buf[64];
    if (list_title[0])
	snprintf(title_buf, sizeof(title_buf), "特別名單 (%d) %s", idx, list_title);
    else
	snprintf(title_buf, sizeof(title_buf), "特別名單 (%d)", idx);

    user_list_cfg_t cfg = {
	.title = title_buf,
	.desc_prompt = "描述一下：",
	.max_entries = MAX_NAMELIST,
	.allow_waterball = 1,
	.allow_clean = 1,
	.import_fn = friend_append,
    };

    if (edit_user_list(fpath, &cfg)) {
	char namebuf[32] = "";
	STRLCPY(namebuf, list_title);
	getdata_buf(2, 0, " 請為此特別名單取一個簡短名稱:", namebuf, 30, DOECHO);
	special_list_set_title(idx, namebuf);
	friend_load();
	return 1;
    }
    return 0;
}

void
friend_edit(int type)
{
    char            fpath[PATHLEN];

    if (type < 0 || (size_t)type >= ARRAY_SIZE(friend_list))
	return;

    setfriendfile(fpath, type);

    user_list_cfg_t cfg = {
	.title = friend_list[type],
	.desc_prompt = friend_desc[type],
	.max_entries = friend_max[type],
	.allow_waterball = 1,
	.allow_clean = 1,
	.import_fn = friend_append,
    };

    if (edit_user_list(fpath, &cfg)) {
	if (type == FRIEND_ALOHA) {
            friend_svc_reload(cuser.userid);
	}
	friend_load();
    }
}
static int
has_friend_or_reject_file(void)
{
    char fpath[PATHLEN];

    setfriendfile(fpath, FRIEND_OVERRIDE);
    if (dashs(fpath) > 0)
        return 1;

    setfriendfile(fpath, FRIEND_REJECT);
    return dashs(fpath) > 0;
}

/* friend.svc owns friend_online[] (for both sides of every relation). There
 * is no local fallback: if friend.svc is down there is simply no friend data
 * until it comes back and rescans the online sessions. */
void friend_load(void)
{
    if (!currutmp || !currutmp->userid[0])
        return;

    if (friend_svc_sync(currutmp->userid, currutmp->uid, currutmp->pid,
		        get_utmp_slot(currutmp)) != 0) {
        if (has_friend_or_reject_file()) {
            vs_hdr("系統通知");
            outs("\n\n"
                 "  好友功能目前無法同步，好友跟黑名單功能可能暫時失效，\n"
                 "  如果擔心請直接斷線離開。\n\n");
            pressanykey();
        }
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
