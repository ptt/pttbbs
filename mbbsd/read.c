#include "bbs.h"

static int headers_size = 0;
static fileheader_t *headers = NULL;
static int      last_line; // PTT: last_line 游標可指的最後一個

#include <sys/mman.h>

/* ----------------------------------------------------- */
/* Tag List 標籤                                         */
/* ----------------------------------------------------- */
static TagItem         *TagList = NULL;	/* ascending list */

int compare_tagitem(const void *pa, const void *pb) {
    TagItem *taga = (TagItem*) pa,
            *tagb = (TagItem*) pb;
    return strcmp(taga->filename, tagb->filename);
}

int IsEmptyTagList() {
    return !TagList || TagNum <= 0;
}

TagItem *FindTaggedItem(const fileheader_t *fh) {
    if (IsEmptyTagList())
        return NULL;

    return (TagItem*)bsearch(
            fh->filename, TagList, TagNum, sizeof(TagItem), compare_tagitem);
}

TagItem *RemoveTagItem(const fileheader_t *fh) {
    TagItem *tag = IsEmptyTagList() ? NULL : FindTaggedItem(fh);
    if (!tag)
        return tag;

    TagNum--;
    memmove(tag, tag + 1, (TagNum - (tag - TagList)) * sizeof(TagItem));
    return tag;
}

TagItem *AddTagItem(const fileheader_t *fh) {
    if (TagNum == MAXTAGS)
        return NULL;
    if(TagList == NULL) {
        const size_t sz = sizeof(TagItem) * (MAXTAGS + 1);
        TagList = (TagItem*) malloc(sz);
        memset(TagList, 0, sz);
    } else {
        memset(TagList+TagNum, 0, sizeof(TagItem));
    }
    // assert(!FindTaggedItem(fh));
    strlcpy(TagList[TagNum++].filename, fh->filename, sizeof(fh->filename));
    qsort(TagList, TagNum, sizeof(TagItem), compare_tagitem);
    return FindTaggedItem(fh);
}

TagItem *ToggleTagItem(const fileheader_t *fh) {
    TagItem *tag = RemoveTagItem(fh);
    if (tag)
        return tag;
    return AddTagItem(fh);
}

static int
_iter_tag_match_title(void *ptr, void *opt) {
    fileheader_t *fh = (fileheader_t*) ptr;
    char *pattern = (char*) opt;
    const char *title = subject(fh->title);

    if (strncmp(pattern, title, TTLEN) != 0)
        return 0;
    if (FindTaggedItem(fh))
        return 0;
    if (!AddTagItem(fh))
        return -1;
    return 0;
}

static int
TagThread(const char *direct)
{
    apply_record(direct, _iter_tag_match_title, sizeof(fileheader_t), currtitle);
    return FULLUPDATE;
}

static int
_iter_delete_tagged(void *ptr, void *opt) {
    fileheader_t *fh = (fileheader_t*) ptr;
    const char *direct = (const char*)opt;

    if ((fh->filemode & FILE_MARKED) ||
        (fh->filemode & FILE_DIGEST))
        return 0;
    if (!FindTaggedItem(fh))
        return 0;
    // only called by home or board, no need for man.
    // so backup_direct can be same as direct.
    delete_file_content(direct, fh, direct, NULL, 0);
    return IsEmptyTagList();
}


int
TagPruner(int bid)
{
    boardheader_t  *bp=NULL;
    char direct[PATHLEN];

    assert(bid >= 0);   /* bid == 0 means in mailbox */
    if (bid && currstat != RMAIL){
	bp = getbcache(bid);
	if (is_readonly_board(bp->brdname))
	    return DONOTHING;
        setbdir(direct, bp->brdname);
    } else if(currstat == RMAIL) {
        sethomedir(direct, cuser.userid);
    } else {
        vmsg("抱歉，程式異常 - 請至 " BN_BUGREPORT " 報告您剛的詳細步驟。");
        return FULLUPDATE;
    }

    if (IsEmptyTagList() || (currstat == READING && !(currmode & MODE_BOARD)))
        return DONOTHING;
    if (vans("刪除所有標記[N]?") != 'y')
        return READ_REDRAW;

    // ready to start.
    outmsg("處理中,請稍後...");
    refresh();

    // first, delete and backup all files
    apply_record(direct, _iter_delete_tagged, sizeof(fileheader_t), direct);

    // now, delete the header
#ifdef SAFE_ARTICLE_DELETE
    if(bp && !(currmode & MODE_DIGEST) &&
       bp->nuser >= SAFE_ARTICLE_DELETE_NUSER)
        safe_delete_range(currdirect, 0, 0);
    else
#endif
        delete_range(currdirect, 0, 0);

    TagNum = 0;
    if (bid)
        setbtotal(bid);
    else if(currstat == RMAIL)
        setupmailusage();

    return NEWDIRECT;
}


/* ----------------------------------------------------- */
/* cursor & reading record position control              */
/* ----------------------------------------------------- */
keeploc_t      *
getkeep(const char *s, int def_topline, int def_cursline)
{
    /* 為省記憶體, 且避免 malloc/free 不成對, getkeep 最好不要 malloc,
     * 只記 s 的 hash 值,
     * fvn1a-32bit collision 機率約小於十萬分之一 */
    /* 原本使用 link list, 可是一方面會造成 malloc/free 不成對,
     * 一方面 size 小, malloc space overhead 就高, 因此改成 link block,
     * 以 KEEPSLOT 為一個 block 的 link list.
     * 只有第一個 block 可能沒滿. */
    /* TODO LRU recycle? 麻煩在於別處可能把 keeploc_t pointer 記著... */
#define KEEPSLOT 10
    struct keepsome {
	unsigned char used;
	keeploc_t arr[KEEPSLOT];
	struct keepsome *next;
    };
    static struct keepsome preserv_keepblock;
    static struct keepsome *keeplist = &preserv_keepblock;
    struct keeploc_t *p;
    unsigned key=StringHash(s);
    int i;

    if (def_cursline >= 0) {
	struct keepsome *kl=keeplist;
	while(kl) {
	    for(i=0; i<kl->used; i++)
		if(key == kl->arr[i].hashkey) {
		    p = &kl->arr[i];
		    if (p->crs_ln < 1)
			p->crs_ln = 1;
		    return p;
		}
	    kl=kl->next;
	}
    } else
	def_cursline = -def_cursline;

    if(keeplist->used==KEEPSLOT) {
	struct keepsome *kl;
	kl = (struct keepsome*)malloc(sizeof(struct keepsome));
	memset(kl, 0, sizeof(struct keepsome));
	kl->next = keeplist;
	keeplist = kl;
    }
    p = &keeplist->arr[keeplist->used];
    keeplist->used++;
    p->hashkey = key;
    p->top_ln = def_topline;
    p->crs_ln = def_cursline;
    return p;
}

void
fixkeep(const char *s, int first)
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
cursor_pos(keeploc_t * locmem, int val, int from_top, int isshow)
{
    int  top=locmem->top_ln;
    if (!last_line){
	cursor_show(3 , 0);
	return DONOTHING;
    }
    if (val > last_line)
	val = last_line;
    if (val <= 0)
	val = 1;
    if (val >= top && val < top + headers_size) {
        if(isshow){
	    if(locmem->crs_ln >= top)
		cursor_clear(3 + locmem->crs_ln - top, 0);
	    cursor_show(3 + val - top, 0);
	}
	locmem->crs_ln = val;
	return DONOTHING;
    }
    locmem->top_ln = val - from_top;
    if (locmem->top_ln <= 0)
	locmem->top_ln = 1;
    locmem->crs_ln = val;
    return isshow ? PARTUPDATE : HEADERS_RELOAD;
}

/**
 * 根據 stypen 選擇上/下一篇文章
 *
 * @param locmem  用來存在某看板游標位置的 structure。
 * @param stypen  游標移動的方法
 *           CURSOR_FIRST, CURSOR_NEXT, CURSOR_PREV:
 *             與游標目前位置的文章同標題 的 第一篇/下一篇/前一篇 文章。
 *           RELATE_FIRST, RELATE_NEXT, RELATE_PREV:
 *             與目前正閱讀的文章同標題 的 第一篇/下一篇/前一篇 文章。
 *           NEWPOST_NEXT, NEWPOST_PREV:
 *             下一個/前一個 thread 的第一篇。
 *           AUTHOR_NEXT, AUTHOR_PREV:
 *             XXX 這功能目前好像沒用到?
 *
 * @return 新的游標位置
 */
static int
thread(const keeploc_t * locmem, int stypen)
{
    fileheader_t fh;
    int     pos = locmem->crs_ln, jump = THREAD_SEARCH_RANGE, new_ln;
    int     fd = -1, amatch = -1;
    int     step = (stypen & RS_FORWARD) ? 1 : -1;
    const char *key;

    if(locmem->crs_ln==0)
	return locmem->crs_ln;

    STATINC(STAT_THREAD);
    if (stypen & RS_AUTHOR)
	key = headers[pos - locmem->top_ln].owner;
    else if (stypen & RS_CURRENT)
     	key = subject(currtitle);
    else
	key = subject(headers[pos - locmem->top_ln].title );

    for( new_ln = pos + step ;
	 new_ln > 0 && new_ln <= last_line && --jump > 0;
	 new_ln += step ) {

	if (get_records_keep(currdirect, &fh, sizeof(fileheader_t), new_ln, 1, &fd) <= 0)
	{
	    new_ln = pos;
	    break;
	}

        if( stypen & RS_TITLE ){
            if( stypen & RS_FIRST ){
		if( !strncmp(fh.title, key, PROPER_TITLE_LEN) )
		    break;
		else if( !strncmp(&fh.title[4], key, PROPER_TITLE_LEN) ) {
		    amatch = new_ln;
		    jump = THREAD_SEARCH_RANGE;
		    /* 當搜尋同主題第一篇, 連續找不到多少篇才停 */
		}
	    }
            else if( !strncmp(subject(fh.title), key, PROPER_TITLE_LEN) )
		break;
	}
        else if( stypen & RS_NEWPOST ){
            if( strncmp(fh.title, "Re:", 3) )
		break;
	}
        else{  // RS_AUTHOR
            if( strcmp(subject(fh.owner), key) == EQUSTR )
		break;
	}
    }

    if( fd != -1 )
	close(fd);

    if( jump <= 0 || new_ln <= 0 || new_ln > last_line )
	new_ln = (amatch == -1 ? pos : amatch); //didn't find

    return new_ln;
}

/*
 * 根據 stypen 選擇上/下一篇已讀過的文章
 *
 * @param locmem  用來存在某看板游標位置的 structure。
 * @param stypen  游標移動的方法
 *           CURSOR_NEXT, CURSOR_PREV:
 *             與游標目前位置的文章同標題 的 下一篇/前一篇 文章。
 *
 * @return 新的游標位置
 */
static int
search_read(const int bid, const keeploc_t * locmem, int stypen)
{
    fileheader_t fh;
    int     pos = locmem->crs_ln;
    int     rk;
    int     fd = -1;
    int     forward = (stypen & RS_FORWARD) ? 1 : 0;
    time4_t ftime, result;
    int     ret;
    int     step = forward ? 1 : -1;

    if( last_line <= 1 ) return pos;

    /* First load the timestamp of article where cursor points to */
reload_fh:
    rk = get_records_keep(currdirect, &fh, sizeof(fileheader_t), pos, 1, &fd);
    if( rk < 0 ) goto out;
    if( rk == 0 /* EOF */ ) {
        /* 如果是置底文章, 則要將 ftime 設定成最大 (代表比最後一篇還要新)  */
        ftime = 2147483647;
    } else {
#ifdef SAFE_ARTICLE_DELETE
        if (fh.filename[0] == '.' || fh.owner[0] == '-') {
            /* 游標所在文章已被刪除, 跳過, 往下找下一篇文章 */
            close(fd);
            pos += step;
            goto reload_fh;
        }
#endif
        ftime = atoi( &fh.filename[2] );
    }

    /* given the ftime to resolve the read article */
    ret = brc_search_read(bid, ftime, forward, &result );
    if( ret < 0 ) {
        /* No read record is found. Try to traverse from last article to
         * the first until a read article is found (via brc_unread()).
         * Note that this is O(n) since the brc_unread() returns at
         * either (ftime <= brc_expire_time) or (bnum <= 0).
         */
        int i;

        for( i = last_line; i >= 0; --i ) {
            rk = get_records_keep(currdirect, &fh, sizeof(fileheader_t), i, 1, &fd);
            if (rk < 0) goto out;
            if (rk == 0) continue;
            if( 0 == brc_unread( bid, fh.filename, 0 ) ) {
                pos = i;
                goto out;
            }
        }
    } else if( ret ) {
        /* yes we found the read article and stored in result. */
        int i;

        /* find out the position for the article result */
        for( i = pos; i >= 0 && i <= last_line; i += step ) {
            rk = get_records_keep(currdirect, &fh, sizeof(fileheader_t), i, 1, &fd);
            if (rk < 0) goto out;
            if (rk == 0) continue;
#ifdef SAFE_ARTICLE_DELETE
            if (fh.filename[0] == '.' || fh.owner[0] == '-') {
                /* 這篇文章已被刪除, 跳過, 試試下一篇 */
                continue;
            }
#endif
            if( atoi( &fh.filename[2] ) == result ) {
                pos = i;
                goto out;
            }
        }
    }

out:
    if( fd != -1 ) close(fd);
    return pos;
}

static int
select_by_aid(const keeploc_t * locmem, int *pnew_ln, int *pnewdirect_new_ln,
	char *pdefault_ch)
{
    char aidc[100];
    aidu_t aidu = 0;
    char dirfile[PATHLEN];
    char *sp;
    int n = -1;

    if(!getdata(b_lines, 0, "搜尋" AID_DISPLAYNAME ": #", aidc, 20, DOECHO))
    {
	move(b_lines, 0);
	clrtoeol();
	return FULLUPDATE;
    }

    if((currmode & MODE_SELECT) ||
	    (currstat == RMAIL))
    {
	move(21, 0);
	clrtobot();
	move(22, 0);
	prints("此狀態下無法使用搜尋" AID_DISPLAYNAME "功\能");
	pressanykey();
	return FULLUPDATE;
    }

    /* strip leading spaces and '#' */
    sp = aidc;
    while(*sp == ' ')
	sp ++;
    if(*sp == '#')
	sp ++;

    if((aidu = aidc2aidu(sp)) > 0)
    {
	/* search bottom */
	/* FIXME: 置底文但沒列在 .DIR.bottom 的在這段會搜不到，
	   在下一段 search board 時才會搜到本體。難解。 */
	{
	    char buf[FNLEN];

	    snprintf(buf, FNLEN, "%s.bottom", FN_DIR);
	    setbfile(dirfile, currboard, buf);
	    if((n = search_aidu(dirfile, aidu)) >= 0)
	    {
		n += getbtotal(currbid);
		/* 不可用 bottom_line，因為如果是在 digest mode，
		   bottom_line 會是文摘的數目，而不是真正的文章數 */
		if(currmode & MODE_DIGEST)
		{
		    *pnewdirect_new_ln = n;

		    *pnew_ln = locmem->crs_ln;
		    /* dirty hack for crs_ln = 1, then HOME pressed */

		    *pdefault_ch = KEY_TAB;
		    return DONOTHING;
		}
	    }
	}
	if(n < 0)
	    /* search board */
	{
	    setbfile(dirfile, currboard, FN_DIR);
	    n = search_aidu(dirfile, aidu);
	    if(n >= 0 && (currmode & MODE_DIGEST))
		/* switch to normal read mode */
	    {
		*pnewdirect_new_ln = n;

		*pnew_ln = locmem->crs_ln;
		/* dirty hack for crs_ln = 1, then HOME pressed */

		*pdefault_ch = KEY_TAB;
		return DONOTHING;
	    }
	}
	if(n < 0)
	    /* search digest */
	{
	    setbfile(dirfile, currboard, fn_mandex);
	    n = search_aidu(dirfile, aidu);
	    if(n >= 0 && !(currmode & MODE_DIGEST))
		/* switch to digest mode */
	    {
		*pnewdirect_new_ln = n;

		*pnew_ln = locmem->crs_ln;
		/* dirty hack for crs_ln = 1, then HOME pressed */

		*pdefault_ch = KEY_TAB;
		return DONOTHING;
	    }
	}
    }  /* if(aidu > 0) */

    if(n < 0)
    {
	move(21, 0);
	clrtobot();
	move(22, 0);
	if(aidu <= 0)
	    prints("不合法的" AID_DISPLAYNAME "，請確定輸入是正確的");
	else
	    prints("找不到這個" AID_DISPLAYNAME "，可能是文章已消失，或是你找錯看板了");
	pressanykey();
	return FULLUPDATE;
    }  /* if(n < 0) */

    // else
    *pnew_ln = n + 1;
    move(b_lines, 0);
    clrtoeol();
    return DONOTHING;
}

void
forward_file(const fileheader_t * fhdr, const char *direct)
{
    int             i;
    char            buf[PATHLEN];
    char           *p;

    strlcpy(buf, direct, sizeof(buf));
    if ((p = strrchr(buf, '/')))
	*p = '\0';
    switch (i = doforward(buf, fhdr, 'F')) {
    case 0:
	vmsg(msg_fwd_ok);
	break;
    case -1:
	vmsg(msg_fwd_err1);
	break;
    case -2:
#ifndef DEBUG_FWDADDRERR
	vmsg(msg_fwd_err2);
#endif
	break;
    case -4:
	vmsg("信箱已滿");
	break;
    default:
	break;
    }
}

static int
trim_blank(char *buf) {
    trim(buf);
    return *buf == 0;
}

static int
ask_filter_predicate(fileheader_predicate_t *pred, int prev_modes, int sr_mode,
		     const fileheader_t *fh, int *success)
{
    memset(pred, 0, sizeof(*pred));
    char * const keyword = pred->keyword;
    pred->mode = sr_mode;
    *success = 0;

    if(sr_mode & RS_AUTHOR) {
	if(!getdata(b_lines, 0,
		    currmode & MODE_SELECT ? "增加條件 作者: ":"搜尋作者: ",
		    keyword, IDLEN+1, DOECHO) || trim_blank(keyword))
	    return READ_REDRAW;
    } else if(sr_mode & RS_KEYWORD) {
	if(!getdata(b_lines, 0,
		    currmode & MODE_SELECT ? "增加條件 標題: ":"搜尋標題: ",
		    keyword, TTLEN, DOECHO) || trim_blank(keyword))
	    return READ_REDRAW;

	LOG_IF(LOG_CONF_KEYWORD, log_filef("keyword_search_log", LOG_CREAT,
					   "%s:%s\n", currboard, keyword));
    } else if(sr_mode & RS_KEYWORD_EXCLUDE) {
	if (currmode & MODE_SELECT) {
	    // TTLEN width exceed default screen
	    // let's use TTLEN-4 here.
	    if (!getdata(b_lines, 0, "增加條件 排除標題: ",
			 keyword, TTLEN-4, DOECHO) ||
		trim_blank(keyword))
		return READ_REDRAW;
	} else {
	    vmsg("反向搜尋(!)只能在已進入搜尋時使用(例: 先用 / 搜尋)");
	    return READ_REDRAW;
	}
    } else if (sr_mode & RS_RECOMMEND) {
	if (currstat == RMAIL ||
	    !getdata(b_lines, 0, (currmode & MODE_SELECT) ?
		     "增加條件 推文數: ": "搜尋推文數高於多少"
#ifndef OLDRECOMMEND
		     " (<0則搜噓文數) "
#endif // OLDRECOMMEND
		     "的文章: ",
		     // 因為有負數所以暫時不能用 NUMECHO
		     keyword, 7, LCECHO) ||
	    (pred->recommend = atoi(keyword)) == 0)
	    return READ_REDRAW;
    } else if (sr_mode & RS_MONEY) {
	if (currstat == RMAIL ||
	    !getdata(b_lines, 0, (currmode & MODE_SELECT) ?
		      "增加條件 文章價格: ":"搜尋價格高於多少的文章: ",
		      keyword, 7, NUMECHO) ||
	    (pred->money = atoi(keyword)) <= 0)
	    return READ_REDRAW;
	strcat(keyword, "M");
    } else {
	// Ptt: only once for these modes.
	if (prev_modes & sr_mode &
	    (RS_TITLE | RS_NEWPOST | RS_MARK | RS_SOLVED))
	    return DONOTHING;

	if (sr_mode & RS_TITLE) {
	    strcpy(keyword, subject(fh->title));
	}
    }
    *success = 1;
    return 0;  // Return value does not matter.
}

static int
match_filter_predicate(const fileheader_t *fh, void *arg)
{
    fileheader_predicate_t *pred = (fileheader_predicate_t *) arg;

    if (pred->mode & RS_MONEY)
	return query_file_money(fh) >= pred->money;

    return match_fileheader_predicate(fh, pred);
}

static int
select_read(const keeploc_t * locmem, int sr_mode)
{
   char newdirect[PATHLEN];
   int first_select;
   char genbuf[PATHLEN], *p = strstr(currdirect, "SR.");
   static int _mode = 0;

   if(locmem->crs_ln == 0)
       return locmem->crs_ln;
   fileheader_t *fh = &headers[locmem->crs_ln - locmem->top_ln];

   first_select = p==NULL;
   if (first_select)
       _mode = 0;

   STATINC(STAT_SELECTREAD);

   fileheader_predicate_t predicate;
   int success;
   int ui_ret = ask_filter_predicate(&predicate, _mode, sr_mode, fh, &success);
   if (!success)
       return ui_ret;
   _mode |= sr_mode;

   select_read_name(genbuf, sizeof(genbuf),
		    first_select ? NULL : p, &predicate);

   // pre-calculate board prefix
   if (currstat == RMAIL)
       sethomefile(newdirect, cuser.userid, "x");
   else
       setbfile(newdirect, currboard, "x");

   // XXX currently currdirect is 64 bytes while newdirect is 256 bytes.
   // however if we enlarge currdirect, there may be lots of SR generated.
   // as a result, let's make restriction here.
   assert( sizeof(newdirect) >= sizeof(currdirect) );
   if( strlen(genbuf) + strlen(newdirect) >= sizeof(currdirect) )
   {
       vmsg("抱歉，已達搜尋條件上限。");
       return  READ_REDRAW; // avoid overflow
   }

   if (currstat == RMAIL)
       sethomefile(newdirect, cuser.userid, genbuf);
   else
       setbfile(newdirect, currboard, genbuf);

   /* mark and recommend shouldn't incremental select */
   int force_full_build = sr_mode & (RS_MARK | RS_RECOMMEND | RS_SOLVED);

   int bid = (currstat != RMAIL && currboard[0] && currbid > 0) ? currbid : 0;
   time4_t resume_from;
   int count;
   if (select_read_should_build(newdirect, bid, &resume_from, &count) &&
       (count = select_read_build(currdirect, newdirect, !first_select,
				  force_full_build ? 0 : resume_from, count,
				  match_filter_predicate, &predicate)) < 0) {
      return READ_REDRAW;
   }

   if (count) {
       strlcpy(currdirect, newdirect, sizeof(currdirect));
       currmode |= MODE_SELECT;
       currsrmode |= sr_mode;
       return NEWDIRECT;
   }
   return READ_REDRAW;
}

static int newdirect_new_ln = -1;

static int
i_read_key(const onekey_t * rcmdlist, keeploc_t * locmem,
           int bid, int bottom_line, int pending_draws)
{
    int     mode = DONOTHING, num, new_top=10;
    int     ch, new_ln = locmem->crs_ln, lastmode = DONOTHING;
    static  char default_ch = 0;

    do {
	if( (mode = cursor_pos(locmem, new_ln, new_top, default_ch ? 0 : 1))
	    != DONOTHING )
	    return mode;

	if( !default_ch ) {
	    // Waiting for a key.

	    // We should have drawn the listing.
	    // If we had not, do it now.
	    if (pending_draws)
		return FULLUPDATE;

	    ch = vkey();
	} else {
	    if(new_ln != locmem->crs_ln) {// move fault
		default_ch=0;
		return FULLUPDATE;
	    }
	    ch = default_ch;
	}

	new_top = 10; // default 10
	switch (ch) {
	case Ctrl('Z'):
	    mode = FULLUPDATE;
	    if (ZA_Select())
		mode = DOQUIT;
	    break;
        case '0':    case '1':    case '2':    case '3':    case '4':
	case '5':    case '6':    case '7':    case '8':    case '9':
	    if( (num = search_num(ch, last_line)) != -1 )
		new_ln = num + 1;
	    break;
    	case 'q':
    	case 'e':
    	case KEY_LEFT:
	    if(currmode & MODE_SELECT && locmem->crs_ln>0){
		char genbuf[PATHLEN];
		fileheader_t *fhdr = &headers[locmem->crs_ln - locmem->top_ln];
		board_select();
		setbdir(genbuf, currboard);
		locmem = getkeep(genbuf, 0, 1);
		locmem->crs_ln = fhdr->multi.refer.ref;
		num = locmem->crs_ln - p_lines + 1;
		locmem->top_ln = num < 1 ? 1 : num;
		mode =  NEWDIRECT;
	    }
	    else
		mode =
		    (currmode & MODE_DIGEST) ? board_digest() : DOQUIT;
	    break;

	case '#':
	    mode = select_by_aid(locmem, &new_ln, &newdirect_new_ln, &default_ch);
	    break;

        case Ctrl('L'):
	    redrawwin();
	    refresh();
	    break;

        case Ctrl('H'):
	    mode = select_read(locmem, RS_NEWPOST);
	    break;

	case 'Z':
	    mode = select_read(locmem, RS_RECOMMEND);
	    break;

        case 'a':
	    mode = select_read(locmem, RS_AUTHOR);
	    break;

        case 'A':
	    mode = select_read(locmem, RS_MONEY);
	    break;

        case 'G':
	    // special types
	    switch(vans( currmode & MODE_SELECT ?
			"增加條件 標記(m/s)(未輸入則取消): ":
			"搜尋標記(m/s)(未輸入則取消): "))
	    {
		case 's':
		    mode = select_read(locmem, RS_SOLVED);
		    break;

		case 'm':
		    mode = select_read(locmem, RS_MARK);
		    break;

		default:
                    mode = READ_REDRAW;
                    break;
	    }
	    break;

        case '/':
        case '?':
	    mode = select_read(locmem, RS_KEYWORD);
	    break;

        case 'S':
	    mode = select_read(locmem, RS_TITLE);
	    break;

        case '!':
	    mode = select_read(locmem, RS_KEYWORD_EXCLUDE);
	    break;

        case '=':
	    new_ln = thread(locmem, RELATE_FIRST);
	    break;

        case '\\':
	    new_ln = thread(locmem, CURSOR_FIRST);
	    break;

        case ']':
	    new_ln = thread(locmem, RELATE_NEXT);
	    break;

        case '+':
	    new_ln = thread(locmem, CURSOR_NEXT);
	    break;

        case '[':
	    new_ln = thread(locmem, RELATE_PREV);
	    break;

     	case '-':
	    new_ln = thread(locmem, CURSOR_PREV);
	    break;

    	case '<':
    	case ',':
	    new_ln = thread(locmem, NEWPOST_PREV);
	    break;

    	case '.':
    	case '>':
	    new_ln = thread(locmem, NEWPOST_NEXT);
	    break;

    	case 'p':
    	case 'k':
	case KEY_UP:
	    if (locmem->crs_ln <= 1) {
		new_ln = last_line;
		new_top = p_lines-1;
	    } else {
		new_ln = locmem->crs_ln - 1;
		new_top = p_lines - 2;
	    }
	    break;

	case 'n':
	case 'j':
	case KEY_DOWN:
	    new_ln = locmem->crs_ln + 1;
	    new_top = 1;
	    break;

	case ' ':
	case KEY_PGDN:
	case 'N':
	case Ctrl('F'):
	    new_ln = locmem->top_ln + p_lines;
	    new_top = 0;
	    break;

	case KEY_PGUP:
	case Ctrl('B'):
	case 'P':
	    new_ln = locmem->top_ln - p_lines;
	    new_top = 0;
	    break;

	    /* add home(top entry) support? */
	case KEY_HOME:
	    new_ln = 0;
	    new_top = 0;
	    break;

	case KEY_END:
	case '$':
	    new_ln = last_line;
	    new_top = p_lines-1;
	    break;

	case Ctrl('Q'):
            // my_query (talk->query) needs PERM_LOGINOK, so no reason to allow
            // here (seen bots doing this).
	    if(HasBasicUserPerm(PERM_LOGINOK) && locmem->crs_ln>0)
		mode = my_query(headers[locmem->crs_ln - locmem->top_ln].owner);
	    break;

	case Ctrl('S'):
	    if (HasUserPerm(PERM_ACCOUNTS|PERM_SYSOP) && locmem->crs_ln>0) {
		int             id;
		userec_t        muser;

		vs_hdr("使用者設定");
		move(1, 0);
		if ((id = getuser(headers[locmem->crs_ln - locmem->top_ln].owner, &muser))) {
		    user_display(&muser, 1);
		    if( HasUserPerm(PERM_ACCOUNTS) )
			uinfo_query(muser.userid, 1, id);
		    else
			pressanykey();
		}
		mode = FULLUPDATE;
	    }
	    break;

	    /* rocker.011018: 採用新的tag模式 */
	case 't':
	    if(locmem->crs_ln == 0)
		break;
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
            if (ToggleTagItem(&headers[locmem->crs_ln - locmem->top_ln]))
	    {
		locmem->crs_ln ++;
		mode = PARTUPDATE;
	    }
	    break;

    case Ctrl('C'):
	if (TagNum) {
	    TagNum = 0;
	    mode = FULLUPDATE;
	}
        break;

    case Ctrl('T'):
	/* XXX duplicated code, copy from case 't' */
	if ((currstat & RMAIL && TagBoard != 0) ||
		(!(currstat & RMAIL) && TagBoard != bid)) {
	    if (currstat & RMAIL)
		TagBoard = 0;
	    else
		TagBoard = bid;
	    TagNum = 0;
	}
	mode = TagThread(currdirect);
        break;

    case Ctrl('D'):
        if (currmode & MODE_SELECT) {
            vmsg("請先離開搜尋模式(T標記會保留)再刪除檔案。");
            mode = FULLUPDATE;
        } else {
            mode = TagPruner(bid);
        }
        break;

    case '{':
        new_ln = search_read(bid, locmem, READ_PREV);
        break;

    case '}':
        new_ln = search_read(bid, locmem, READ_NEXT);
        break;

    case KEY_ENTER:
    case 'l':
    case KEY_RIGHT:
	ch = 'r';
    default:
	if( ch == 'h' && currmode & (MODE_DIGEST) )
	    break;
	if (ch > 0 && ch <= onekey_size) {
	    int (*func)() = rcmdlist[ch - 1].func;
	    if(rcmdlist[ch - 1].needitem && locmem->crs_ln == 0)
		break;
	    if (func != NULL){
		num  = locmem->crs_ln - bottom_line;

		if(!rcmdlist[ch - 1].needitem)
		    mode = (*func)();
		else if( num > 0 ){
		    char    direct[60];
                    sprintf(direct,"%s.bottom", currdirect);
		    mode= (*func)(num, &headers[locmem->crs_ln-locmem->top_ln],
				  direct, locmem->crs_ln - locmem->top_ln);
		}
		else
                    mode = (*func)(locmem->crs_ln,
				   &headers[locmem->crs_ln - locmem->top_ln],
				   currdirect, locmem->crs_ln - locmem->top_ln);
		if(mode == READ_SKIP)
                    mode = lastmode;

		if (mode == RET_SELECTAID)
		{
		    ch = '#';
		    lastmode = FULLUPDATE;
		    select_by_aid(locmem, &new_ln, &newdirect_new_ln, &default_ch);
		    cursor_pos(locmem, new_ln, new_top, default_ch ? 0 : 1);
		    return FULLUPDATE;
		}

		// 以下這幾種 mode 要再處理游標
                if(mode == READ_PREV || mode == READ_NEXT ||
                   mode == RELATE_PREV || mode == RELATE_FIRST ||
                   mode == AUTHOR_NEXT || mode ==  AUTHOR_PREV ||
                   mode == RELATE_NEXT){
		    lastmode = mode;

		    switch(mode){
                    case READ_PREV:
                        new_ln =  locmem->crs_ln - 1;
                        break;
                    case READ_NEXT:
                        new_ln =  locmem->crs_ln + 1;
                        break;
	            case RELATE_PREV:
                        new_ln = thread(locmem, RELATE_PREV);
			break;
                    case RELATE_NEXT:
                        new_ln = thread(locmem, RELATE_NEXT);
			/* XXX: 讀到最後一篇要跳出來 */
			if( new_ln == locmem->crs_ln ){
			    default_ch = 0;
			    return FULLUPDATE;
			}
		        break;
                    case RELATE_FIRST:
                        new_ln = thread(locmem, RELATE_FIRST);
		        break;
                    case AUTHOR_PREV:
                        new_ln = thread(locmem, AUTHOR_PREV);
		        break;
                    case AUTHOR_NEXT:
                        new_ln = thread(locmem, AUTHOR_NEXT);
		        break;
		    }
		    mode = DONOTHING; default_ch = 'r';
                }
		else {
		    default_ch = 0;
		    lastmode = DONOTHING;
		}
	    } //end if (func != NULL)
	} // ch > 0 && ch <= onekey_size
    	break;
	} // end switch

	// ZA support
	if (ZA_Waiting())
	    mode = DOQUIT;

    } while (mode == DONOTHING);
    return mode;
}

// recbase:	顯示位置的開頭
// headers_size:要顯示幾行
// last_line:	全板 .DIR + 置底 的有效數目
// bottom_line:	全板 .DIR (無置底) 的有效數目

// XXX never return -1!

static int
get_records_and_bottom(const char *direct,  fileheader_t* headers,
                     int recbase, int headers_size, int last_line, int bottom_line)
{
    // n: 置底除外的可顯示數目
    int     n = bottom_line - recbase + 1, rv = 0;

    if( last_line < 1)	// 完全沒東西
	return 0;

    BEGINSTAT(STAT_BOARDREC);
    // 不顯示置底的情形
    if( n >= headers_size || (currmode & (MODE_SELECT | MODE_DIGEST)) )
    {
	rv = get_records(direct, headers, sizeof(fileheader_t),
		recbase, headers_size);
	ENDSTAT(STAT_BOARDREC);
	return rv > 0 ? rv : 0;
    }

    //////// 顯示本文+置底: ////////

    // 讀取 .DIR 本文
    if (n > 0)
    {
	n = get_records(direct, headers, sizeof(fileheader_t), recbase, n);
	if (n < 0) n = 0;
	rv += n; // rv 為有效本文數

	recbase = 1;
    } else {
	// n <= 0
	recbase = 1 + (-n);
    }

    // 讀取置底 (注意 recbase 可能超過 bottom_line, 也就是以置底第 n 個開始)
    n = last_line - bottom_line +1 - (recbase-1);
    if (rv + n > headers_size)
	n = headers_size - rv;

    if (n > 0) {
	char    directbottom[PATHLEN];
	snprintf(directbottom, sizeof(directbottom), "%s.bottom", direct);
	n = get_records(directbottom, headers+rv, sizeof(fileheader_t), recbase, n);
	if (n < 0) n = 0;
	rv += n;
    }

    ENDSTAT(STAT_BOARDREC);
    return rv;
}

void
i_read(int cmdmode, const char *direct, void (*dotitle) (),
       void (*doentry) (), const onekey_t * rcmdlist, int bidcache)
{
    keeploc_t      *locmem = NULL;
    int             recbase = 0, mode;
    int             entries = 0;
    char            currdirect0[PATHLEN];
    int             last_line0 = last_line;
    int             bottom_line = 0;
    fileheader_t   *headers0 = headers;
    int             headers_size0 = headers_size;
    time4_t	    enter_time = now;
    const size_t FHSZ = sizeof(fileheader_t);
    int             needs_fullupdate = 0;

    strlcpy(currdirect0, currdirect, sizeof(currdirect0));
    /* Ptt: 這邊 headers 可以針對看板的最後 60 篇做 cache */
    headers_size = p_lines;
    headers = (fileheader_t *) calloc(headers_size, FHSZ);
    assert(headers != NULL);
    strlcpy(currdirect, direct, sizeof(currdirect));
    mode = NEWDIRECT;

    do {
	/* 檢查權限是否已改 */
	if (currbid > 0 && getbcache(currbid)->perm_reload > enter_time)
	{
	    boardheader_t *bp = getbcache(currbid);
	    if(!HasBoardPerm(bp))
		break;
	    enter_time = bp->perm_reload;
	}

	/* 依據 mode 顯示 fileheader */
	setutmpmode(cmdmode);
	switch (mode) {
	case DONOTHING:
	    break;

	case NEWDIRECT:	/* 第一次載入此目錄 */
	case DIRCHANGED:
	    if (bidcache > 0 && !(currmode & (MODE_SELECT | MODE_DIGEST))){
		if( (last_line = getbtotal(currbid)) == 0 ){
		    setbtotal(currbid);
                    setbottomtotal(currbid);
		    last_line = getbtotal(currbid);
		}
                bottom_line = last_line;
                last_line += getbottomtotal(currbid);
	    }
	    else
		bottom_line = last_line = get_num_records(currdirect, FHSZ);

	    if (mode == NEWDIRECT) {
		int num;
		num = last_line - p_lines + 1;
		locmem = getkeep(currdirect, num < 1 ? 1 : num,
			bottom_line ? bottom_line : last_line);
		if(newdirect_new_ln >= 0)
		{
		  locmem->crs_ln = newdirect_new_ln + 1;
		  newdirect_new_ln = -1;
		}
	    }
	    recbase = -1;
	    /* no break */

	default: // for any unknown keys
	case FULLUPDATE:
	    needs_fullupdate = 0;
	    (*dotitle) ();
	    /* no break */

	case PARTUPDATE:
	    if (headers_size != p_lines) {
		headers_size = p_lines;
		headers = (fileheader_t *) realloc(headers, headers_size*FHSZ);
		assert(headers);
	    }

	    /* In general, records won't be reloaded in PARTUPDATE state.
	     * But since a board is often changed and cached, it is always
	     * reloaded here. */
	    if (bidcache > 0 && !(currmode & (MODE_SELECT | MODE_DIGEST))) {
		int rec_num;
		bottom_line = getbtotal(currbid);
		rec_num = bottom_line + getbottomtotal(currbid);
		if (last_line != rec_num) {
		    last_line = rec_num;
		    recbase = -1;
		}
	    }

	    if (recbase != locmem->top_ln) { //headers reload
		recbase = locmem->top_ln;
		if (recbase > last_line) {
		    recbase = last_line - headers_size + 1;
		    if (recbase < 1)
			recbase = 1;
		    locmem->top_ln = recbase;
		}
		/* XXX if entries return -1 or black-hole */
                entries = get_records_and_bottom(currdirect,
                           headers, recbase, headers_size, last_line, bottom_line);
	    }
	    if (locmem->crs_ln > last_line)
		locmem->crs_ln = last_line;
	    move(3, 0);
	    clrtobot();
	    /* no break */
	case PART_REDRAW:
	    move(3, 0);
            if( last_line == 0 )
                  outs("    沒有文章...");
            else {
		int i;
		for( i = 0; i < entries ; i++ )
		    (*doentry) (locmem->top_ln + i, &headers[i]);
	    }
	    /* no break */
	case READ_REDRAW:
	    if (currstat == RMAIL)
		vs_footer(" 鴻雁往返 ",
		    " (R/y)回信 (x)站內轉寄 (d/D)刪信 (^P)寄發新信 \t(←/q)離開");
	    else
		vs_footer(" 文章選讀 ",
		    " (y)回應(X)推文(^X)轉錄 (=[]<>)相關主題(/?a)找標題/作者 (b)進板畫面");
	    break;

	case TITLE_REDRAW:
	    (*dotitle) ();
            break;

        case HEADERS_RELOAD:
	    if (recbase != locmem->top_ln) {
		recbase = locmem->top_ln;
		if (recbase > last_line) {
		    recbase = last_line - p_lines + 1;
		    if (recbase < 1)
			recbase = 1;
		    locmem->top_ln = recbase;
		}
		if(headers_size != p_lines) {
		    headers_size = p_lines;
		    headers = (fileheader_t *) realloc(headers, headers_size*FHSZ);
		    assert(headers);
		}
		/* XXX if entries return -1 */
                entries =
		    get_records_and_bottom(currdirect, headers, recbase,
					   headers_size, last_line, bottom_line);
		needs_fullupdate = 1;
	    }
            break;
	} //end switch
	mode = i_read_key(rcmdlist, locmem, currbid, bottom_line, needs_fullupdate);
    } while (mode != DOQUIT && !ZA_Waiting());

    free(headers);
    last_line = last_line0;
    headers = headers0;
    headers_size = headers_size0;
    strlcpy(currdirect, currdirect0, sizeof(currdirect));
    return;
}
