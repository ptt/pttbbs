/* $Id$ */
#include "bbs.h"

static fileheader_t *headers = NULL;
static int      last_line; // PTT: last_line 游標可指的最後一個
static int      hit_thread;

#include <sys/mman.h>

/* ----------------------------------------------------- */
/* Tag List 標籤                                         */
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
	if (!comp && recno)	/* 絕對嚴謹：連 recno 一起比對 */
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
    snprintf(fname, sizeof(fname), "M.%d.A", (int)TagList[locus].chrono);
}

void
EnumTagFhdr(fileheader_t * fhdr, char *direct, int locus)
{
    get_record(direct, fhdr, sizeof(fileheader_t), TagList[locus].recno);
}

/* -1 : 取消 */
/* 0 : single article */
/* ow: whole tag list */

int
AskTag(char *msg)
{
    char            buf[80];
    int             num;

    num = TagNum;
    snprintf(buf, sizeof(buf), "◆ %s A)文章 T)標記 Q)uit?", msg);
    switch (getans(buf)) {
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
    boardheader_t  *bp=NULL;
    assert(bid >= 0);   /* bid == 0 means in mailbox */
    if (bid){
	bp = getbcache(bid);
	if (strcmp(bp->brdname, "Security") == 0)
	    return DONOTHING;
    }
    if (TagNum && ((currstat != READING) || (currmode & MODE_BOARD))) {
	if (getans("刪除所有標記[N]?") != 'y')
	    return READ_REDRAW;
#ifdef SAFE_ARTICLE_DELETE
        if(bp && bp->nuser>20)
            safe_delete_range(currdirect, 0, 0);
        else
#endif
	    delete_range(currdirect, 0, 0);
	TagNum = 0;
	if (bid)
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
    assert(p);
    p->key = (char *)malloc(strlen(s) + 1);
    assert(p->key);
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
    int  top=locmem->top_ln;
    if (!last_line)
	{
          cursor_show(3 , 0);
          return DONOTHING;
        }
    if (val > last_line) {
	bell();
	val = last_line;
    }
    if (val <= 0) {
	bell();
	val = 1;
    }
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
thread(keeploc_t * locmem, int stype, int *new_ln)
{
    static char     a_ans[32], t_ans[32];
    char            ans[32], s_pmt[64];
    register char  *tag, *query = NULL;
    register int    now, pos, match, near = 0;
    fileheader_t    fh;
    int             circulate_flag = 1;	/* circulate at end or begin */
    int             fd = -1;

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
		strlcpy(a_ans, currowner, sizeof(a_ans));
	    else if (*currauthor)
		strlcpy(a_ans, currauthor, sizeof(a_ans));
	}
	snprintf(s_pmt, sizeof(s_pmt),
		 "%s搜尋%s [%s] ", (stype & RS_FORWARD) ? "往後" : "往前",
		 (stype & RS_TITLE) ? "標題" : "作者", query);
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
		if (++now > last_line){
		    if( fd != -1 )
			close(fd);
		    return DONOTHING;
		}
	    } else {
		if (--now <= 0 || now < pos - 200) {
		    if( fd )
			close(fd);
		    if ((stype & RS_FIRST) && (near)) {
			hit_thread = 1;
			*new_ln = near;
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

	get_record_keep(currdirect, &fh, sizeof(fileheader_t), now, &fd);

	if (fh.owner[0] == '-')
	    continue;

	if (stype & RS_THREAD) {
	    if (strncasecmp(fh.title, str_reply, 3)) {
		hit_thread = 1;
		if( fd )
		    close(fd);
		*new_ln = now;
                return DONOTHING;
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
	    *new_ln = now;
	    if ((!(stype & RS_CURRENT)) &&
		(stype & RS_RELATED) &&
		strncmp(currtitle, query, TTLEN)) {
		strncpy(currtitle, query, TTLEN);
		match = PARTUPDATE;
	    }
	    break;
	}
    } while (now != pos);

    if( fd != -1 )
	close(fd);
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
	vmsg(msg_fwd_ok);
	break;
    case -1:
	vmsg(msg_fwd_err1);
	break;
    case -2:
	vmsg(msg_fwd_err2);
	break;
    default:
	break;
    }
}
#endif

static int
select_read(keeploc_t * locmem, int sr_mode)
{
#define READSIZE 64  // 8192 / sizeof(fileheader_t)
    char           *tag, *query = NULL, *temp;
    fileheader_t    fhs[READSIZE];
    char            fpath[80], genbuf[MAXPATHLEN], buf3[5];
    static char     t_ans[TTLEN + 1] = "";
    static char     a_ans[TTLEN + 1] = "";
    int             fd, fr, size = sizeof(fileheader_t), i, len;
    struct stat     st;
    /* rocker.011018: make a reference number for process article */
    int             reference = 0;

    if ((currmode & MODE_SELECT))
	return -1;
    if (sr_mode == RS_TITLE)
	query = subject(headers[locmem->crs_ln - locmem->top_ln].title);
    else if (sr_mode == RS_NEWPOST) {
	strlcpy(buf3, "Re: ", sizeof(buf3));
	query = buf3;
    } 
    else if (sr_mode == RS_THREAD) {
    
    } else {
	char            buff[80];
	char            newdata[35];

	query = (sr_mode == RS_RELATED) ? t_ans : a_ans;
	snprintf(buff, sizeof(buff), "搜尋%s [%s] ",
		 (sr_mode == RS_RELATED) ? "標題" : "作者", query);
	getdata(b_lines, 0, buff, newdata, sizeof(newdata), DOECHO);
	if (newdata[0])
	    strcpy(query, newdata);
	if (!(*query))
	    return DONOTHING;
    }

    if ((fd = open(currdirect, O_RDONLY, 0)) != -1) {
	snprintf(genbuf, sizeof(genbuf), "SR.%s", cuser.userid);
	if (currstat == RMAIL)
	    sethomefile(fpath, cuser.userid, genbuf);
	else
	    setbfile(fpath, currboard, genbuf);
	if (((fr = open(fpath, O_WRONLY | O_CREAT | O_TRUNC, 0600)) != -1)) {
	    switch (sr_mode) {
	    case RS_TITLE:
		while( (len = read(fd, fhs, sizeof(fhs))) > 0 ){
		    len /= sizeof(fileheader_t);
		    for( i = 0 ; i < len ; ++i ){
			++reference;
			tag = subject(fhs[i].title);
			if (!strncmp(tag, query, 40)) {
			    fhs[i].money = reference | FHR_REFERENCE;
			    write(fr, &fhs[i], size);
			}
		    }
		}
		break;
	    case RS_RELATED:
		while( (len = read(fd, fhs, sizeof(fhs))) > 0 ){
		    len /= sizeof(fileheader_t);
		    for( i = 0 ; i < len ; ++i ){
			++reference;
			tag = fhs[i].title;
			if (strcasestr(tag, query)) {
			    fhs[i].money = reference | FHR_REFERENCE;
			    write(fr, &fhs[i], size);
			}
		    }
		}
		break;
	    case RS_NEWPOST:
		while( (len = read(fd, fhs, sizeof(fhs))) > 0 ){
		    len /= sizeof(fileheader_t);
		    for( i = 0 ; i < len ; ++i ){
			++reference;
			tag = fhs[i].title;
			temp = strstr(tag, query);
			if (temp == NULL || temp != tag) {
			    fhs[i].money = reference | FHR_REFERENCE;
			    write(fr, &fhs[i], size);
			}
		    }
		}
		break;
	    case RS_AUTHOR:
		while( (len = read(fd, fhs, sizeof(fhs))) > 0 ){
		    len /= sizeof(fileheader_t);
		    for( i = 0 ; i < len ; ++i ){
			++reference;
			tag = fhs[i].owner;
			if (strcasestr(tag, query)) {
			    fhs[i].money = reference | FHR_REFERENCE;
			    write(fr, &fhs[i], size);
			}
		    }
		}
		break;
	    case RS_THREAD:
		while( (len = read(fd, fhs, sizeof(fhs))) > 0 ){
		    len /= sizeof(fileheader_t);
		    for( i = 0 ; i < len ; ++i ){
			++reference;
			if (fhs[i].filemode & FILE_MARKED) {
			    fhs[i].money = reference | FHR_REFERENCE;
			    write(fr, &fhs[i], size);
			}
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
	    strlcpy(currdirect, fpath, sizeof(currdirect));
	}
    }
    return READ_REDRAW;
}

#define select_read_mode(m) select_read(locmem, m) ? NEWDIRECT:READ_REDRAW
static int
i_read_key(onekey_t * rcmdlist, char default_ch, keeploc_t * locmem, 
           int bid, int bottom_line)
{
    int mode = DONOTHING;
    int num;
    char direct[60];
    int ch, new_ln= locmem->crs_ln;
    
    do {
       if(default_ch)
         {
          ch = default_ch;
          default_ch=0;
         }
       else
         {
          if((mode=cursor_pos(locmem, new_ln, 10))!=DONOTHING)
            return mode;
          ch = igetch();
         }
       switch (ch) {
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
                 if((num = search_num(ch, last_line))!=-1)
                       new_ln = num+1; 
                  break;
    	case 'q':
    	case 'e':
    	case KEY_LEFT:
  	  if(currmode & MODE_SELECT){
    		char genbuf[256];
   		 fileheader_t *fhdr = &headers[locmem->crs_ln - locmem->top_ln];
   		 board_select();
    	  	setbdir(genbuf, currboard);
    		locmem = getkeep(genbuf, 0, 1);
   	        locmem->crs_ln = 
                     getindex(genbuf, fhdr->filename, sizeof(fileheader_t));
    		num = locmem->crs_ln - p_lines + 1;
    		locmem->top_ln = num < 1 ? 1 : num;

    	         mode =  NEWDIRECT;
		}
               else
                 mode =  (currmode & MODE_ETC) ? board_etc() :
                         (currmode & MODE_DIGEST) ? board_digest() : DOQUIT;
		break;
        case Ctrl('L'):
	        redoscr();
		break;

        case Ctrl('H'):
    		    mode = select_read_mode(RS_NEWPOST);
	    break;
        case 'a':
        case 'A':
	            mode = select_read_mode(RS_AUTHOR);
        	    break;
        case 'G':
                    mode = select_read_mode(RS_THREAD);
	            break;
        case '/':
        case '?':
                    mode = select_read_mode(RS_RELATED);
                     break;
        case 'S':
                     mode = select_read_mode(RS_TITLE);
	             break;
        case '=':
	       	     mode = thread(locmem, RELATE_FIRST, &new_ln);
	             break;
        case '\\':
	       	     mode = thread(locmem, CURSOR_FIRST, &new_ln);
	             break;
        case ']':
                     mode = thread(locmem, RELATE_NEXT, &new_ln);
                     break;
        case '+':
	             mode = thread(locmem, CURSOR_NEXT, &new_ln);
	             break;
        case '[':
		mode = thread(locmem, RELATE_PREV, &new_ln);
		break;
     	case '-':
		mode = thread(locmem, CURSOR_PREV, &new_ln);
		break;
    	case '<':
    	case ',':
		mode = thread(locmem, THREAD_PREV, &new_ln);
		break;
    	case '.':
    	case '>':
		mode = thread(locmem, THREAD_NEXT, &new_ln);
		break;
    	case 'p':
    	case 'k':
    case KEY_UP:
        new_ln = locmem->crs_ln - 1; 
	break;
    case 'n':
    case 'j':
    case KEY_DOWN:
        new_ln = locmem->crs_ln + 1; 
	break;
    case ' ':
    case KEY_PGDN:
    case 'N':
    case Ctrl('F'):
        new_ln = locmem->crs_ln + p_lines; 
	break;
    case KEY_PGUP:
    case Ctrl('B'):
    case 'P':
        new_ln = locmem->crs_ln - p_lines; 
	break;
    case KEY_END:
    case '$':
	new_ln = last_line;
	break;
    case 'F':
    case 'U':
	if (HAS_PERM(PERM_FORWARD)) {
	    mail_forward(&headers[locmem->crs_ln - locmem->top_ln],
			 currdirect, ch /* == 'U' */ );
	    /* by CharlieL */
	    mode = READ_REDRAW;
	}
	break;
    case Ctrl('Q'):
	mode = my_query(headers[locmem->crs_ln - locmem->top_ln].owner);
    case Ctrl('S'):
	if (HAS_PERM(PERM_ACCOUNTS)) {
	    int             id;
	    userec_t        muser;

	    strlcpy(currauthor,
		    headers[locmem->crs_ln - locmem->top_ln].owner,
		    sizeof(currauthor));
	    stand_title("使用者設定");
	    move(1, 0);
        if ((id = getuser(headers[locmem->crs_ln - locmem->top_ln].owner))) 
            {
		memcpy(&muser, &xuser, sizeof(muser));
		user_display(&muser, 1);
		uinfo_query(&muser, 1, id);
	    }
	    mode = FULLUPDATE;
	}
	break;

	/* rocker.011018: 採用新的tag模式 */
    case 't':
	/* 將原本在 Read() 裡面的 "TagNum = 0" 移至此處 */
	if ((currstat & RMAIL && TagBoard != 0) ||
		(!(currstat & RMAIL) && TagBoard != bid)) {
	    if (currstat & RMAIL)
		TagBoard = 0;
	    else
		TagBoard = bid;
	    TagNum = 0;
	}
	/* rocker.011112: 解決再select mode標記文章的問題 */
	if (Tagger(atoi(headers[locmem->crs_ln - locmem->top_ln].filename + 2),
		   (currmode & MODE_SELECT) ?
	 (headers[locmem->crs_ln - locmem->top_ln].money & ~FHR_REFERENCE) :
		   locmem->crs_ln, TAG_TOGGLE))
            locmem->crs_ln = locmem->crs_ln + 1; 
         mode = PART_REDRAW;
         break;

    case Ctrl('C'):
	if (TagNum) {
	    TagNum = 0;
	    mode = FULLUPDATE;
	}
        break;

    case Ctrl('T'):
	mode = TagThread(currdirect);
        break;
    case Ctrl('D'):
	mode = TagPruner(bid);
        break;
    case '\n':
    case '\r':
    case 'l':
    case KEY_RIGHT:
	ch = 'r';
    default:
	if( ch == 'h' && currmode & (MODE_ETC | MODE_DIGEST) )
	    break;
	if (ch > 0 && ch <= onekey_size) {
	    int (*func)() = rcmdlist[ch - 1];
	    if (func != NULL)
             {
               num  = locmem->crs_ln - bottom_line;
                   
               if (num>0)
                   {
                    sprintf(direct,"%s.bottom", currdirect);
		    mode= (*func)(num, &headers[locmem->crs_ln-locmem->top_ln], 
                                direct);
                   }
               else
                    mode = (*func)(locmem->crs_ln, 
                        &headers[locmem->crs_ln - locmem->top_ln], currdirect);
             }
	}  
    	break;
       } // end switch
    }
    while (mode == DONOTHING);
    return mode;
}

int
get_records_and_bottom(char *direct,  fileheader_t* headers,
                     int recbase, int p_lines, int last_line, int bottom_line)
{
    int n = bottom_line - recbase + 1, rv;
    char directbottom[60];

    if(!last_line) return 0;
    if(n>=p_lines || (currmode & (MODE_SELECT | MODE_DIGEST)))
      return get_records(direct, headers, sizeof(fileheader_t), recbase, 
                  p_lines);

    sprintf(directbottom, "%s.bottom", direct);
    if (n<=0)
      return get_records(directbottom, headers, sizeof(fileheader_t), 1-n, 
                  last_line-recbase + 1);
      
     rv = get_records(direct, headers, sizeof(fileheader_t), recbase, n);

     if(bottom_line<last_line)
       rv += get_records(directbottom, headers+n, sizeof(fileheader_t), 1, 
                            p_lines - n );
     return rv;
} 
void
i_read(int cmdmode, char *direct, void (*dotitle) (), void (*doentry) (), onekey_t * rcmdlist, int bidcache)
{
    keeploc_t      *locmem = NULL;
    int             recbase = 0, mode, lastmode, last_ln;
    int             num = 0, entries = 0, n_bottom=0;
    int             i;
    char            currdirect0[64], default_ch = 0;
    int             last_line0 = last_line;
    int             bottom_line = 0;
    int             hit_thread0 = hit_thread;
    fileheader_t   *headers0 = headers;

    strlcpy(currdirect0, currdirect, sizeof(currdirect0));
#define FHSZ    sizeof(fileheader_t)
    /* Ptt: 這邊 headers 可以針對看板的最後 60 篇做 cache */
    headers = (fileheader_t *) calloc(p_lines, FHSZ);
    strlcpy(currdirect, direct, sizeof(currdirect));
    mode = NEWDIRECT;

    do {
	/* 依據 mode 顯示 fileheader */
	setutmpmode(cmdmode);
	switch (mode) {
	case NEWDIRECT:	/* 第一次載入此目錄 */
	case DIRCHANGED:
	    if (bidcache > 0 && !(currmode & (MODE_SELECT | MODE_DIGEST))){
		if( (last_line = getbtotal(currbid)) == 0 ){
		    setbtotal(currbid);
                    setbottomtotal(currbid);
		    last_line = getbtotal(currbid);
		}
                bottom_line = last_line;
                last_line += (n_bottom = getbottomtotal(currbid)); 
	    }
	    else
		bottom_line = last_line = get_num_records(currdirect, FHSZ);

	    if (mode == NEWDIRECT) {
		num = last_line - p_lines + 1;
		locmem = getkeep(currdirect, num < 1 ? 1 : num, last_line);
	    }
	    recbase = -1;

	case FULLUPDATE:
	    (*dotitle) ();

	case PARTUPDATE:
	    if (last_line < locmem->top_ln + p_lines) {
		if (bidcache > 0 && !(currmode & (MODE_SELECT | MODE_DIGEST)))
		    {
		     bottom_line = getbtotal(currbid);
		     num = bottom_line+getbottomtotal(currbid);
		    }
		else
		    num = get_num_records(currdirect, FHSZ);

		if (last_line != num) {
		    last_line = num;
		    recbase = -1;
		}
	    }
	    if (recbase != locmem->top_ln) {
		recbase = locmem->top_ln;
		if (recbase > last_line) {
		    recbase = last_line - p_lines + 1;
		    if (recbase < 1)
			recbase = 1;
		    locmem->top_ln = recbase;
		}
                entries=get_records_and_bottom(currdirect,
                           headers, recbase, p_lines, last_line, bottom_line);
	    }
	    if (locmem->crs_ln > last_line)
		locmem->crs_ln = last_line;
	    move(3, 0);
	    clrtobot();
	case PART_REDRAW:
	    move(3, 0);
            if(last_line==0)
                  outs("    沒有文章...");
            else
	          for (i = 0; i < entries; i++)
		          (*doentry) (locmem->top_ln + i, &headers[i]);
	case READ_REDRAW:
	    outmsg(curredit & EDIT_ITEM ?
		   "\033[44m 私人收藏 \033[30;47m 繼續? \033[m" :
		   curredit & EDIT_MAIL ? msg_mailer : MSG_POSTER);
	    break;
	case TITLE_REDRAW:
	    (*dotitle) ();
             break;
	}

       mode = i_read_key(rcmdlist, default_ch, locmem, currbid, 
			          bottom_line);
       if(mode == READ_SKIP)
            mode = lastmode;
       // 以下這幾種 mode 要再處理游標
       if(mode == READ_PREV || mode == READ_NEXT || mode == RELATE_PREV ||
          mode == RELATE_FIRST || mode == 'A' || mode == 'a' )
            {
                lastmode = mode;
                last_ln = locmem->crs_ln;

                switch(mode)
                   {
                    case READ_PREV:
                     mode = cursor_pos(locmem, locmem->crs_ln - 1, 10);
                               break;
                    case READ_NEXT:
                     mode = cursor_pos(locmem, locmem->crs_ln + 1, 10);
                               break;
	            case RELATE_PREV:
                     mode = thread(locmem, RELATE_PREV, &locmem->crs_ln);
			       break;
                    case RELATE_NEXT:
                     mode = thread(locmem, RELATE_NEXT, &locmem->crs_ln);
		               break;
                    case RELATE_FIRST:
                     mode = thread(locmem, RELATE_FIRST, &locmem->crs_ln);
		               break;
                    case 'A':
                     mode = thread(locmem, 'A', &locmem->crs_ln);
		               break;
                    case 'a':
                     mode = thread(locmem, 'a', &locmem->crs_ln);
		               break;
                     }
                     if(locmem->crs_ln != last_ln) default_ch = 'r';
           }
       else
           default_ch=0;
    } while (mode != DOQUIT);
#undef  FHSZ

    free(headers);
    last_line = last_line0;
    hit_thread = hit_thread0;
    headers = headers0;
    strlcpy(currdirect, currdirect0, sizeof(currdirect));
    return;
}
