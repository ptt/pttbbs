/* $Id$ */
#include "bbs.h"

static word_t  *current = NULL;
static char    * const msg_more = "-- More --";

typedef char    (*arrptr)[];
/* name complete for user ID */

//-----------------------------------------------------------------------

void NameList_init(struct NameList *self)
{
    self->size = 0;
    self->capacity = 0;
    self->base = NULL;
}

void NameList_delete(struct NameList *self)
{
    self->size = 0;
    self->capacity = 0;
    if(self->base)
	free(self->base);
    self->base = NULL;
}

void NameList_clear(struct NameList *self)
{
    NameList_delete(self);
    NameList_init(self);
}

static void NameList_resizefor(struct NameList *self, int size)
{
    int capacity = size * (IDLEN+1);
#define MIN_CAPACITY 4096
    if (capacity == 0) {
	if(self->base) free(self->base);
	self->base = NULL;
	self->capacity = 0;
    } else {
	int old_capacity = self->capacity;
	assert(capacity > 0);
	if (self->capacity == 0)
	    self->capacity = MIN_CAPACITY;
	if (self->capacity > capacity && self->capacity > MIN_CAPACITY)
	    self->capacity /= 2;
	if (self->capacity < capacity)
	    self->capacity *= 2;

	if(old_capacity != self->capacity || self->base == NULL) {
	    char (*tmp)[IDLEN+1] = (char(*)[IDLEN+1])malloc((IDLEN+1)*self->capacity);
	    assert(tmp);
	    if (self->size)
		memcpy(tmp, self->base, (IDLEN+1)*self->size);
	    if (self->base)
		free(self->base);
	    self->base = tmp;
	}
    }
}

void NameList_add(struct NameList *self, const char *name)
{
    NameList_resizefor(self, self->size+1);
    strlcpy(self->base[self->size], name, IDLEN+1);
    self->size++;
}

const char* NameList_get(struct NameList *self, int idx)
{
    assert(0<=idx && idx<self->size);
    return self->base[idx];
}

static int NameList_MaxLen(const struct NameList *list, int offset, int count)
{
    int i;
    int maxlen = 0;

    for(i=offset; i<list->size; i++) {
	int len = strlen(list->base[i]);
	if (len > maxlen)
	    maxlen = len;
    }
    assert(maxlen <= IDLEN);
    return maxlen;
}

int NameList_match(const struct NameList *src, struct NameList *dst, int key, int pos)
{
    int uckey, lckey;
    int i;

    NameList_clear(dst);

    uckey = chartoupper(key);
    if (key >= 'A' && key <= 'Z')
	lckey = key | 0x20;
    else
	lckey = key;

    for(i=0; i<src->size; i++) {
	int ch = src->base[i][pos];
	if (ch == lckey || ch == uckey)
	    NameList_add(dst, src->base[i]);
    }

    return dst->size;
}

int NameList_length(struct NameList *self)
{
    return self->size;
}

void NameList_sublist(struct NameList *src, struct NameList *dst, char *tag)
{
    int i;
    int len;
    NameList_clear(dst);

    len = strlen(tag);
    for(i=0; i<src->size; i++)
	if(len==0 || strncasecmp(src->base[i], tag, len)==0)
	    NameList_add(dst, src->base[i]);
}

int NameList_remove(struct NameList *self, const char *name)
{
    int i;
    for(i=0; i<self->size; i++)
	if(strcasecmp(self->base[i], name)==0) {
	    strcpy(self->base[i], self->base[self->size-1]);

	    self->size--;
	    NameList_resizefor(self, self->size);
	    return 1;
	}
    return 0;
}

int NameList_search(const struct NameList *self, const char *name)
{
    int i;
    for(i=0; i<self->size; i++)
	if (strcasecmp(self->base[i], name)==0)
	    return 1;
    return 0;
}

//-----------------------------------------------------------------------

static int
UserMaxLen(char cwlist[][IDLEN + 1], int cwnum, int morenum,
	   int count)
{
    int             len, max = 0;

    while (count-- > 0 && morenum < cwnum) {
	len = strlen(cwlist[morenum++]);
	if (len > max)
	    max = len;
    }
    /* assert max IDLEN */
    if(max > IDLEN)
	max = IDLEN+1;
    return max;
}

static int
UserSubArray(char cwbuf[][IDLEN + 1], char cwlist[][IDLEN + 1],
	     int cwnum, int key, int pos)
{
    int             key2, num = 0;
    int             n, ch;

    key = chartoupper(key);

    if (key >= 'A' && key <= 'Z')
	key2 = key | 0x20;
    else
	key2 = key;

    for (n = 0; n < cwnum; n++) {
	ch = cwlist[n][pos];
	if (ch == key || ch == key2)
	    strlcpy(cwbuf[num++], cwlist[n], sizeof(cwbuf[num]));
    }
    return num;
}

void
FreeNameList(void)
{
    word_t         *p, *temp;

    for (p = toplev; p; p = temp) {
	temp = p->next;
	free(p->word);
	free(p);
    }
}

void
CreateNameList(void)
{
    if (toplev)
	FreeNameList();
    toplev = current = NULL;
}

void
AddNameList(const char *name)
{
    word_t         *node;

    node = (word_t *) malloc(sizeof(word_t));
    node->next = NULL;
    node->word = (char *)malloc(strlen(name) + 1);
    strcpy(node->word, name);

    if (toplev)
	current = current->next = node;
    else
	current = toplev = node;
}

int
RemoveNameList(const char *name)
{
    word_t         *curr, *prev = NULL;

    for (curr = toplev; curr; curr = curr->next) {
	if (!strcmp(curr->word, name)) {
	    if (prev == NULL)
		toplev = curr->next;
	    else
		prev->next = curr->next;

	    if (curr == current)
		current = prev;
	    free(curr->word);
	    free(curr);
	    return 1;
	}
	prev = curr;
    }
    return 0;
}

static inline int
InList(const word_t * list, const char *name)
{
    const word_t         *p;

    for (p = list; p; p = p->next)
	if (!strcasecmp(p->word, name))
	    return 1;
    return 0;
}

int
InNameList(const char *name)
{
    return InList(toplev, name);
}

void
ShowNameList(int row, int column, const char *prompt)
{
    word_t         *p;

    move(row, column);
    clrtobot();
    outs(prompt);

    column = 80;
    for (p = toplev; p; p = p->next) {
	row = strlen(p->word) + 1;
	if (column + row > 76) {
	    column = row;
	    outc('\n');
	} else {
	    column += row;
	    outc(' ');
	}
	outs(p->word);
    }
}

void
ToggleNameList(int *reciper, const char *listfile, const char *msg)
{
    FILE           *fp;
    char            genbuf[200];

    if ((fp = fopen(listfile, "r"))) {
	while (fgets(genbuf, STRLEN, fp)) {
	    char *space = strpbrk(genbuf, str_space);
	    if (space) *space = '\0';
	    if (!genbuf[0])
		continue;
	    if (!InNameList(genbuf)) {
		AddNameList(genbuf);
		(*reciper)++;
	    } else {
		RemoveNameList(genbuf);
		(*reciper)--;
	    }
	}
	fclose(fp);
	ShowNameList(3, 0, msg);
    }
}

static int
NumInList(const word_t * list)
{
    register int    i;

    for (i = 0; list; i++)
	list = list->next;
    return i;
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

static word_t  *
GetSubList(char *tag, word_t * list)
{
    word_t         *wlist, *wcurr;
    char            tagbuf[STRLEN];
    int             n;

    wlist = wcurr = NULL;
    for (n = 0; tag[n]; n++)
	tagbuf[n] = chartoupper(tag[n]);
    tagbuf[n] = '\0';

    while (list) {
	if (chkstr(tag, tagbuf, list->word)) {
	    register word_t *node;

	    node = (word_t *) malloc(sizeof(word_t));
	    node->word = list->word;
	    node->next = NULL;
	    if (wlist)
		wcurr->next = node;
	    else
		wlist = node;
	    wcurr = node;
	}
	list = list->next;
    }
    return wlist;
}

static void
ClearSubList(word_t * list)
{
    struct word_t  *tmp_list;

    while (list) {
	tmp_list = list->next;
	free(list);
	list = tmp_list;
    }
}

static int
MaxLen(const word_t * list, int count)
{
    int             len = strlen(list->word);
    int             t;

    while (list && count) {
	if ((t = strlen(list->word)) > len)
	    len = t;
	list = list->next;
	count--;
    }
    return len;
}

/* TODO use namecomplete2() instead */
void
namecomplete(const char *prompt, char *data)
{
    char           *temp;
    word_t         *cwlist, *morelist;
    int             x, y, origx, origy;
    int             ch;
    int             count = 0;
    int             clearbot = NA;

    if (toplev == NULL)
	AddNameList("");
    cwlist = GetSubList("", toplev);
    morelist = NULL;
    temp = data;

    outs(prompt);
    clrtoeol();
    getyx(&y, &x);
    standout();
    prints("%*s", IDLEN + 1, "");
    standend();
    move(y, x);
    origy = y; origx = x;

    while ((ch = igetch()) != EOF) {
	if (ch == '\n' || ch == '\r') {
	    *temp = '\0';
	    // outc('\n');
	    if (NumInList(cwlist) == 1)
		strcpy(data, cwlist->word);
	    else if (!InList(cwlist, data))
		data[0] = '\0';
	    ClearSubList(cwlist);
	    break;
	}
	if (ch == ' ') {
	    int             col, len;

	    if (NumInList(cwlist) == 1) {
		strcpy(data, cwlist->word);
		move(y, x);
		outs(data + count);
		count = strlen(data);
		temp = data + count;
		getyx(&y, &x);
		continue;
	    }
	    clearbot = YEA;
	    col = 0;
	    if (!morelist)
		morelist = cwlist;
	    len = MaxLen(morelist, p_lines);
	    move(2, 0);
	    clrtobot();
	    printdash("相關資訊一覽表", 0);
	    while (len + col < t_columns) {
		int             i;

		for (i = p_lines; (morelist) && (i > 0); i--) {
		    move(3 + (p_lines - i), col);
		    outs(morelist->word);
		    morelist = morelist->next;
		}
		col += len + 2;
		if (!morelist)
		    break;
		len = MaxLen(morelist, p_lines);
	    }
	    if (morelist) {
		vmsg(msg_more);
	    }
	    move(y, x);
	    continue;
	}
	if (ch == '\177' || ch == '\010') {
	    if (temp == data)
		continue;
	    temp--;
	    count--;
	    *temp = '\0';
	    ClearSubList(cwlist);
	    cwlist = GetSubList(data, toplev);
	    morelist = NULL;
	    x--;
	    move(y, x);
	    outc(' ');
	    move(y, x);
	    continue;
	}
	if (count < STRLEN && isprint(ch)) {
	    word_t         *node;

	    *temp++ = ch;
	    count++;
	    *temp = '\0';
	    node = GetSubList(data, cwlist);
	    if (node == NULL) {
		temp--;
		*temp = '\0';
		count--;
		continue;
	    }
	    ClearSubList(cwlist);
	    cwlist = node;
	    morelist = NULL;
	    move(y, x);
	    outc(ch);
	    x++;
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
	move(origy, origx);
	outs(data);
	outc('\n');
    }
}

void
namecomplete2(struct NameList *namelist, const char *prompt, char *data)
{
    char           *temp;
    int             x, y, origx, origy;
    int             ch;
    int             count = 0;
    int             clearbot = NA;
    struct NameList sublist;
    int viewoffset = 0;

    NameList_init(&sublist);

    NameList_sublist(namelist, &sublist, "");
    temp = data;

    outs(prompt);
    clrtoeol();
    getyx(&y, &x);
    standout();
    prints("%*s", IDLEN + 1, "");
    standend();
    move(y, x);
    origy = y; origx = x;
    viewoffset = 0;

    while ((ch = igetch()) != EOF) {
	if (ch == '\n' || ch == '\r') {
	    *temp = '\0';
	    if (NameList_length(&sublist)==1)
		strcpy(data, NameList_get(&sublist, 0));
	    else if (!NameList_search(&sublist, data))
		data[0] = '\0';
	    NameList_delete(&sublist);
	    break;
	}
	if (ch == ' ') {
	    int             col, len;

	    if (NameList_length(&sublist) == 1) {
		strcpy(data, NameList_get(&sublist, 0));
		move(y, x);
		outs(data + count);
		count = strlen(data);
		temp = data + count;
		getyx(&y, &x);
		continue;
	    }
	    clearbot = YEA;
	    col = 0;
	    len = NameList_MaxLen(&sublist, viewoffset, p_lines);
	    move(2, 0);
	    clrtobot();
	    printdash("相關資訊一覽表", 0);
	    while (len + col < t_columns) {
		int             i;

		for (i = p_lines; viewoffset < NameList_length(&sublist) && (i > 0); i--) {
		    move(3 + (p_lines - i), col);
		    outs(NameList_get(&sublist, viewoffset));
		    viewoffset++;
		}
		col += len + 2;
		if (viewoffset == NameList_length(&sublist)) {
		    viewoffset = 0;
		    break;
		}
		len = NameList_MaxLen(&sublist, viewoffset, p_lines);
	    }
	    if (viewoffset < NameList_length(&sublist)) {
		vmsg(msg_more);
	    }
	    move(y, x);
	    continue;
	}
	if (ch == '\177' || ch == '\010') {
	    if (temp == data)
		continue;
	    temp--;
	    count--;
	    *temp = '\0';
	    NameList_sublist(namelist, &sublist, data);
	    viewoffset = 0;
	    x--;
	    move(y, x);
	    outc(' ');
	    move(y, x);
	    continue;
	}
	if (count < STRLEN && isprint(ch)) {
	    struct NameList tmplist;
	    NameList_init(&tmplist);

	    *temp++ = ch;
	    count++;
	    *temp = '\0';

	    NameList_sublist(&sublist, &tmplist, data);
	    if (NameList_length(&tmplist)==0) {
		NameList_delete(&tmplist);
		temp--;
		*temp = '\0';
		count--;
		continue;
	    }
	    NameList_delete(&sublist);
	    sublist = tmplist;
	    viewoffset = 0;
	    move(y, x);
	    outc(ch);
	    x++;
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
	move(origy, origx);
	outs(data);
	outc('\n');
    }
}

void
usercomplete(const char *prompt, char *data)
{
    char           *temp;
    char           *cwbuf, *cwlist;
    int             cwnum, x, y, origx, origy;
    int             clearbot = NA, count = 0, morenum = 0;
    char            ch;
    int		    dashdirty = 0;

    /* TODO 節省記憶體. (不過這個 function 不常占記憶體...) */
    cwbuf = malloc(MAX_USERS * (IDLEN + 1));
    cwlist = u_namearray((arrptr) cwbuf, &cwnum, "");
    temp = data;

    outs(prompt);
    clrtoeol();
    getyx(&y, &x);
    standout();
    prints("%*s", IDLEN + 1, "");
    standend();
    move(y, x);
    origy = y; origx = x;

    while ((ch = igetch()) != EOF) {

	if (ch == '\n' || ch == '\r') {
	    int             i;
	    char           *ptr;

	    *temp = '\0';
	    outc('\n');
	    ptr = cwlist;
	    for (i = 0; i < cwnum; i++) {
		if (strncasecmp(data, ptr, IDLEN + 1) == 0) {
		    strcpy(data, ptr);
		    break;
		}
		ptr += IDLEN + 1;
	    }
	    if (i == cwnum)
		data[0] = '\0';
	    break;

	} else if (ch == '\177' || ch == '\010') {
	    if (temp == data)
		continue;
	    temp--;
	    count--;
	    *temp = '\0';
	    cwlist = u_namearray((arrptr) cwbuf, &cwnum, data);
	    morenum = 0;
	    x--;
	    move(y, x);
	    outc(' ');
	    move(y, x);
	    continue;

	} else if (!(count <= IDLEN && isprint((int)ch))) {

	    /* invalid input */
	    continue;

	} else if (ch != ' ') {

	    int n;

	    *temp++ = ch;
	    *temp = '\0';
	    n = UserSubArray((arrptr) cwbuf, (arrptr) cwlist, cwnum, ch, count);

	    if (n > 0) {
		/* found something */
		cwlist = cwbuf;
		count++;
		cwnum = n;
		morenum = 0;
		move(y, x);
		outc(ch);
		x++;

		continue;
	    }
	    /* no break, no continue, list later. */
	}

	/* finally, list available users. */
	{
	    int             col, len;

	    if (ch == ' ' && cwnum == 1) {
		if(dashdirty)
		{
		    move(2,0);
		    clrtoeol();
		    printdash(cwlist, 0);
		}
		strcpy(data, cwlist);
		move(y, x);
		outs(data + count);
		count = strlen(data);
		temp = data + count;
		getyx(&y, &x);
		continue;
	    }

	    clearbot = YEA;
	    col = 0;

	    len = UserMaxLen((arrptr) cwlist, cwnum, morenum, p_lines);
	    move(2, 0);
	    clrtobot();
	    printdash("使用者代號一覽表", 0);
	    dashdirty = 0;

	    if(ch != ' ')
	    {
		/* no such user */
		move(2,0);
		outs("- 目前無使用者 ");
		outs(data);
		outs(" ");
		temp--;
		*temp = '\0';
		dashdirty = 1;
	    }

	    while (len + col < t_columns-1) {

		int i;

		for (i = 0; morenum < cwnum && i < p_lines; i++) {
		    move(3 + i, col);
		    prints("%.*s ", IDLEN,
			    cwlist + (IDLEN + 1) * morenum++);
		}
		col += len + 2;
		if (morenum >= cwnum)
		    break;
		len = UserMaxLen((arrptr) cwlist, cwnum, morenum, p_lines);
	    }
	    if (morenum < cwnum) {
		move(b_lines, 0); clrtoeol();
		outs(msg_more);
		// vmsg(msg_more);
	    } else
		morenum = 0;

	    move(y, x);

	    continue;
	}
    }
    free(cwbuf);
    if (ch == EOF)
	/* longjmp(byebye, -1); */
	raise(SIGHUP);		/* jochang: don't know if this is necessary */
    outc('\n');
    if (clearbot) {
	move(2, 0);
	clrtobot();
    }
    if (*data) {
	move(origy, origx);
	outs(data);
	outc('\n');
    }
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
    int             x, y, origx, origy, ch, i, morelist = -1, col, ret = -1;
    int             start, end, ptr;
    int             clearbot = NA;

    outs(prompt);
    clrtoeol();
    getyx(&y, &x);
    standout();
    prints("%*s", IDLEN + 1, "");
    standend();
    move(y, x);
    origy = y; origx = x;

    ptr = 0;
    data[ptr] = 0;

    start = 0; end = nmemb - 1;
    while ((ch = igetch()) != EOF) {
	if (ch == '\n' || ch == '\r') {
	    data[ptr] = 0;
	    outc('\n');
	    if (ptr != 0) {
		gnc_findbound(data, &start, &end, nmemb, compar);
		if (gnc_complete(data, &start, &end, permission, getname)
			== 1 || (*compar)(start, data, len) == 0) {
		    strcpy(data, (*getname)(start));
		    ret = start;
		} else {
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
		    move(origy, origx);
		    outs(data);
		    ptr = strlen(data);
		    getyx(&y, &x);
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
			move(y, x);
			outs(data + ptr);
			getyx(&y, &x);
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
	    while (len + col < 79) {
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
		vmsg(msg_more);
	    }
	    move(y, x);
	    continue;

	} else if (ch == '\177' || ch == '\010') {	/* backspace */
	    if (ptr == 0)
		continue;
	    morelist = -1;
	    --ptr;
	    --x;
	    data[ptr] = 0;
	    move(y, x);
	    outc(' ');
	    move(y, x);
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
		else {
		    move(y, x);
		    outc(ch);
		    x++;
		}
	    }
	}
    }

    outc('\n');
    if (clearbot) {
	move(2, 0);
	clrtobot();
    }
    if (*data) {
	move(origy, origx);
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
	    !(bptr->brdattr & BRD_GROUPBOARD) &&
	    !(bptr->brdattr & BRD_OVER18 && !over18));
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
