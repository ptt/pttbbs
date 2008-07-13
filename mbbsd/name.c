/* $Id$ */
#include "bbs.h"

#define MORE_MSG "按空白鍵可列出更多項目"

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
	    printdash("相關資訊一覽表", 0);

	    nc_int->idx = ShowVector(&nc_int->sublist, 3, 0, NULL, nc_int->idx);
	    if (nc_int->idx < Vector_length(&nc_int->sublist))
		vshowmsg(MORE_MSG);
	    return VGETCB_NEXT;
	    break;

	case KEY_BS2: case KEY_BS:  /* backspace */
	    nc_int->dirty = -1;
	    break;

	case KEY_HOME:  case Ctrl('A'):
	case KEY_END:   case Ctrl('E'):
	case KEY_LEFT:  case Ctrl('B'):
	case KEY_RIGHT: case Ctrl('F'):
	case KEY_DEL:   case Ctrl('D'):
	case Ctrl('Y'):
	case Ctrl('K'):
	    return VGETCB_NEXT;
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
    struct namecomplete_int nc_int = {
	.base = namelist,
	.dirty = 0,
    };
    VGET_CALLBACKS vcb = {
	.peek = nc_cb_peek,
	.data = NULL,
	.post = NULL,
    };

    outs(prompt);
    clrtoeol();
    Vector_init(&nc_int.sublist, IDLEN+1);
    Vector_sublist(namelist, &nc_int.sublist, "");
    vgetstring(data, IDLEN + 1, VGET_ASCII_ONLY, NULL, &vcb, &nc_int);
    Vector_delete(&nc_int.sublist);
}

void
usercomplete(const char *prompt, char *data)
{
    struct Vector namelist;

    Vector_init_const(&namelist, SHM->userid[0], MAX_USERS, IDLEN+1);

    namecomplete2(&namelist, prompt, data);
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


int
generalnamecomplete(const char *prompt, char *data, int len, size_t nmemb,
		    gnc_comp_func compar, gnc_perm_func permission,
		    gnc_getname_func getname)
{
    int             x, y, origx, scrx, ch, i, morelist = -1, col, ret = -1;
    int             start, end, ptr;
    int             clearbot = NA;

    outs(prompt);
    clrtoeol();
    getyx(&y, &x);
    scrx = origx = x;

    ptr = 0;
    data[ptr] = 0;

    start = 0; end = nmemb - 1;
    while (1)
    {
	// print input field again
	move(y, scrx); outc(' '); clrtoeol(); move(y, scrx);
	outs(ANSI_REVERSE);
	// data[ptr] = 0;
	prints("%-*s", len, data);
	outs(ANSI_RESET);
	move(y, scrx + ptr);

	// get input
	if ((ch = igetch()) == EOF)
	    break;

	if (ch == KEY_ENTER) {
	    data[ptr] = 0;
	    outc('\n');
	    if (ptr != 0) {
		gnc_findbound(data, &start, &end, nmemb, compar);
		if (gnc_complete(data, &start, &end, permission, getname)
			== 1 || (*compar)(start, data, len) == 0) {
		    strcpy(data, (*getname)(start));
		    ret = start;
		} else {
		    // XXX why newline here?
		    data[0] = '\n';
		    ret = -1;
		}
	    } else
		ptr = -1;
	    break;
	} else if (ch == ' ') {
	    if (morelist == -1) {
		if (gnc_findbound(data, &start, &end, nmemb, compar) == -1)
		    continue;
		i = gnc_complete(data, &start, &end, permission, getname);
		if (i == 1) {
		    ptr = strlen(data);
		    continue;
		} else {
		    char* first = (*getname)(start);
		    i = ptr;
		    while (first[i] && (*compar)(end, first, i + 1) == 0) {
			data[i] = first[i];
			++i;
		    }
		    data[i] = '\0';

		    if (i != ptr) { /* did complete several words */
			ptr = i;
		    }
		}
		morelist = start;
	    } else if (morelist > end)
		continue;
	    clearbot = YEA;
	    move(2, 0);
	    clrtobot();
	    printdash("相關資訊一覽表", 0);

	    col = 0;
	    while (len + col < t_columns-1) {
		for (i = 0; morelist <= end && i < p_lines; ++morelist) {
		    if ((*permission)(morelist)) {
			move(3 + i, col);
			prints("%s ", (*getname)(morelist));
			++i;
		    }
		}

		col += len + 2;
	    }
	    if (morelist != end + 1) {
		vshowmsg(MORE_MSG);
	    }
	    continue;

	} else if (ch == KEY_BS2 || ch == KEY_BS) {	/* backspace */
	    if (ptr == 0)
		continue;
	    morelist = -1;
	    --ptr;
	    data[ptr] = 0;
	    continue;
	} else if (isprint(ch) && ptr <= (len - 2)) {
	    morelist = -1;
	    data[ptr] = ch;
	    ++ptr;
	    data[ptr] = 0;
	    if (gnc_findbound(data, &start, &end, nmemb, compar) < 0)
		data[--ptr] = 0;
	    else {
		for (i = start; i <= end; ++i)
		    if ((*permission)(i))
			break;
		if (i == end + 1)
		    data[--ptr] = 0;
	    }
	} else if (ch == KEY_UP || ch == KEY_DOWN) {
	    if (!InputHistoryExists(data))
		InputHistoryAdd(data);

	    if (ch == KEY_DOWN)
		InputHistoryNext(data, len);
	    else
		InputHistoryPrev(data, len);

	    ptr = strlen(data);
	}
    }

    outc('\n');
    if (clearbot) {
	move(2, 0);
	clrtobot();
    }
    if (*data) {
	move(y, origx);
	outs(data);
	outc('\n');

	// save the history
	InputHistoryAdd(data);
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
