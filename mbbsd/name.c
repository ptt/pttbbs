#include "bbs.h"

#define COMPLETE_MORE_MSG   "按空白鍵可列出更多項目"
#define COMPLETE_LIST_TITLE "相關資訊一覽表"

void
ToggleVector(struct Vector *list, int *recipient, const char *listfile, const char *msg)
{
    FILE           *fp;
    char            genbuf[STRLEN];

    if ((fp = fopen(listfile, "r"))) {
	while (fgets(genbuf, sizeof(genbuf), fp)) {
	    char *space = strpbrk(genbuf, str_space);
	    if (space) *space = '\0';
	    if (!genbuf[0])
		continue;
	    if (Vector_search(list, genbuf) < 0) {
		if (Vector_length(list) < MAX_MULTILIST) {
		    Vector_add(list, genbuf);
		    (*recipient)++;
		}
	    } else {
		Vector_remove(list, genbuf);
		(*recipient)--;
	    }
	}
	fclose(fp);
	ShowVector(list, 3, 0, msg, 0);
    }
}

int
ShowVector(struct Vector *list, int row, int col, const char * msg, int idx)
{
    int i, len;

    move(row, col);
    clrtobot();

    if (msg) {
	outs(msg);
	row++;
    }
    col = 0;

    len = Vector_MaxLen(list, idx, b_lines - row);
    while (len + col < t_columns) {
	for (i = row; idx < Vector_length(list) && (i < b_lines); i++) {
	    move(i, col);
	    outs(Vector_get(list, idx));
	    idx++;
	}
	col += len + 2;
	if (idx == Vector_length(list)) {
	    idx = 0;
	    break;
	}
	len = Vector_MaxLen(list, idx, b_lines - row);
    }

    return idx;
}

#define MAX_COMPLETE_LIST   25

struct namecomplete_int {
    const struct Vector * base;
    struct Vector sublist;
    char sublist_buf[(MAX_COMPLETE_LIST + 1) * (IDLEN + 1)];
    int idx, dirty;
    int allow_nonexistent_prefix;
    int is_usercomplete;
    bool is_truncated;
};

static int
nc_sublist(struct namecomplete_int *nc_int, const struct Vector *src, const char *tag,
	   char *dst_buf, bool *out_truncated)
{
    int i, len = strlen(tag);
    int count = 0;
    bool truncated = false;
    char exact_id[IDLEN + 1] = "";
    int first_uc = len > 0 ? chartoupper((unsigned char)tag[0]) : 0;

    if (len == 0 && nc_int->is_usercomplete) {
	*out_truncated = true;
	return 0;
    }

    if (len > 0 && src == nc_int->base) {
	if (nc_int->is_usercomplete) {
	    if (searchuser(tag, exact_id) > 0) {
		strlcpy(dst_buf, exact_id, IDLEN + 1);
		count = 1;
	    }
	} else {
	    int idx = Vector_search(src, tag);
	    if (idx >= 0) {
		strlcpy(exact_id, Vector_get(src, idx), IDLEN + 1);
		strlcpy(dst_buf, exact_id, IDLEN + 1);
		count = 1;
	    }
	}
    }

    for (i = 0; i < src->length; i++) {
	const char *item = src->base + src->size * i;
	if (!item[0])
	    continue;
	if (len > 0) {
	    if (chartoupper((unsigned char)item[0]) != first_uc)
		continue;
	    if (len > 1 && strncasecmp(item + 1, tag + 1, len - 1) != 0)
		continue;
	}
	if (exact_id[0] && strcasecmp(item, exact_id) == 0)
	    continue;

	if (count < MAX_COMPLETE_LIST) {
	    strlcpy(dst_buf + count * (IDLEN + 1), item, IDLEN + 1);
	    count++;
	} else {
	    strlcpy(dst_buf + MAX_COMPLETE_LIST * (IDLEN + 1), "...", IDLEN + 1);
	    count = MAX_COMPLETE_LIST + 1;
	    truncated = true;
	    break;
	}
    }

    *out_truncated = truncated;
    return count;
}

static int
nc_cb_peek(int key, VGET_RUNTIME *prt, void *instance)
{
    struct namecomplete_int * nc_int = (struct namecomplete_int *) instance;
    int tmp;

    prt->buf[prt->iend] = 0;

    switch (key) {
	case KEY_ENTER:
	    if (prt->iend == 0) {
		prt->buf[0] = '\0';
		break;
	    }
	    if (nc_int->dirty < 0) {
		int count = nc_sublist(nc_int, nc_int->base, prt->buf,
				       nc_int->sublist_buf, &nc_int->is_truncated);
		Vector_init_const(&nc_int->sublist, nc_int->sublist_buf, count, IDLEN + 1);
		nc_int->idx = 0;
		nc_int->dirty = 0;
	    }
	    if (!nc_int->is_truncated && Vector_length(&nc_int->sublist) == 1)
		strlcpy(prt->buf, Vector_get(&nc_int->sublist, 0), prt->len);
	    else if ((tmp = Vector_search(&nc_int->sublist, prt->buf)) >= 0 &&
		     !(nc_int->is_truncated && tmp == Vector_length(&nc_int->sublist) - 1))
		strlcpy(prt->buf, Vector_get(&nc_int->sublist, tmp), prt->len);
	    else
		prt->buf[0] = '\0';
	    prt->icurr = prt->iend = strlen(prt->buf);
	    break;

	case ' ':
	    if (nc_int->is_usercomplete && prt->iend == 0)
		return VGETCB_NEXT;
	    if (nc_int->dirty < 0) {
		int count = nc_sublist(nc_int, nc_int->base, prt->buf,
				       nc_int->sublist_buf, &nc_int->is_truncated);
		Vector_init_const(&nc_int->sublist, nc_int->sublist_buf, count, IDLEN + 1);
		nc_int->idx = 0;
		nc_int->dirty = 0;
	    }
	    if (!nc_int->is_truncated && Vector_length(&nc_int->sublist) == 1) {
		const char *target = Vector_get(&nc_int->sublist, 0);
		// Update the buffer if the current input does not match the
		// completion target, using exact matching.
		if (strcmp(prt->buf, target)) {
		    strlcpy(prt->buf, target, prt->len);
		    prt->icurr = prt->iend = strlen(prt->buf);
		}
		// When the current input perfectly matches, show the list to
		// indicate user that the completion has exactly one match.
		// Otherwise, there no indication to the user, and might confuse
		// them when allow_nonexistent_prefix is on.
	    }

	    move(2, 0);
	    clrtobot();
	    printdash(COMPLETE_LIST_TITLE, 0);

	    nc_int->idx = ShowVector(&nc_int->sublist, 3, 0, NULL, nc_int->idx);
	    if (nc_int->idx > 0)
		vshowmsg(COMPLETE_MORE_MSG);
	    return VGETCB_NEXT;
	    break;

	case KEY_BS:	/* backspace */
	case KEY_UP:	/* history */
	case KEY_DOWN:	/* history */
	    nc_int->dirty = -1;
	    break;

	default:
	    if (isprint(key)) {
		char tmp_buf[(MAX_COMPLETE_LIST + 1) * (IDLEN + 1)];
		bool tmp_truncated = false;
		const struct Vector *src = (nc_int->dirty < 0 || nc_int->is_truncated)
					   ? nc_int->base : &nc_int->sublist;
		int count;

		prt->buf[prt->iend] = key;
		prt->buf[prt->iend + 1] = 0;

		count = nc_sublist(nc_int, src, prt->buf, tmp_buf, &tmp_truncated);

		if (!nc_int->allow_nonexistent_prefix && count == 0) {
		    prt->buf[prt->iend] = 0;
		    return VGETCB_NEXT;
		} else {
		    memcpy(nc_int->sublist_buf, tmp_buf, count * (IDLEN + 1));
		    Vector_init_const(&nc_int->sublist, nc_int->sublist_buf, count, IDLEN + 1);
		    nc_int->is_truncated = tmp_truncated;
		    nc_int->idx = 0;
		    nc_int->dirty = 0;
		    prt->buf[prt->iend] = 0;
		}
	    }
    }

    return VGETCB_NONE;
}

void
namecomplete2(const struct Vector *namelist, const char *prompt, char *data)
{
    return namecomplete3(namelist, prompt, data, NULL);
}

static void
namecomplete_internal(struct namecomplete_int *nc_int, const char *prompt, char *data, const char *defval)
{
    VGET_CALLBACKS vcb = {
	.peek = nc_cb_peek,
	.data = NULL,
	.change = NULL,
	.redraw = NULL,
    };
    int count;

    outs(prompt);
    clrtoeol();
    count = nc_sublist(nc_int, nc_int->base, defval ? defval : "",
		       nc_int->sublist_buf, &nc_int->is_truncated);
    Vector_init_const(&nc_int->sublist, nc_int->sublist_buf, count, IDLEN + 1);
    vgetstring(data, IDLEN + 1, VGET_ASCII_ONLY|VGET_NO_NAV_EDIT, defval, &vcb, nc_int);
    Vector_delete(&nc_int->sublist);
}

void
namecomplete3(const struct Vector *namelist, const char *prompt, char *data, const char *defval)
{
    struct namecomplete_int nc_int = {
	.base = namelist,
	.dirty = 0,
	.is_usercomplete = 0,
    };
    namecomplete_internal(&nc_int, prompt, data, defval);
}

void
usercomplete2(const char *prompt, char *data, const char *defval)
{
    struct Vector namelist;
    struct namecomplete_int nc_int = {
	.base = &namelist,
	.dirty = 0,
	.allow_nonexistent_prefix = 1,
	.is_usercomplete = 1,
    };

    Vector_init_const(&namelist, SHM->userid[0], MAX_USERS, IDLEN+1);
    namecomplete_internal(&nc_int, prompt, data, defval);
}

void
usercomplete(const char *prompt, char *data)
{
    usercomplete2(prompt, data, NULL);
}

// General Complete: used for boards and online users.

static int
gnc_findbound(char *str, int *START, int *END,
	      size_t nmemb, gnc_comp_func compar)
{
    int             start, end, mid, cmp, strl;
    strl = strlen(str);

    start = -1, end = nmemb - 1;
    /* The first available element is always in the half-open interval
     * (start, end]. (or `end'-th it self if start == end) */
    while (end > start + 1) {
	mid = (start + end) / 2;
	cmp = (*compar)(mid, str, strl);
	if (cmp >= 0)
	    end = mid;
	else
	    start = mid;
    }
    if ((*compar)(end, str, strl) != 0) {
	*START = *END = -1;
	return -1;
    }
    *START = end;

    start = end;
    end = nmemb;
    /* The last available element is always in the half-open interval
     * [start, end). (or `start'-th it self if start == end) */
    while (end > start + 1) {
	mid = (start + end) / 2;
	cmp = (*compar)(mid, str, strl);
	if (cmp <= 0)
	    start = mid;
	else
	    end = mid;
    }
    *END = start;
    return 0;
}

static int
gnc_complete(char *data, int *start, int *end,
		gnc_perm_func permission, gnc_getname_func getname, int limit)
{
    int             i, count, first = -1, last = *end;
    if (*start < 0 || *end < 0)
	return 0;
    for (i = *start, count = 0; i <= *end; ++i)
	if ((*permission)(i)) {
	    if (first == -1)
		first = i;
	    last = i;
	    ++count;
	    if (limit > 0 && count >= limit)
		break;
	}
    if (count == 1)
	strcpy(data, (*getname)(first));

    *start = first;
    if (limit == 0 || count < limit)
	*end = last;
    return count;
}

typedef struct {
    int  start, end, nmemb, ptr;
    int  morelist;
    int  page_dirty;  // YEA if screen was dirty and needs a clrtobot().
    int  is_online_user;
    gnc_comp_func    compar;
    gnc_perm_func    permission;
    gnc_getname_func getname;
} generalcomplete_int;

static int
gnc_cb_data(int key, VGET_RUNTIME *prt, void *instance)
{
    generalcomplete_int *gc_int = (generalcomplete_int*) instance;
    char *data = prt->buf;
    int   ret  = VGETCB_NEXT;	// reject by default
    int   i;

    assert(prt->icurr+1 < prt->len);	// verify size
    assert(prt->icurr == prt->iend);	// verify cursor position
    gc_int->morelist = -1;
    // try to add character
    data[prt->icurr]  = key;
    data[prt->icurr+1]= 0;
    if (gnc_findbound(data, &gc_int->start, &gc_int->end, gc_int->nmemb, gc_int->compar) >= 0)
    {
	// verify permission
	for (i = gc_int->start; i <= gc_int->end; ++i)
	    if ((*gc_int->permission)(i))
		break;
	// found something? accept it
	if (i != gc_int->end + 1)
	    ret = VGETCB_NONE;
    }
    // restore data buffer
    data[prt->icurr]= 0;
    return ret;
}

#define GNC_PAGE_START_Y    (2)

static int
gnc_cb_peek(int key, VGET_RUNTIME *prt, void *instance)
{
    generalcomplete_int *gc_int = (generalcomplete_int*) instance;
    char *data = prt->buf;

    switch(key) {
	case KEY_BS:	// backspace
	    gc_int->morelist = -1;
	    break;

	case ' ':	// render complete page
	    assert(prt->icurr == prt->iend);
	    if (gc_int->is_online_user && prt->iend == 0)
		return VGETCB_NEXT;
	    if (gc_int->morelist == -1)
	    {
		int i;
		char *first;
		int limit = gc_int->is_online_user ? (MAX_COMPLETE_LIST + 1) : 0;
		if (gnc_findbound(data, &gc_int->start, &gc_int->end,
				         gc_int->nmemb,  gc_int->compar) == -1)
		    return VGETCB_NEXT;

		i = gnc_complete (data, &gc_int->start, &gc_int->end,
					 gc_int->permission, gc_int->getname, limit);
		if (i <= 0) {
		    if (gc_int->page_dirty) {
			move(GNC_PAGE_START_Y, 0);
			clrtobot();
			printdash(COMPLETE_LIST_TITLE, 0);
		    }
		    return VGETCB_NEXT;
		}
		if (i == 1) {
		    prt->iend = prt->icurr = strlen(data);
		} else {
		    first = (*gc_int->getname)(gc_int->start);
		    i = prt->icurr;
		    while (first[i] && (*gc_int->compar)(gc_int->end, first, i + 1) == 0) {
			data[i] = first[i];
			++i;
		    }
		    data[i] = '\0';
		    prt->iend = prt->icurr = i;
		}
		gc_int->morelist = gc_int->start;
	    } else if (gc_int->morelist > gc_int->end)
		return VGETCB_NEXT;

	    // rendef list
	    gc_int->page_dirty = YEA;
	    move(GNC_PAGE_START_Y, 0);
	    clrtobot();
	    printdash(COMPLETE_LIST_TITLE, 0);
	    {
		int col = 0, i = 0;
		int len = prt->len;
		int printed = 0;
		while (len + col < t_columns-1) {
		    for (i = 0; gc_int->morelist <= gc_int->end && i < p_lines; ++gc_int->morelist) {
			if ((*gc_int->permission)(gc_int->morelist)) {
			    move(3 + i, col);
			    if (gc_int->is_online_user && printed >= MAX_COMPLETE_LIST) {
				prints("... ");
				++i;
				gc_int->morelist = gc_int->end + 1;
				break;
			    }
			    prints("%s ", (*gc_int->getname)(gc_int->morelist));
			    ++i;
			    ++printed;
			}
		    }
		    if (gc_int->is_online_user && gc_int->morelist > gc_int->end)
			break;
		    col += len + 2;
		}
	    }
	    if (gc_int->morelist != gc_int->end + 1) {
		vshowmsg(COMPLETE_MORE_MSG);
	    }
	    return VGETCB_NEXT;
    }
    return VGETCB_NONE;
}


int
generalnamecomplete(const char *prompt, char *data, int len, size_t nmemb,
		    gnc_comp_func compar, gnc_perm_func permission,
		    gnc_getname_func getname)
{
    generalcomplete_int gc_int = {
	.start = 0,
	.end   = nmemb-1,
	.nmemb = nmemb,
	.morelist = -1,
	.page_dirty = NA,
	.is_online_user = (getname == &completeutmp_getname),
	.compar     = compar,
	.permission = permission,
	.getname    = getname,
    };
    const VGET_CALLBACKS vcb = {
	.peek = gnc_cb_peek,
	.data = gnc_cb_data,
    };
    int ret = -1;

    outs(prompt);
    clrtoeol();

    // init vector
    ret = vgetstring(data, len, VGET_NO_NAV_EDIT, NULL, &vcb, &gc_int);
    outc('\n');
    if (gc_int.page_dirty) {
	move(GNC_PAGE_START_Y, 0);
	clrtobot();
    }

    // vgetstring() return string length, but namecomplete needs to return
    // the index of input string.

    if (ret < 1)
    {
	data[0] = '\0';
	return -1;
    }

    gnc_findbound(data, &gc_int.start, &gc_int.end, nmemb, compar);
    if (gnc_complete(data, &gc_int.start, &gc_int.end, permission, getname, 2) == 1 ||
	(gc_int.start >= 0 && (*compar)(gc_int.start, data, len) == 0))
    {
	strlcpy(data, (*getname)(gc_int.start), len);
	ret = gc_int.start;
    } else {
	data[0] = '\0';
	ret = -1;
    }

    return ret;
}

/* general complete functions (brdshm) */
int
completeboard_compar(int where, const char *str, int len)
{
    boardheader_t *bh = &bcache[SHM->bsorted[0][where]];
    return strncasecmp(bh->brdname, str, len);
}

int
completeboard_permission(int where)
{
    boardheader_t *bptr = &bcache[SHM->bsorted[0][where]];
    return (!(bptr->brdattr & BRD_SYMBOLIC) &&
	    (GROUPOP() || HasBoardPerm(bptr)) &&
	    !(bptr->brdattr & BRD_GROUPBOARD));
}

int
complete_board_and_group_permission(int where)
{
    boardheader_t *bptr = &bcache[SHM->bsorted[0][where]];
    return (!(bptr->brdattr & BRD_SYMBOLIC) &&
	    (GROUPOP() || HasBoardPerm(bptr)));

}

char           *
completeboard_getname(int where)
{
    return bcache[SHM->bsorted[0][where]].brdname;
}

/* general complete functions (utmpshm) */
int
completeutmp_compar(int where, const char *str, int len)
{
    userinfo_t *u = &SHM->uinfo[SHM->sorted[SHM->currsorted][0][where]];
    return strncasecmp(u->userid, str, len);
}

int
completeutmp_permission(int where)
{
   userinfo_t *u = &SHM->uinfo[SHM->sorted[SHM->currsorted][0][where]];
    return (unlikely(HasUserPerm(PERM_SYSOP)) ||
	    unlikely(HasUserPerm(PERM_SEECLOAK)) ||
//	    !SHM->sorted[SHM->currsorted][0][where]->invisible);
	    isvisible(currutmp, u));
}

char           *
completeutmp_getname(int where)
{
    return SHM->uinfo[SHM->sorted[SHM->currsorted][0][where]].userid;
}

static void
format_multilist_title(char *buf, size_t sz, const char *prefix, int count)
{
    size_t len = prefix ? strlen(prefix) : 0;
    while (len > 0 && (prefix[len - 1] == '\n' || prefix[len - 1] == '\r'))
        len--;
    if (len > 0)
        snprintf(buf, sz, "%.*s (共 %d 名)", (int)len, prefix, count);
    else
        snprintf(buf, sz, "名單： (共 %d 名)", count);
}

void
multi_user_list(struct Vector *namelist, int *recipient,
                const char *title, const char *msg_prefix, int flags)
{
    char uid[IDLEN + 1];
    char genbuf[PATHLEN];
    char listfile[] = "list.0";
    char msg_title[80];
    int i, cRemoved;

    while (1) {
        vs_hdr(title);
        format_multilist_title(msg_title, sizeof(msg_title), msg_prefix, Vector_length(namelist));
        ShowVector(namelist, 3, 0, msg_title, 0);
        move(1, 0);
        outs("(I)引入好友 (O)引入上線通知 (0-9)特別名單 (E)檔案編輯/貼上名單");
        getdata(2, 0,
                "(A)增加     (D)刪除         (M)確認名單   (Q)取消 ？[M]",
                genbuf, 4, LCECHO);
        switch (genbuf[0]) {
        case 'a':
            while (1) {
                if (Vector_length(namelist) >= MAX_MULTILIST) {
                    vmsgf("名單人數已達上限 (%d 人)！", MAX_MULTILIST);
                    break;
                }
                move(1, 0);
                usercomplete("請輸入要增加的代號(只按 ENTER 結束新增): ", uid);
                if (uid[0] == '\0')
                    break;
                move(2, 0);
                clrtoeol();
                if (!searchuser(uid, uid))
                    outs(err_uid);
                else if ((flags & MULTILIST_EXCLUDE_SELF) && strcasecmp(uid, cuser.userid) == 0)
                    outs("不能加入自己喔！");
                else if (strcasecmp(uid, STR_GUEST) == 0)
                    outs("不能加入 guest！");
                else if ((flags & MULTILIST_CHECK_REJECT) && is_rejected(uid))
                    outs("對方拒收信件！");
                else if (Vector_search(namelist, uid) < 0) {
                    Vector_add(namelist, uid);
                    (*recipient)++;
                }
                format_multilist_title(msg_title, sizeof(msg_title), msg_prefix, Vector_length(namelist));
                ShowVector(namelist, 3, 0, msg_title, 0);
            }
            break;
        case 'd':
            while (*recipient) {
                move(1, 0);
                namecomplete2(namelist, "請輸入要刪除的代號(只按 ENTER 結束刪除): ", uid);
                if (uid[0] == '\0')
                    break;
                if (Vector_remove(namelist, uid))
                    (*recipient)--;
                format_multilist_title(msg_title, sizeof(msg_title), msg_prefix, Vector_length(namelist));
                ShowVector(namelist, 3, 0, msg_title, 0);
            }
            break;
        case 'e':
            {
                char tmpfile[PATHLEN];
                FILE *fp;
                setuserfile(tmpfile, "multi_list.tmp");
                if ((fp = fopen(tmpfile, "w"))) {
                    fprintf(fp, "# 請在下方輸入或貼上 ID 名單（單次上限 %d 人，# 開頭為註解自動忽略）\n"
                                "# 支援格式：\n"
                                "#   1. 一行一個 ID，或以空白、逗號分隔多個 ID\n"
                                "#   2. 貼上文章推文（如：推 userid: 內容），系統會擷取 ID 並去除重複 ID\n",
                            MAX_MULTILIST);
                    for (i = 0; i < Vector_length(namelist); i++)
                        fprintf(fp, "%s\n", Vector_get(namelist, i));
                    fclose(fp);
                }
                if (veditfile(tmpfile) != -1 && (fp = fopen(tmpfile, "r"))) {
                    char line[ANSILINELEN];
                    Vector_delete(namelist);
                    Vector_init(namelist, IDLEN + 1);
                    *recipient = 0;
                    while (fgets(line, sizeof(line), fp)) {
                        char *src = line, *dst = line;
                        while (*src) {
                            if (*src == ESC_CHR && src[1] == '[') {
                                src += 2;
                                while (*src && !isalpha((unsigned char)*src))
                                    src++;
                                if (*src)
                                    src++;
                            } else {
                                *dst++ = *src++;
                            }
                        }
                        *dst = '\0';

                        src = line;
                        while (*src && isspace((unsigned char)*src))
                            src++;
                        if (!*src || *src == '#')
                            continue;

                        char *colon = strchr(src, ':');
                        if (colon)
                            *colon = '\0';

                        char *saveptr = NULL;
                        for (char *tok = strtok_r(src, " \t\r\n,;.", &saveptr);
                             tok && Vector_length(namelist) < MAX_MULTILIST;
                             tok = strtok_r(NULL, " \t\r\n,;.", &saveptr)) {
                            if (!isalpha((unsigned char)tok[0]))
                                continue;
                            if (searchuser(tok, uid) &&
                                strcasecmp(uid, STR_GUEST) != 0 &&
                                !((flags & MULTILIST_EXCLUDE_SELF) && strcasecmp(uid, cuser.userid) == 0) &&
                                !((flags & MULTILIST_CHECK_REJECT) && is_rejected(uid)) &&
                                Vector_search(namelist, uid) < 0) {
                                Vector_add(namelist, uid);
                                (*recipient)++;
                            }
                        }
                    }
                    fclose(fp);
                }
                unlink(tmpfile);
            }
            break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            listfile[5] = genbuf[0];
            genbuf[0] = '1';
        case 'i':
            setuserfile(genbuf, genbuf[0] == '1' ? listfile : fn_overrides);
            ToggleVector(namelist, recipient, genbuf, msg_title);
            break;
        case 'o':
            setuserfile(genbuf, FN_ALOHAED);
            ToggleVector(namelist, recipient, genbuf, msg_title);
            break;
        case 'q':
            *recipient = 0;
            return;
        default:
            vs_hdr(title);
            outs("\n正在檢查名單... \n");
            doupdate();
            cRemoved = 0;
            for (i = 0; i < Vector_length(namelist); i++) {
                const char *p = Vector_get(namelist, i);
                if (searchuser(p, uid) &&
                    strcasecmp(STR_GUEST, uid) != 0 &&
                    !((flags & MULTILIST_EXCLUDE_SELF) && strcasecmp(cuser.userid, uid) == 0) &&
                    !((flags & MULTILIST_CHECK_REJECT) && is_rejected(uid)))
                    continue;
                if (!cRemoved)
                    outs("下列 ID 無效或無法加入，已自名單移除；"
                         "請重新確認名單。\n\n");
                cRemoved++;
                prints("%-.*s ", IDLEN, p);
                Vector_remove(namelist, p);
                i--;
                if ((cRemoved + 1) * (IDLEN + 1) >= t_columns)
                    outs("\n");
            }
            *recipient = Vector_length(namelist);
            if (cRemoved) {
                pressanykey();
                continue;
            }
            return;
        }
    }
}
