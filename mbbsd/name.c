/* $Id$ */
#include "bbs.h"

typedef char    (*arrptr)[];
/* name complete for user ID */

static void 
prompt_more()
{
    move(b_lines, 0); clrtoeol();
    outs(ANSI_COLOR(1;36;44));
    prints("%-*s" ANSI_RESET, t_columns-2, " ◆ 按空白鍵可列出更多項目 ");
}

//-----------------------------------------------------------------------

void
ShowVector(struct Vector *self, int row, int column, const char *prompt)
{
    int i;

    move(row, column);
    clrtobot();
    outs(prompt);

    column = 80;
    for (i = 0; i < Vector_length(self); i++) {
	const char *p = Vector_get(self, i);
	row = strlen(p) + 1;
	if (column + row > 76) {
	    column = row;
	    outc('\n');
	} else {
	    column += row;
	    outc(' ');
	}
	outs(p);
    }
}

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
	    if (!Vector_search(list, genbuf)) {
		Vector_add(list, genbuf);
		(*recipient)++;
	    } else {
		Vector_remove(list, genbuf);
		(*recipient)--;
	    }
	}
	fclose(fp);
	ShowVector(list, 3, 0, msg);
    }
}

int
chkstr(char *otag, const char *tag, const char *name)
{
    char            ch;
    const char     *oname = name;

    while (*tag) {
	ch = *name++;
	if (*tag != chartoupper(ch))
	    return 0;
	tag++;
    }
    if (*tag && *name == '\0')
	strcpy(otag, oname);
    return 1;
}

void
namecomplete2(struct Vector *namelist, const char *prompt, char *data)
{
    char           *temp;
    int             x, y, origx, scrx;
    int             ch;
    int             count = 0;
    int             clearbot = NA;
    struct Vector sublist;
    int viewoffset = 0;

    Vector_init(&sublist, IDLEN + 1);

    Vector_sublist(namelist, &sublist, "");
    temp = data;

    outs(prompt);
    clrtoeol();
    getyx(&y, &x);
    scrx = origx = x;
    data[count] = 0;
    viewoffset = 0;

    while (1)
    {
	// print input field
	move(y, scrx); outc(' '); clrtoeol(); move(y, scrx);
	outs(ANSI_REVERSE);
	prints("%-*s", IDLEN + 1, data);
	outs(ANSI_RESET);
	move(y, scrx + count);

	// get input
	if ((ch = igetch()) == EOF) {
	    Vector_delete(&sublist);
	    break;
	}

	if (ch == KEY_ENTER) {
	    *temp = '\0';
	    if (Vector_length(&sublist)==1)
		strcpy(data, Vector_get(&sublist, 0));
	    else if (!Vector_search(&sublist, data))
		data[0] = '\0';
	    Vector_delete(&sublist);
	    break;
	}
	if (ch == ' ') {
	    int             col, len;

	    if (Vector_length(&sublist) == 1) {
		strcpy(data, Vector_get(&sublist, 0));
		count = strlen(data);
		temp = data + count;
		continue;
	    }
	    clearbot = YEA;
	    col = 0;
	    len = Vector_MaxLen(&sublist, viewoffset, p_lines);
	    move(2, 0);
	    clrtobot();
	    printdash("相關資訊一覽表", 0);
	    while (len + col < t_columns) {
		int             i;

		for (i = p_lines; viewoffset < Vector_length(&sublist) && (i > 0); i--) {
		    move(3 + (p_lines - i), col);
		    outs(Vector_get(&sublist, viewoffset));
		    viewoffset++;
		}
		col += len + 2;
		if (viewoffset == Vector_length(&sublist)) {
		    viewoffset = 0;
		    break;
		}
		len = Vector_MaxLen(&sublist, viewoffset, p_lines);
	    }
	    if (viewoffset < Vector_length(&sublist)) {
		prompt_more();
	    }
	    continue;
	}
	if (ch == KEY_BS2 || ch == KEY_BS) {	/* backspace */
	    if (temp == data)
		continue;
	    temp--;
	    count--;
	    *temp = '\0';
	    Vector_sublist(namelist, &sublist, data);
	    viewoffset = 0;
	    continue;
	}
	if (count < STRLEN && isprint(ch)) {
	    struct Vector tmplist;
	    Vector_init(&tmplist, IDLEN + 1);

	    *temp++ = ch;
	    count++;
	    *temp = '\0';

	    Vector_sublist(&sublist, &tmplist, data);
	    if (Vector_length(&tmplist)==0) {
		Vector_delete(&tmplist);
		temp--;
		*temp = '\0';
		count--;
		continue;
	    }
	    Vector_delete(&sublist);
	    sublist = tmplist;
	    viewoffset = 0;
	}
    }
    if (ch == EOF)
	/* longjmp(byebye, -1); */
	raise(SIGHUP);		/* jochang: don't know if this is
				 * necessary... */
    outc('\n');
    if (clearbot) {
	move(2, 0);
	clrtobot();
    }
    if (*data) {
	move(y, origx);
	outs(data);
	outc('\n');
    }
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
		prompt_more();
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
