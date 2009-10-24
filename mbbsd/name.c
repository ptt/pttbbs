/* $Id$ */
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
		Vector_add(list, genbuf);
		(*recipient)++;
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

struct namecomplete_int {
    const struct Vector * base;
    struct Vector sublist;
    int idx, dirty;
};

static int
nc_cb_peek(int key, VGET_RUNTIME *prt, void *instance)
{
    struct namecomplete_int * nc_int = (struct namecomplete_int *) instance;
    int tmp;

    prt->buf[prt->iend] = 0;

    if (nc_int->dirty < 0) {
	Vector_sublist(nc_int->base, &nc_int->sublist, prt->buf);
	nc_int->idx = 0;
	nc_int->dirty = 0;
    }

    switch (key) {
	case KEY_ENTER:
	    if (Vector_length(&nc_int->sublist) == 1)
		strlcpy(prt->buf, Vector_get(&nc_int->sublist, 0), prt->len);
	    else if ((tmp = Vector_search(&nc_int->sublist, prt->buf)) >= 0)
		strlcpy(prt->buf, Vector_get(&nc_int->sublist, tmp), prt->len);
	    else
		prt->buf[0] = '\0';
	    prt->icurr = prt->iend = strlen(prt->buf);
	    break;

	case ' ':
	    if (Vector_length(&nc_int->sublist) == 1) {
		strlcpy(prt->buf, Vector_get(&nc_int->sublist, 0), prt->len);
		prt->icurr = prt->iend = strlen(prt->buf);
		return VGETCB_NEXT;
	    }

	    move(2, 0);
	    clrtobot();
	    printdash(COMPLETE_LIST_TITLE, 0);

	    nc_int->idx = ShowVector(&nc_int->sublist, 3, 0, NULL, nc_int->idx);
	    if (nc_int->idx < Vector_length(&nc_int->sublist))
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
		struct Vector tmplist;

		prt->buf[prt->iend] = key;
		prt->buf[prt->iend + 1] = 0;

		Vector_init(&tmplist, IDLEN + 1);
		Vector_sublist(&nc_int->sublist, &tmplist, prt->buf);

		if (Vector_length(&tmplist) == 0) {
		    Vector_delete(&tmplist);
		    prt->buf[prt->iend] = 0;
		    return VGETCB_NEXT;
		} else {
		    Vector_delete(&nc_int->sublist);
		    nc_int->sublist = tmplist;
		    nc_int->idx = 0;
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

void
namecomplete3(const struct Vector *namelist, const char *prompt, char *data, const char *defval)
{
    struct namecomplete_int nc_int = {
	.base = namelist,
	.dirty = 0,
    };
    VGET_CALLBACKS vcb = {
	.peek = nc_cb_peek,
	.data = NULL,
	.change = NULL,
	.redraw = NULL,
    };

    outs(prompt);
    clrtoeol();
    Vector_init(&nc_int.sublist, IDLEN+1);
    Vector_sublist(namelist, &nc_int.sublist, defval ? defval : "");
    vgetstring(data, IDLEN + 1, VGET_ASCII_ONLY|VGET_NO_NAV_EDIT, defval, &vcb, &nc_int);
    Vector_delete(&nc_int.sublist);
}

void
usercomplete2(const char *prompt, char *data, const char *defval)
{
    struct Vector namelist;

    Vector_init_const(&namelist, SHM->userid[0], MAX_USERS, IDLEN+1);
    namecomplete3(&namelist, prompt, data, defval);
}

void
usercomplete(const char *prompt, char *data)
{
    usercomplete2(prompt, data, NULL);
}

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
		gnc_perm_func permission, gnc_getname_func getname)
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
	}
    if (count == 1)
	strcpy(data, (*getname)(first));

    *start = first;
    *end = last;
    return count;
}

typedef struct {
    int  start, end, nmemb, ptr;
    int  morelist;
    int  page_dirty;  // YEA if screen was dirty and needs a clrtobot().
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
	    if (gc_int->morelist == -1) 
	    {
		int i;
		char *first;
		if (gnc_findbound(data, &gc_int->start, &gc_int->end, 
				         gc_int->nmemb,  gc_int->compar) == -1)
		    return VGETCB_NEXT;

		i = gnc_complete (data, &gc_int->start, &gc_int->end, 
					 gc_int->permission, gc_int->getname);
		if (i == 1) {
		    prt->iend = prt->icurr = strlen(data);
		    return VGETCB_NEXT;
		}
		first = (*gc_int->getname)(gc_int->start);
		i = prt->icurr;
		while (first[i] && (*gc_int->compar)(gc_int->end, first, i + 1) == 0) {
		    data[i] = first[i];
		    ++i;
		}
		data[i] = '\0';
		prt->iend = prt->icurr = i;
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
		while (len + col < t_columns-1) {
		    for (i = 0; gc_int->morelist <= gc_int->end && i < p_lines; ++gc_int->morelist) {
			if ((*gc_int->permission)(gc_int->morelist)) {
			    move(3 + i, col);
			    prints("%s ", (*gc_int->getname)(gc_int->morelist));
			    ++i;
			}
		    }
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
    if (gnc_complete(data, &gc_int.start, &gc_int.end, permission, getname) == 1 || 
	(*compar)(gc_int.start, data, len) == 0)
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
