/* $Id: read.c,v 1.8 2002/07/05 17:10:28 in2 Exp $ */
#include "bbs.h"

#define MAXPATHLEN 256

static fileheader_t *headers = NULL;
static int      last_line;
static int      hit_thread;

#include <sys/mman.h>

/* ----------------------------------------------------- */
/* Tag List ¼ÐÅÒ                                         */
/* ----------------------------------------------------- */
void
UnTagger(int locus)
{
    if (locus > TagNum)
	return;

    TagNum--;

    if (TagNum > locus)
	memcpy(&TagList[locus], &TagList[locus + 1],
	       (TagNum - locus) * sizeof(TagItem));
}

int
Tagger(time_t chrono, int recno, int mode)
{
    int             head, tail, posi = 0, comp;

    for (head = 0, tail = TagNum - 1, comp = 1; head <= tail;) {
	posi = (head + tail) >> 1;
	comp = TagList[posi].chrono - chrono;
	if (!comp) {
	    break;
	} else if (comp < 0) {
	    head = posi + 1;
	} else {
	    tail = posi - 1;
	}
    }

    if (mode == TAG_NIN) {
	if (!comp && recno)	/* µ´¹ïÄYÂÔ¡G³s recno ¤@°_¤ñ¹ï */
	    comp = recno - TagList[posi].recno;
	return comp;

    }
    if (!comp) {
	if (mode != TAG_TOGGLE)
	    return NA;

	TagNum--;
	memcpy(&TagList[posi], &TagList[posi + 1],
	       (TagNum - posi) * sizeof(TagItem));
    } else if (TagNum < MAXTAGS) {
	TagItem        *tagp, buf[MAXTAGS];

	tail = (TagNum - head) * sizeof(TagItem);
	tagp = &TagList[head];
	memcpy(buf, tagp, tail);
	tagp->chrono = chrono;
	tagp->recno = recno;
	memcpy(++tagp, buf, tail);
	TagNum++;
    } else {
	bell();
	return 0;		/* full */
    }
    return YEA;
}


void
EnumTagName(char *fname, int locus)
{
    sprintf(fname, "M.%d.A", (int)TagList[locus].chrono);
}

void
EnumTagFhdr(fileheader_t * fhdr, char *direct, int locus)
{
    get_record(direct, fhdr, sizeof(fileheader_t), TagList[locus].recno);
}

/* -1 : ¨ú®ø */
/* 0 : single article */
/* ow: whole tag list */

int
AskTag(char *msg)
{
    char            buf[80];
    int             num;

    num = TagNum;
    sprintf(buf, "¡» %s A)¤å³¹ T)¼Ð°O Q)uit?", msg);
    switch (rget(b_lines - 1, buf)) {
    case 'q':
	num = -1;
	break;
    case 'a':
	num = 0;
    }
    return num;
}


#include <sys/mman.h>

#define BATCH_SIZE      65536

char           *
f_map(char *fpath, int *fsize)
{
    int             fd, size;
    struct stat     st;

    if ((fd = open(fpath, O_RDONLY)) < 0)
	return (char *)-1;

    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || (size = st.st_size) <= 0) {
	close(fd);
	return (char *)-1;
    }
    fpath = (char *)mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    *fsize = size;
    return fpath;
}


static int
TagThread(char *direct)
{
    int             fsize, count;
    char           *title, *fimage;
    fileheader_t   *head, *tail;

    fimage = f_map(direct, &fsize);
    if (fimage == (char *)-1)
	return DONOTHING;

    head = (fileheader_t *) fimage;
    tail = (fileheader_t *) (fimage + fsize);
    count = 0;
    do {
	count++;
	title = subject(head->title);
	if (!strncmp(currtitle, title, TTLEN)) {
	    if (!Tagger(atoi(head->filename + 2), count, TAG_INSERT))
		break;
	}
    } while (++head < tail);

    munmap(fimage, fsize);
    return FULLUPDATE;
}


int
TagPruner(int bid)
{
    boardheader_t  *bp;
    bp = getbcache(bid);
    if (strcmp(bp->brdname, "Security") == 0)
	return DONOTHING;
    if (TagNum && ((currstat != READING) || (currmode & MODE_BOARD))) {
	if (getans("§R°£©Ò¦³¼Ð°O[N]?") != 'y')
	    return FULLUPDATE;
	delete_range(currdirect, 0, 0);
	TagNum = 0;
	if (bid > 0);
	setbtotal(bid);
	return NEWDIRECT;
    }
    return DONOTHING;
}


/* ----------------------------------------------------- */
/* cursor & reading record position control              */
/* ----------------------------------------------------- */
keeploc_t      *
getkeep(char *s, int def_topline, int def_cursline)
{
    static struct keeploc_t *keeplist = NULL;
    struct keeploc_t *p;
    void           *malloc();

    if (def_cursline >= 0)
	for (p = keeplist; p; p = p->next) {
	    if (!strcmp(s, p->key)) {
		if (p->crs_ln < 1)
		    p->crs_ln = 1;
		return p;
	    }
	}
    else
	def_cursline = -def_cursline;
    p = (keeploc_t *) malloc(sizeof(keeploc_t));
    p->key = (char *)malloc(strlen(s) + 1);
    strcpy(p->key, s);
    p->top_ln = def_topline;
    p->crs_ln = def_cursline;
    p->next = keeplist;
    return (keeplist = p);
}

void 
fixkeep(char *s, int first)
{
    keeploc_t      *k;

    k = getkeep(s, 1, 1);
    if (k->crs_ln >= first) {
	k->crs_ln = (first == 1 ? 1 : first - 1);
	k->top_ln = (first < 11 ? 1 : first - 10);
    }
}

/* calc cursor pos and show cursor correctly */
static int 
cursor_pos(keeploc_t * locmem, int val, int from_top)
{
    int             top;

    if (val > last_line) {
	bell();
	val = last_line;
    }
    if (val <= 0) {
	bell();
	val = 1;
    }
    top = locmem->top_ln;
    if (val >= top && val < top + p_lines) {
	cursor_clear(3 + locmem->crs_ln - top, 0);
	locmem->crs_ln = val;
	cursor_show(3 + val - top, 0);
	return DONOTHING;
    }
    locmem->top_ln = val - from_top;
    if (locmem->top_ln <= 0)
	locmem->top_ln = 1;
    locmem->crs_ln = val;
    return PARTUPDATE;
}

static int 
move_cursor_line(keeploc_t * locmem, int mode)
{
    int             top, crs;
    int             reload = 0;

    top = locmem->top_ln;
    crs = locmem->crs_ln;
    if (mode == READ_PREV) {
	if (crs <= top) {
	    top -= p_lines - 1;
	    if (top < 1)
		top = 1;
	    reload = 1;
	}
	if (--crs < 1) {
	    crs = 1;
	    reload = -1;
	}
    } else if (mode == READ_NEXT) {
	if (crs >= top + p_lines - 1) {
	    top += p_lines - 1;
	    reload = 1;
	}
	if (++crs > last_line) {
	    crs = last_line;
	    reload = -1;
	}
    }
    locmem->top_ln = top;
    locmem->crs_ln = crs;
    return reload;
}

static int 
thread(keeploc_t * locmem, int stype)
{
    static char     a_ans[32], t_ans[32];
    char            ans[32], s_pmt[64];
    register char  *tag, *query = NULL;
    register int    now, pos, match, near = 0;
    fileheader_t    fh;
    int             circulate_flag = 1;	/* circulate at end or begin */

    match = hit_thread = 0;
    now = pos = locmem->crs_ln;
    if (stype == 'A') {
	if (!*currowner)
	    return DONOTHING;
	str_lower(a_ans, currowner);
	query = a_ans;
	circulate_flag = 0;
	stype = 0;
    } else if (stype == 'a') {
	if (!*currowner)
	    return DONOTHING;
	str_lower(a_ans, currowner);
	query = a_ans;
	circulate_flag = 0;
	stype = RS_FORWARD;
    } else if (stype == '/') {
	if (!*t_ans)
	    return DONOTHING;
	query = t_ans;
	circulate_flag = 0;
	stype = RS_TITLE | RS_FORWARD;
    } else if (stype == '?') {
	if (!*t_ans)
	    return DONOTHING;
	circulate_flag = 0;
	query = t_ans;
	stype = RS_TITLE;
    } else if (stype & RS_RELATED) {
	tag = headers[pos - locmem->top_ln].title;
	if (stype & RS_CURRENT) {
	    if (stype & RS_FIRST) {
		if (!strncmp(currtitle, tag, TTLEN))
		    return DONOTHING;
		near = 0;
	    }
	    query = currtitle;
	} else {
	    query = subject(tag);
	    if (stype & RS_FIRST) {
		if (query == tag)
		    return DONOTHING;
		near = 0;
	    }
	}
    } else if (!(stype & RS_THREAD)) {
	query = (stype & RS_TITLE) ? t_ans : a_ans;
	if (!*query && query == a_ans) {
	    if (*currowner)
		strcpy(a_ans, currowner);
	    else if (*currauthor)
		strcpy(a_ans, currauthor);
	}
	sprintf(s_pmt, "%s·j´M%s [%s] ", (stype & RS_FORWARD) ? "©¹«á" : "©¹«e",
		(stype & RS_TITLE) ? "¼ÐÃD" : "§@ªÌ", query);
	getdata(b_lines - 1, 0, s_pmt, ans, sizeof(ans), DOECHO);
	if (*ans)
	    strcpy(query, ans);
	else if (*query == '\0')
	    return DONOTHING;
    }
    tag = fh.owner;

    do {
	if (!circulate_flag || stype & RS_RELATED) {
	    if (stype & RS_FORWARD) {
		if (++now > last_line)
		    return DONOTHING;
	    } else {
		if (--now <= 0 || now < pos - 300) {
		    if ((stype & RS_FIRST) && (near)) {
			hit_thread = 1;
			return cursor_pos(locmem, near, 10);
		    }
		    return DONOTHING;
		}
	    }
	} else {
	    if (stype & RS_FORWARD) {
		if (++now > last_line)
		    now = 1;
	    } else if (--now <= 0)
		now = last_line;
	}

	get_record(currdirect, &fh, sizeof(fileheader_t), now);

	if (fh.owner[0] == '-')
	    continue;

	if (stype & RS_THREAD) {
	    if (strncasecmp(fh.title, str_reply, 3)) {
		hit_thread = 1;
		return cursor_pos(locmem, now, 10);
	    }
	    continue;
	}
	if (stype & RS_TITLE)
	    tag = subject(fh.title);

	if (((stype & RS_RELATED) && !strncmp(tag, query, 40)) ||
	    (!(stype & RS_RELATED) && ((query == currowner) ?
				       !strcmp(tag, query) :
				       strstr_lower(tag, query)))) {
	    if ((stype & RS_FIRST) && tag != fh.title) {
		near = now;
		continue;
	    }
	    hit_thread = 1;
	    match = cursor_pos(locmem, now, 10);
	    if ((!(stype & RS_CURRENT)) &&
		(stype & RS_RELATED) &&
		strncmp(currtitle, query, TTLEN)) {
		strncpy(currtitle, query, TTLEN);
		match = PARTUPDATE;
	    }
	    break;
	}
    } while (now != pos);

    return match;
}


#ifdef INTERNET_EMAIL
static void 
mail_forward(fileheader_t * fhdr, char *direct, int mode)
{
    int             i;
    char            buf[STRLEN];
    char           *p;

    strncpy(buf, direct, sizeof(buf));
    if ((p = strrchr(buf, '/')))
	*p = '\0';
    switch (i = doforward(buf, fhdr, mode)) {
    case 0:
	outmsg(msg_fwd_ok);
	break;
    case -1:
	outmsg(msg_fwd_err1);
	break;
    case -2:
	outmsg(msg_fwd_err2);
	break;
    default:
	break;
    }
    refresh();
    sleep(1);
}
#endif

static int 
select_read(keeploc_t * locmem, int sr_mode)
{
    register char  *tag, *query, *temp;
    fileheader_t    fh;
    char            fpath[80], genbuf[MAXPATHLEN], buf3[5];
    char static     t_ans[TTLEN + 1] = "";
    char static     a_ans[TTLEN + 1] = "";
    int             fd, fr, size = sizeof(fileheader_t);
    struct stat     st;
    /* rocker.011018: make a reference number for process article */
    int             reference = 0;

    if ((currmode & MODE_SELECT))
	return -1;
    if (sr_mode == RS_TITLE)
	query = subject(headers[locmem->crs_ln - locmem->top_ln].title);
    else if (sr_mode == RS_NEWPOST) {
	strcpy(buf3, "Re: ");
	query = buf3;
    } else {
	char            buff[80];
	char            newdata[35];

	query = (sr_mode == RS_RELATED) ? t_ans : a_ans;
	sprintf(buff, "·j´M%s [%s] ",
		(sr_mode == RS_RELATED) ? "¼ÐÃD" : "§@ªÌ", query);
	getdata(b_lines, 0, buff, newdata, sizeof(newdata), DOECHO);
	if (newdata[0])
	    strcpy(query, newdata);
	if (!(*query))
	    return DONOTHING;
    }

    if ((fd = open(currdirect, O_RDONLY, 0)) != -1) {
	sprintf(genbuf, "SR.%s", cuser.userid);
	if (currstat == RMAIL)
	    sethomefile(fpath, cuser.userid, genbuf);
	else
	    setbfile(fpath, currboard, genbuf);
	if (((fr = open(fpath, O_WRONLY | O_CREAT | O_TRUNC, 0600)) != -1)) {
	    switch (sr_mode) {
	    case RS_TITLE:
		while (read(fd, &fh, size) == size) {
		    ++reference;
		    tag = subject(fh.title);
		    if (!strncmp(tag, query, 40)) {
			fh.money = reference | FHR_REFERENCE;
			write(fr, &fh, size);
		    }
		}
		break;
	    case RS_RELATED:
		while (read(fd, &fh, size) == size) {
		    ++reference;
		    tag = fh.title;
		    if (strcasestr(tag, query)) {
			fh.money = reference | FHR_REFERENCE;
			write(fr, &fh, size);
		    }
		}
		break;
	    case RS_NEWPOST:
		while (read(fd, &fh, size) == size) {
		    ++reference;
		    tag = fh.title;
		    temp = strstr(tag, query);
		    if (temp == NULL || temp != tag) {
			write(fr, &fh, size);
			fh.money = reference | FHR_REFERENCE;
		    }
		}
	    case RS_AUTHOR:
		while (read(fd, &fh, size) == size) {
		    ++reference;
		    tag = fh.owner;
		    if (strcasestr(tag, query)) {
			write(fr, &fh, size);
			fh.money = reference | FHR_REFERENCE;
		    }
		}
		break;
	    }
	    fstat(fr, &st);
	    close(fr);
	}
	close(fd);
	if (st.st_size) {
	    currmode |= MODE_SELECT;
	    strcpy(currdirect, fpath);
	}
    }
    return st.st_size;
}

static int 
i_read_key(onekey_t * rcmdlist, keeploc_t * locmem, int ch, int bid)
{
    int             i, mode = DONOTHING;

    switch (ch) {
    case 'q':
    case 'e':
    case KEY_LEFT:
	return (currmode & MODE_SELECT) ? board_select() :
	    (currmode & MODE_ETC) ? board_etc() :
	    (currmode & MODE_DIGEST) ? board_digest() : DOQUIT;
    case Ctrl('L'):
	redoscr();
	break;
	/*
	 * case Ctrl('C'): cal(); return FULLUPDATE; break;
	 */
    case KEY_ESC:
	if (KEY_ESC_arg == 'i') {
	    t_idle();
	    return FULLUPDATE;
	}
	break;
    case Ctrl('H'):
	if (select_read(locmem, RS_NEWPOST))
	    return NEWDIRECT;
	else
	    return READ_REDRAW;
    case 'a':
    case 'A':
	if (select_read(locmem, RS_AUTHOR))
	    return NEWDIRECT;
	else
	    return READ_REDRAW;
    case '/':
    case '?':
	if (select_read(locmem, RS_RELATED))
	    return NEWDIRECT;
	else
	    return READ_REDRAW;
    case 'S':
	if (select_read(locmem, RS_TITLE))
	    return NEWDIRECT;
	else
	    return READ_REDRAW;
	/* quick search title first */
    case '=':
	return thread(locmem, RELATE_FIRST);
    case '\\':
	return thread(locmem, CURSOR_FIRST);
	/* quick search title forword */
    case ']':
	return thread(locmem, RELATE_NEXT);
    case '+':
	return thread(locmem, CURSOR_NEXT);
	/* quick search title backword */
    case '[':
	return thread(locmem, RELATE_PREV);
    case '-':
	return thread(locmem, CURSOR_PREV);
    case '<':
    case ',':
	return thread(locmem, THREAD_PREV);
    case '.':
    case '>':
	return thread(locmem, THREAD_NEXT);
    case 'p':
    case 'k':
    case KEY_UP:
	return cursor_pos(locmem, locmem->crs_ln - 1, p_lines - 2);
    case 'n':
    case 'j':
    case KEY_DOWN:
	return cursor_pos(locmem, locmem->crs_ln + 1, 1);
    case ' ':
    case KEY_PGDN:
    case 'N':
    case Ctrl('F'):
	if (last_line >= locmem->top_ln + p_lines) {
	    if (last_line > locmem->top_ln + p_lines)
		locmem->top_ln += p_lines;
	    else
		locmem->top_ln += p_lines - 1;
	    locmem->crs_ln = locmem->top_ln;
	    return PARTUPDATE;
	}
	cursor_clear(3 + locmem->crs_ln - locmem->top_ln, 0);
	locmem->crs_ln = last_line;
	cursor_show(3 + locmem->crs_ln - locmem->top_ln, 0);
	break;
    case KEY_PGUP:
    case Ctrl('B'):
    case 'P':
	if (locmem->top_ln > 1) {
	    locmem->top_ln -= p_lines;
	    if (locmem->top_ln <= 0)
		locmem->top_ln = 1;
	    locmem->crs_ln = locmem->top_ln;
	    return PARTUPDATE;
	}
	break;
    case KEY_END:
    case '$':
	if (last_line >= locmem->top_ln + p_lines) {
	    locmem->top_ln = last_line - p_lines + 1;
	    if (locmem->top_ln <= 0)
		locmem->top_ln = 1;
	    locmem->crs_ln = last_line;
	    return PARTUPDATE;
	}
	cursor_clear(3 + locmem->crs_ln - locmem->top_ln, 0);
	locmem->crs_ln = last_line;
	cursor_show(3 + locmem->crs_ln - locmem->top_ln, 0);
	break;
    case 'F':
    case 'U':
	if (HAS_PERM(PERM_FORWARD)) {
	    mail_forward(&headers[locmem->crs_ln - locmem->top_ln],
			 currdirect, ch /* == 'U' */ );
	    /* by CharlieL */
	    return FULLUPDATE;
	}
	break;
    case Ctrl('Q'):
	return my_query(headers[locmem->crs_ln - locmem->top_ln].owner);
    case Ctrl('S'):
	if (HAS_PERM(PERM_ACCOUNTS)) {
	    int             id;
	    userec_t        muser;

	    strcpy(currauthor, headers[locmem->crs_ln - locmem->top_ln].owner);
	    stand_title("¨Ï¥ÎªÌ³]©w");
	    move(1, 0);
	    if ((id = getuser(headers[locmem->crs_ln - locmem->top_ln].owner))) {
		memcpy(&muser, &xuser, sizeof(muser));
		user_display(&muser, 1);
		uinfo_query(&muser, 1, id);
	    }
	    return FULLUPDATE;
	}
	break;

	/* rocker.011018: ±Ä¥Î·sªºtag¼Ò¦¡ */
    case 't':
	/* rocker.011112: ¸Ñ¨M¦Aselect mode¼Ð°O¤å³¹ªº°ÝÃD */
	if (Tagger(atoi(headers[locmem->crs_ln - locmem->top_ln].filename + 2),
		   (currmode & MODE_SELECT) ?
	 (headers[locmem->crs_ln - locmem->top_ln].money & ~FHR_REFERENCE) :
		   locmem->crs_ln, TAG_TOGGLE))
	    return POS_NEXT;
	return DONOTHING;

    case Ctrl('C'):
	if (TagNum) {
	    TagNum = 0;
	    return FULLUPDATE;
	}
	return DONOTHING;

    case Ctrl('T'):
	return TagThread(currdirect);
    case Ctrl('D'):
	return TagPruner(bid);
    case '\n':
    case '\r':
    case 'l':
    case KEY_RIGHT:
	ch = 'r';
    default:
	for (i = 0; rcmdlist[i].fptr; i++) {
	    if (rcmdlist[i].key == ch) {
		mode = (*(rcmdlist[i].fptr)) (locmem->crs_ln,
					      &headers[locmem->crs_ln -
					       locmem->top_ln], currdirect);
		break;
	    }
	    if (rcmdlist[i].key == 'h')
		if (currmode & (MODE_ETC | MODE_DIGEST))
		    return DONOTHING;
	}
    }
    return mode;
}

void            i_read(int cmdmode, char *direct, void (*dotitle) (), void (*doentry) (), onekey_t * rcmdlist, int bidcache){
    keeploc_t      *locmem = NULL;
    int             recbase = 0, mode, ch;
    int             num = 0, entries = 0;
    int             i;
    int             jump = 0;
    char            genbuf[4];
    char            currdirect0[64];
    int             last_line0 = last_line;
    int             hit_thread0 = hit_thread;
    fileheader_t   *headers0 = headers;

    strcpy(currdirect0, currdirect);
#define FHSZ    sizeof(fileheader_t)
//Ptt:³o Ã äheaders ¥ i ¥ H ° w ¹ ï¬ÝªO ª º³Ì«á60 ½ g ° µcache
	headers = (fileheader_t *) calloc(p_lines, FHSZ);
    strcpy(currdirect, direct);
    mode = NEWDIRECT;

    /* rocker.011018: ¥[¤J·sªºtag¾÷¨î */
    TagNum = 0;

    do {
	/* ¨Ì¾Ú mode Åã¥Ü fileheader */
	setutmpmode(cmdmode);
	switch (mode) {
	case NEWDIRECT:	/* ²Ä¤@¦¸¸ü¤J¦¹¥Ø¿ý */
	case DIRCHANGED:
	    if (bidcache > 0 && !(currmode & (MODE_SELECT | MODE_DIGEST)))
		last_line = getbtotal(currbid);
	    else
		last_line = get_num_records(currdirect, FHSZ);

	    if (mode == NEWDIRECT) {
		if (last_line == 0) {
		    if (curredit & EDIT_ITEM) {
			outs("¨S¦³ª««~");
			refresh();
			goto return_i_read;
		    } else if (curredit & EDIT_MAIL) {
			outs("¨S¦³¨Ó«H");
			refresh();
			goto return_i_read;
		    } else if (currmode & MODE_ETC) {
			board_etc();	/* Kaede */
			outmsg("©|¥¼¦¬¿ý¨ä¥¦¤å³¹");
			refresh();
		    } else if (currmode & MODE_DIGEST) {
			board_digest();	/* Kaede */
			outmsg("©|¥¼¦¬¿ý¤åºK");
			refresh();
		    } else if (currmode & MODE_SELECT) {
			board_select();	/* Leeym */
			outmsg("¨S¦³¦¹¨t¦Cªº¤å³¹");
			refresh();
		    } else {
			getdata(b_lines - 1, 0,
				"¬ÝªO·s¦¨¥ß (P)µoªí¤å³¹ (Q)Â÷¶}¡H[Q] ",
				genbuf, 4, LCECHO);
			if (genbuf[0] == 'p')
			    do_post();
			goto return_i_read;
		    }
		}
		num = last_line - p_lines + 1;
		locmem = getkeep(currdirect, num < 1 ? 1 : num, last_line);
	    }
	    recbase = -1;

	case FULLUPDATE:
	    (*dotitle) ();

	case PARTUPDATE:
	    if (last_line < locmem->top_ln + p_lines) {
		if (bidcache > 0 && !(currmode & (MODE_SELECT | MODE_DIGEST)))
		    num = getbtotal(currbid);
		else
		    num = get_num_records(currdirect, FHSZ);

		if (last_line != num) {
		    last_line = num;
		    recbase = -1;
		}
	    }
	    if (last_line == 0)
		goto return_i_read;
	    else if (recbase != locmem->top_ln) {
		recbase = locmem->top_ln;
		if (recbase > last_line) {
		    recbase = (last_line - p_lines) >> 1;
		    if (recbase < 1)
			recbase = 1;
		    locmem->top_ln = recbase;
		}
		if (bidcache > 0 && !(currmode & (MODE_SELECT | MODE_DIGEST))
		    && (last_line - recbase) < DIRCACHESIZE)
		    entries = get_fileheader_cache(currbid, currdirect, headers,
						   recbase, p_lines);
		else
		    entries = get_records(currdirect, headers, FHSZ, recbase,
					  p_lines);
	    }
	    if (locmem->crs_ln > last_line)
		locmem->crs_ln = last_line;
	    move(3, 0);
	    clrtobot();
	case PART_REDRAW:
	    move(3, 0);
	    for (i = 0; i < entries; i++)
		(*doentry) (locmem->top_ln + i, &headers[i]);
	case READ_REDRAW:
	    outmsg(curredit & EDIT_ITEM ?
		   "\033[44m ¨p¤H¦¬ÂÃ \033[30;47m Ä~Äò? \033[m" :
		   curredit & EDIT_MAIL ? msg_mailer : MSG_POSTER);
	    break;
	case READ_PREV:
	case READ_NEXT:
	case RELATE_PREV:
	case RELATE_NEXT:
	case RELATE_FIRST:
	case POS_NEXT:
	case 'A':
	case 'a':
	case '/':
	case '?':
	    jump = 1;
	    break;
	}

	/* Åª¨úÁä½L¡A¥[¥H³B²z¡A³]©w mode */
	if (!jump) {
	    cursor_show(3 + locmem->crs_ln - locmem->top_ln, 0);
	    ch = egetch();
	    mode = DONOTHING;
	} else
	    ch = ' ';

	if (mode == POS_NEXT) {
	    mode = cursor_pos(locmem, locmem->crs_ln + 1, 1);
	    if (mode == DONOTHING)
		mode = PART_REDRAW;
	    jump = 0;
	} else if (ch >= '0' && ch <= '9') {
	    if ((i = search_num(ch, last_line)) != -1)
		mode = cursor_pos(locmem, i + 1, 10);
	} else {
	    if (!jump)
		mode = i_read_key(rcmdlist, locmem, ch, currbid);
	    while (mode == READ_NEXT || mode == READ_PREV ||
		   mode == RELATE_FIRST || mode == RELATE_NEXT ||
		   mode == RELATE_PREV || mode == THREAD_NEXT ||
		   mode == THREAD_PREV || mode == 'A' || mode == 'a' ||
		   mode == '/' || mode == '?') {
		int             reload;

		if (mode == READ_NEXT || mode == READ_PREV)
		    reload = move_cursor_line(locmem, mode);
		else {
		    reload = thread(locmem, mode);
		    if (!hit_thread) {
			mode = FULLUPDATE;
			break;
		    }
		}

		if (reload == -1) {
		    mode = FULLUPDATE;
		    break;
		} else if (reload) {
		    recbase = locmem->top_ln;

		    if (bidcache > 0 && !(currmode & (MODE_SELECT | MODE_DIGEST))
			&& last_line - recbase < DIRCACHESIZE)
			entries = get_fileheader_cache(currbid, currdirect,
						 headers, recbase, p_lines);
		    else
			entries = get_records(currdirect, headers, FHSZ, recbase,
					      p_lines);

		    if (entries <= 0) {
			last_line = -1;
			break;
		    }
		}
		num = locmem->crs_ln - locmem->top_ln;
		if (headers[num].owner[0] != '-')
		    mode = i_read_key(rcmdlist, locmem, ch, bidcache);
	    }
	}
    } while (mode != DOQUIT);
#undef  FHSZ

return_i_read:
    free(headers);
    last_line = last_line0;
    hit_thread = hit_thread0;
    headers = headers0;
    strcpy(currdirect, currdirect0);
    return;
}
