#include "bbs.h"
#include "psb.h"

static int headers_size = 0;
static fileheader_t *headers = NULL;
static int      last_line; // PTT: last_line 游標可指的最後一個

#include <sys/mman.h>

/* tag extension */

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
    apply_fileheader(direct, _iter_tag_match_title, currtitle);
    return FULLUPDATE;
}

static int
_iter_delete_tagged(void *ptr, void *opt) {
    fileheader_t *fh = (fileheader_t*) ptr;
    const char *direct = (const char*)opt;

    /* FILE_BOTTOM aliases FILE_MULTI in mail. */
    if ((fh->filemode & FILE_MARKED) ||
        (fh->filemode & FILE_DIGEST) ||
        ((fh->filemode & FILE_BOTTOM) && currstat != RMAIL))
        return 0;
    if (!FindTaggedItem(fh))
        return 0;
    // only called by home or board, no need for man.
    // so backup_direct can be same as direct.
    delete_file_content(direct, fh, direct, NULL, 0);
    return IsEmptyTagList();
}


static int
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
    apply_fileheader(direct, _iter_delete_tagged, direct);

    // now, delete the header
#ifdef SAFE_ARTICLE_DELETE
    if(bp && !(currmode & MODE_DIGEST) &&
       bp->nuser >= SAFE_ARTICLE_DELETE_NUSER)
        safe_delete_range(currdirect, 0, 0);
    else
#endif
        delete_range(currdirect, 0, 0);

    ClearTagList();
    if (bid) {
        setbtotal(bid);
        if (bp) {
            syncnow();
            bp->SRexpire = (bp->SRexpire >= now) ? bp->SRexpire + 1 : now;
        }
        search_svc_invalidate(currdirect, bid);
    } else if(currstat == RMAIL)
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

typedef struct {
    int total;
    int bottom_line;
    bool reverse_order;
    int32_t bottom_recs[MAX_BOTTOM_POSTS];
    int bottom_count;
    int32_t *remap;
    int remap_base;
    int remap_count;
} read_view_t;

static inline int
read_view_v2p(const read_view_t *view, int vidx) {
    if (view->reverse_order)
        return view->total - vidx;
    return vidx + 1;
}

static inline int
read_view_p2v(const read_view_t *view, int p_recno) {
    if (view->reverse_order)
        return view->total - p_recno;
    return p_recno - 1;
}

typedef struct {
    const cmd_t    *rcmdlist;
    keeploc_t      *locmem;
    keeploc_t       sr_locmem;
    int             cmdmode;
    int             bid;
    int             bidcache;
    int             bottom_line;
    int             entries;
    bool            is_newdirect;
    time4_t         enter_time;
    void          (*dotitle)(void);
    void          (*doentry)(int, fileheader_t *);
    read_view_t     view;
    fileheader_predicate_t sr_preds[MAX_SEARCH_PREDICATES];
    int             sr_pred_count;
    int             sr_mode_mask;
    bool            sr_just_selected;
} read_ctx_t;

static void
read_view_ensure_window(read_ctx_t *cx, int start_disp_ln, int count)
{
    if (!(currmode & MODE_SELECT) || cx->sr_pred_count <= 0 || cx->view.total <= 0)
        return;
    if (!cx->view.remap) {
        cx->view.remap = (int32_t *)calloc(SEARCH_SVC_WINDOW_SIZE, sizeof(int32_t));
        assert(cx->view.remap != NULL);
        cx->view.remap_base = 0;
        cx->view.remap_count = 0;
    }
    int idx_start = start_disp_ln - 1;
    if (idx_start < 0)
        idx_start = 0;
    int idx_end = idx_start + (count > 0 ? count : 1);
    if (idx_end > cx->view.total)
        idx_end = cx->view.total;

    if (cx->view.remap_count > 0 &&
        idx_start >= cx->view.remap_base &&
        idx_end <= cx->view.remap_base + cx->view.remap_count) {
        return;
    }

    int new_base = idx_start - SEARCH_SVC_WINDOW_SIZE / 2;
    if (new_base + SEARCH_SVC_WINDOW_SIZE > cx->view.total)
        new_base = cx->view.total - SEARCH_SVC_WINDOW_SIZE;
    if (new_base < 0)
        new_base = 0;

    int bid = (currstat != RMAIL && currboard[0] && currbid > 0) ? currbid : 0;
    int total = cx->view.total;
    int loaded = search_predicates_window(currdirect, bid,
                                          cx->sr_preds, cx->sr_pred_count,
                                          new_base, SEARCH_SVC_WINDOW_SIZE,
                                          cx->view.remap, &total);
    if (loaded >= 0) {
        cx->view.total = last_line = cx->bottom_line = total;
        cx->view.remap_base = new_base;
        cx->view.remap_count = loaded;
    }
}

/* Returns the 1-based record number in currdirect for display line disp_ln,
 * or 0 if it cannot be resolved (empty list, or the select remap is not
 * available). 0 is rejected by get_records_keep() and the substitute/delete
 * helpers, so a miss never touches an unrelated record such as #1. */
static int
read_view_real_recno(read_ctx_t *cx, int disp_ln)
{
    if (disp_ln <= 0)
        return 0;
    if (currmode & MODE_SELECT) {
        read_view_ensure_window(cx, disp_ln, 1);
        int idx = disp_ln - 1;
        if (cx->view.remap &&
            idx >= cx->view.remap_base &&
            idx < cx->view.remap_base + cx->view.remap_count) {
            return cx->view.remap[idx - cx->view.remap_base];
        }
        return 0;
    }
    if (disp_ln > cx->view.bottom_line) {
        int b_idx = disp_ln - cx->view.bottom_line - 1;
        if (b_idx >= 0 && b_idx < cx->view.bottom_count)
            return cx->view.bottom_recs[b_idx];
    }
    return disp_ln;
}

static int
TagThreadView(read_ctx_t *cx, const char *direct)
{
    if (!(currmode & MODE_SELECT)) {
        return TagThread(direct);
    }
    int fd = -1;
    fileheader_t fh;
    for (int ln = 1; ln <= last_line; ln++) {
        int real_recno = read_view_real_recno(cx, ln);
        if (get_fileheaders_keep(direct, &fh, real_recno, 1, &fd) > 0) {
            if (_iter_tag_match_title(&fh, currtitle) < 0)
                break;
        }
    }
    if (fd != -1)
        close(fd);
    return FULLUPDATE;
}

/**
 * 根據 stypen 選擇上/下一篇文章
 *
 * @param locmem  用來存在某看板游標位置的 structure。
 * @param stypen  游標移動的方法
 *           CURSOR_FIRST, CURSOR_NEXT, CURSOR_PREV:
 *             與游標目前位置的文章同標題 的 第一篇/下一篇/前一篇 文章。
 *           RELATE_FIRST, RELATE_NEXT, RELATE_PREV:
 *             與目前正閱\讀的文章同標題 的 第一篇/下一篇/前一篇 文章。
 *           NEWPOST_NEXT, NEWPOST_PREV:
 *             下一個/前一個 thread 的第一篇。
 *           AUTHOR_NEXT, AUTHOR_PREV:
 *             XXX 這功\能目前好像沒用到?
 *
 * @return 新的游標位置
 */
static int
thread(read_ctx_t *cx, int stypen)
{
    const keeploc_t *locmem = cx->locmem;
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

	/* Pinned lines duplicate .DIR records; treat them as EOF like before. */
	if (!(currmode & (MODE_SELECT | MODE_DIGEST)) &&
	    cx->view.bottom_count > 0 && new_ln > cx->view.bottom_line) {
	    new_ln = pos;
	    break;
	}
	int real_recno = read_view_real_recno(cx, new_ln);
	if (get_fileheaders_keep(currdirect, &fh, real_recno, 1, &fd) <= 0)
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
/* Bottom (pinned) lines are treated as EOF, i.e. newer than any post. */
static int
search_read_fh(read_ctx_t *cx, fileheader_t *fh, int ln, int *pfd)
{
    if (!(currmode & (MODE_SELECT | MODE_DIGEST)) &&
        cx->view.bottom_count > 0 && ln > cx->view.bottom_line)
        return 0;
    return get_fileheaders_keep(currdirect, fh, read_view_real_recno(cx, ln), 1, pfd);
}

static int
search_read(read_ctx_t *cx, const int bid, int stypen)
{
    const keeploc_t *locmem = cx->locmem;
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
reload_fh: GCC_UNUSED;
    rk = search_read_fh(cx, &fh, pos, &fd);
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
        ftime = get_fhdr_stamp_ts(fh.filename);
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

        for( i = last_line; i >= 1; --i ) {
            rk = search_read_fh(cx, &fh, i, &fd);
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
        for( i = pos; i >= 1 && i <= last_line; i += step ) {
            rk = search_read_fh(cx, &fh, i, &fd);
            if (rk < 0) goto out;
            if (rk == 0) continue;
#ifdef SAFE_ARTICLE_DELETE
            if (fh.filename[0] == '.' || fh.owner[0] == '-') {
                /* 這篇文章已被刪除, 跳過, 試試下一篇 */
                continue;
            }
#endif
            if( get_fhdr_stamp_ts(fh.filename) == result ) {
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
select_by_aid(read_ctx_t *cx, int *pnew_ln, int *pnewdirect_new_ln,
	char *pdefault_ch)
{
    const keeploc_t *locmem = cx->locmem;
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

    /* skip leading spaces and '#' */
    sp = aidc;
    while(*sp == ' ')
	sp ++;
    if(*sp == '#')
	sp ++;

    aidu = aidc2aidu(sp);

    if(aidu > 0)
    {
	/* search bottom in boardheader_t.bottom[] */
	if (currbid > 0) {
	    boardheader_t *bp = getbcache(currbid);
	    int32_t brecs[MAX_BOTTOM_POSTS];
	    int bcnt = bp ? resolve_board_bottoms(currbid, brecs) : 0;
	    if (bp) {
		int b_slot = 0;
		for (int i = 0; i < MAX_BOTTOM_POSTS; i++) {
		    aidu_t baidu = aidu_raw(bp->bottom[i]);
		    if (baidu == 0)
			continue;
		    aidu_t qaidu = aidu_raw(aidu);
		    if (baidu == qaidu || ((qaidu & 0xfffU) == 0 && (baidu & ~0xfffULL) == qaidu)) {
			int btotal = getbtotal(currbid);
			if (btotal <= 0) {
			    setbtotal(currbid);
			    btotal = getbtotal(currbid);
			}
			n = btotal + b_slot;
			if (currmode & MODE_DIGEST) {
			    *pnewdirect_new_ln = n;
			    *pnew_ln = locmem->crs_ln;
			    *pdefault_ch = KEY_TAB;
			    return DONOTHING;
			}
			memcpy(cx->view.bottom_recs, brecs, sizeof(brecs));
			cx->view.bottom_count = bcnt;
			cx->bottom_line = cx->view.bottom_line = btotal;
			cx->view.total = last_line = btotal + bcnt;
			break;
		    }
		    b_slot++;
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

    STRLCPY(buf, direct);
    if ((p = strrchr(buf, '/')))
	*p = '\0';
    switch (i = doforward(buf, fhdr, 'F')) {
    case 0:
	vmsg(MSG_FWD_OK);
	break;
    case -1:
	vmsg(MSG_FWD_ERR1);
	break;
    case -2:
#ifndef DEBUG_FWDADDRERR
	vmsg(MSG_FWD_ERR2);
#endif
	break;
    case -4:
	vmsg("對方信箱已滿");
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

	LOG_IF(LOG_CONF_KEYWORD, log_filef("keyword_search_log",
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
	    strlcpy(keyword, subject(fh->title), TTLEN);
	}
    }
    *success = 1;
    return 0;  // Return value does not matter.
}

static int
select_read(read_ctx_t *cx, int sr_mode)
{
    const keeploc_t *locmem = cx->locmem;
    if (!locmem || locmem->crs_ln == 0)
        return 0;
    fileheader_t *fh = &headers[locmem->crs_ln - locmem->top_ln];

    if (!(currmode & MODE_SELECT)) {
        cx->sr_pred_count = 0;
        cx->sr_mode_mask = 0;
    }

    if (cx->sr_pred_count >= MAX_SEARCH_PREDICATES) {
        vmsg("抱歉，已達搜尋條件上限。");
        return READ_REDRAW;
    }

    STATINC(STAT_SELECTREAD);

    fileheader_predicate_t predicate;
    int success = 0;
    int ui_ret = ask_filter_predicate(&predicate, cx->sr_mode_mask, sr_mode, fh, &success);
    if (!success)
        return ui_ret;

    if (!cx->view.remap) {
        cx->view.remap = (int32_t *)calloc(SEARCH_SVC_WINDOW_SIZE, sizeof(int32_t));
        assert(cx->view.remap != NULL);
    }

    cx->sr_preds[cx->sr_pred_count] = predicate;
    int bid = (currstat != RMAIL && currboard[0] && currbid > 0) ? currbid : 0;
    int total = 0;
    int loaded = search_predicates_window(currdirect, bid,
                                          cx->sr_preds, cx->sr_pred_count + 1,
                                          0, SEARCH_SVC_WINDOW_SIZE,
                                          cx->view.remap, &total);
    if (loaded < 0 || total <= 0) {
        return READ_REDRAW;
    }

    cx->sr_pred_count++;
    cx->sr_mode_mask |= sr_mode;
    cx->view.total = total;
    cx->view.bottom_line = total;
    cx->view.bottom_count = 0;
    cx->view.remap_base = 0;
    cx->view.remap_count = loaded;

    if (total > SEARCH_SVC_WINDOW_SIZE) {
        int tail_base = total - SEARCH_SVC_WINDOW_SIZE;
        int tail_loaded = search_predicates_window(currdirect, bid,
                                                   cx->sr_preds, cx->sr_pred_count,
                                                   tail_base, SEARCH_SVC_WINDOW_SIZE,
                                                   cx->view.remap, &total);
        if (tail_loaded > 0) {
            cx->view.remap_base = tail_base;
            cx->view.remap_count = tail_loaded;
        }
    }

    currmode |= MODE_SELECT;
    currsrmode |= sr_mode;
    cx->sr_just_selected = true;
    return NEWDIRECT;
}

static int newdirect_new_ln = -1;

static int
read_key_handle_mode_navigation(read_ctx_t *cx, int mode, int *new_ln_ptr, char *default_ch_ptr)
{
    keeploc_t *locmem = cx->locmem;
    switch (mode) {
    case READ_PREV:
        *new_ln_ptr = locmem->crs_ln - 1;
        break;
    case READ_NEXT:
        *new_ln_ptr = locmem->crs_ln + 1;
        break;
    case RELATE_PREV:
        *new_ln_ptr = thread(cx, RELATE_PREV);
        break;
    case RELATE_NEXT:
        *new_ln_ptr = thread(cx, RELATE_NEXT);
        if (*new_ln_ptr == locmem->crs_ln) {
            *default_ch_ptr = 0;
            return 1;
        }
        break;
    case RELATE_FIRST:
        *new_ln_ptr = thread(cx, RELATE_FIRST);
        break;
    case AUTHOR_PREV:
        *new_ln_ptr = thread(cx, AUTHOR_PREV);
        break;
    case AUTHOR_NEXT:
        *new_ln_ptr = thread(cx, AUTHOR_NEXT);
        break;
    }
    return 0;
}

static int
read_view_load_physical_range(read_ctx_t *cx, const char *direct,
                              fileheader_t *buf, int recbase, int count)
{
    const read_view_t *view = &cx->view;
    if (view->total < 1 || count <= 0)
        return 0;

    BEGINSTAT(STAT_BOARDREC);
    if (currmode & MODE_SELECT) {
        int fd = -1;
        int rv = 0;
        for (int attempt = 0; attempt < 2; attempt++) {
            read_view_ensure_window(cx, recbase, count);
            rv = 0;
            int stale = 0;
            for (int i = 0; i < count && (recbase + i) <= view->total; i++) {
                int real_recno = read_view_real_recno(cx, recbase + i);
                if (get_fileheader_keep(direct, &buf[rv], real_recno, &fd) <= 0 ||
                    !buf[rv].filename[0] || buf[rv].filename[0] == '.' ||
                    buf[rv].owner[0] == '-') {
                    stale = 1;
                    break;
                }
                for (int p = 0; p < cx->sr_pred_count; p++) {
                    /* Any mismatch (shifted remap after a physical delete,
                     * or an in-place edit) means the cached window is stale. */
                    if (!match_fileheader_predicate(&buf[rv], &cx->sr_preds[p])) {
                        stale = 1;
                        break;
                    }
                }
                if (stale)
                    break;
                rv++;
            }
            if (!stale || attempt > 0)
                break;
            /* Mismatch detected! Notify search.svc that its cached index remap is wrong */
            int bid = (currstat != RMAIL && currboard[0] && currbid > 0) ? currbid : 0;
            search_svc_invalidate(direct, bid);
            cx->view.remap_count = 0;
        }
        if (fd != -1)
            close(fd);
        ENDSTAT(STAT_BOARDREC);
        return rv;
    }

    int n = view->bottom_line - recbase + 1;
    int rv = 0;

    if (n >= count || (currmode & MODE_DIGEST)) {
        rv = get_fileheaders(direct, buf, recbase, count);
        if (rv < 0)
            rv = 0;
        ENDSTAT(STAT_BOARDREC);
        return rv;
    }

    if (n > 0) {
        n = get_fileheaders(direct, buf, recbase, n);
        if (n < 0)
            n = 0;
        rv += n;
        recbase = 1;
    } else {
        recbase = 1 + (-n);
    }

    int b_avail = view->bottom_count - (recbase - 1);
    if (rv + b_avail > count)
        b_avail = count - rv;

    if (b_avail > 0) {
        int fd = -1;
        for (int i = 0; i < b_avail; i++) {
            int b_idx = (recbase - 1) + i;
            if (b_idx >= 0 && b_idx < view->bottom_count) {
                int real_recno = view->bottom_recs[b_idx];
                if (get_fileheader_keep(direct, &buf[rv], real_recno, &fd) > 0) {
                    buf[rv].filemode |= FILE_BOTTOM;
                    rv++;
                }
            }
        }
        if (fd != -1)
            close(fd);
    }

    ENDSTAT(STAT_BOARDREC);
    return rv;
}

static int
read_view_load_window(read_ctx_t *cx, const char *direct,
                      fileheader_t *buf, int base, int count) {
    const read_view_t *view = &cx->view;
    if (!view->reverse_order) {
        return read_view_load_physical_range(cx, direct, buf, base + 1, count);
    }
    int actual_count = count;
    if (base + actual_count > view->total)
        actual_count = view->total - base;
    if (actual_count <= 0)
        return 0;
    int p_start = view->total - (base + actual_count) + 1;
    int loaded = read_view_load_physical_range(cx, direct, buf, p_start, actual_count);
    for (int i = 0; i < loaded / 2; i++) {
        fileheader_t tmp = buf[i];
        buf[i] = buf[loaded - 1 - i];
        buf[loaded - 1 - i] = tmp;
    }
    return loaded;
}

static void
read_move_cursor(read_ctx_t *cx, cmd_ctx_t *ctx, int new_ln, int from_top) {
    if (last_line <= 0)
        return;
    if (new_ln > last_line)
        new_ln = last_line;
    if (new_ln <= 0)
        new_ln = 1;
    int new_curr = read_view_p2v(&cx->view, new_ln);
    if (new_curr < ctx->base || new_curr >= ctx->base + ctx->rows) {
        int new_base = new_curr - from_top;
        if (new_base < 0)
            new_base = 0;
        ctx->base = new_base;
    }
    ctx->curr = new_curr;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    cx->locmem->top_ln = read_view_v2p(&cx->view, ctx->base);
}

static void
read_apply_mode(read_ctx_t *cx, cmd_ctx_t *ctx, int mode) {
    switch (mode) {
    case DOQUIT:
        ctx->quit = true;
        break;
    case NEWDIRECT:
        cx->is_newdirect = true;
        ctx->reload = true;
        break;
    case DIRCHANGED:
        ctx->reload = true;
        break;
    case FULLUPDATE:
        ctx->reload = true;
        ctx->redraw = true;
        break;
    case PARTUPDATE:
        if (cx->bidcache > 0 && !(currmode & (MODE_SELECT | MODE_DIGEST))) {
            if (last_line != getbtotal(currbid) + getbottomtotal(currbid))
                ctx->reload = true;
        }
        ctx->redraw = true;
        break;
    case PART_REDRAW:
        ctx->redraw = true;
        break;
    case HEADERS_RELOAD:
        ctx->reload = true;
        ctx->redraw = true;
        break;
    case TITLE_REDRAW:
        ctx->redraw_header_lines = 3;
        break;
    case READ_REDRAW:
        ctx->redraw_footer_lines = 1;
        break;
    case DONOTHING:
    default:
        break;
    }
}

///////////////////////////////////////////////////////////////////////////
// Layer 2: Read Navigation & Control Commands

static int
read_cmd_num(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    int num;
    if ((num = search_num(ctx->key, last_line)) != -1)
        read_move_cursor(cx, ctx, num + 1, 10);
    ctx->redraw_footer_lines = 1;
    return 0;
}

static int
read_cmd_quit(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    cx->locmem->top_ln = read_view_v2p(&cx->view, ctx->base);
    if (currmode & MODE_SELECT) {
        char genbuf[PATHLEN];
        int real_recno = (cx->view.total > 0 && cx->locmem->crs_ln > 0) ? read_view_real_recno(cx, cx->locmem->crs_ln) : 0;
        board_select();
        cx->sr_pred_count = 0;
        cx->sr_mode_mask = 0;
        free(cx->view.remap);
        cx->view.remap = NULL;
        cx->view.remap_base = 0;
        cx->view.remap_count = 0;
        if (currstat == RMAIL)
            sethomedir(genbuf, cuser.userid);
        else
            setbdir(genbuf, currboard);
        cx->locmem = getkeep(genbuf, 0, 1);
        if (real_recno > 0) {
            cx->locmem->crs_ln = real_recno;
            int num = cx->locmem->crs_ln - p_lines + 1;
            cx->locmem->top_ln = num < 1 ? 1 : num;
        }
        read_apply_mode(cx, ctx, NEWDIRECT);
    } else {
        int mode = (currmode & MODE_DIGEST) ? board_digest() : DOQUIT;
        read_apply_mode(cx, ctx, mode);
    }
    return 0;
}

static int
read_cmd_up(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    int cur_ln = read_view_v2p(&cx->view, ctx->curr);
    if (cur_ln <= 1)
        read_move_cursor(cx, ctx, last_line, p_lines - 1);
    else
        read_move_cursor(cx, ctx, cur_ln - 1, p_lines - 2);
    return 0;
}

static int
read_cmd_down(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    int cur_ln = read_view_v2p(&cx->view, ctx->curr);
    read_move_cursor(cx, ctx, cur_ln + 1, 1);
    return 0;
}

static int
read_cmd_pgdn(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    int top_ln = read_view_v2p(&cx->view, ctx->base);
    read_move_cursor(cx, ctx, top_ln + p_lines, 0);
    return 0;
}

static int
read_cmd_pgup(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    int top_ln = read_view_v2p(&cx->view, ctx->base);
    read_move_cursor(cx, ctx, top_ln - p_lines, 0);
    return 0;
}

static int
read_cmd_home(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    read_move_cursor(cx, ctx, 1, 0);
    return 0;
}

static int
read_cmd_end(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    read_move_cursor(cx, ctx, last_line, p_lines - 1);
    return 0;
}

static const cmd_t read_nav_cmds[] = {
    { '0', NULL, "輸入編號跳轉", read_cmd_num, 0, CMD_PRIO_NONE, true },
    { '1', NULL, NULL, read_cmd_num, 0, CMD_PRIO_NONE, true },
    { '2', NULL, NULL, read_cmd_num, 0, CMD_PRIO_NONE, true },
    { '3', NULL, NULL, read_cmd_num, 0, CMD_PRIO_NONE, true },
    { '4', NULL, NULL, read_cmd_num, 0, CMD_PRIO_NONE, true },
    { '5', NULL, NULL, read_cmd_num, 0, CMD_PRIO_NONE, true },
    { '6', NULL, NULL, read_cmd_num, 0, CMD_PRIO_NONE, true },
    { '7', NULL, NULL, read_cmd_num, 0, CMD_PRIO_NONE, true },
    { '8', NULL, NULL, read_cmd_num, 0, CMD_PRIO_NONE, true },
    { '9', NULL, NULL, read_cmd_num, 0, CMD_PRIO_NONE, true },
    { KEY_LEFT, "離開", "離開列表或結束搜尋", read_cmd_quit, 0, CMD_PRIO_MAX },
    { 'q', NULL, NULL, read_cmd_quit, 0, CMD_PRIO_NONE },
    { 'e', NULL, NULL, read_cmd_quit, 0, CMD_PRIO_NONE },
    { 'p', NULL, "向上移動", read_cmd_up, 0, CMD_PRIO_NONE, true },
    { 'k', NULL, NULL, read_cmd_up, 0, CMD_PRIO_NONE, true },
    { KEY_UP, NULL, NULL, read_cmd_up, 0, CMD_PRIO_NONE, true },
    { 'n', NULL, "向下移動", read_cmd_down, 0, CMD_PRIO_NONE, true },
    { 'j', NULL, NULL, read_cmd_down, 0, CMD_PRIO_NONE, true },
    { KEY_DOWN, NULL, NULL, read_cmd_down, 0, CMD_PRIO_NONE, true },
    { ' ', NULL, "向下翻頁", read_cmd_pgdn, 0, CMD_PRIO_NONE, true },
    { KEY_PGDN, NULL, NULL, read_cmd_pgdn, 0, CMD_PRIO_NONE, true },
    { 'N', NULL, NULL, read_cmd_pgdn, 0, CMD_PRIO_NONE, true },
    { Ctrl('F'), NULL, NULL, read_cmd_pgdn, 0, CMD_PRIO_NONE, true },
    { KEY_PGUP, NULL, "向上翻頁", read_cmd_pgup, 0, CMD_PRIO_NONE, true },
    { Ctrl('B'), NULL, NULL, read_cmd_pgup, 0, CMD_PRIO_NONE, true },
    { 'P', NULL, NULL, read_cmd_pgup, 0, CMD_PRIO_NONE, true },
    { KEY_HOME, NULL, "移至第一筆", read_cmd_home, 0, CMD_PRIO_NONE, true },
    { KEY_END, NULL, "移至最後一筆", read_cmd_end, 0, CMD_PRIO_NONE, true },
    { '$', NULL, NULL, read_cmd_end, 0, CMD_PRIO_NONE, true },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

///////////////////////////////////////////////////////////////////////////
// Layer 1: Read Common Search, Thread & Tag Commands

static int
read_cmd_aid(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    cx->locmem->top_ln = read_view_v2p(&cx->view, ctx->base);
    int new_ln = cx->locmem->crs_ln;
    char default_ch = 0;
    int mode = select_by_aid(cx, &new_ln, &newdirect_new_ln, &default_ch);
    /* select_by_aid may have refreshed the bottom layout and last_line. */
    ctx->total = cx->view.total;
    if (new_ln != cx->locmem->crs_ln)
        read_move_cursor(cx, ctx, new_ln, 10);
    if (default_ch != 0) {
        ctx->key = default_ch;
        ctx->redispatch = true;
    }
    read_apply_mode(cx, ctx, mode);
    return 0;
}

static int
read_cmd_search_newpost(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    read_apply_mode(cx, ctx, select_read(cx, RS_NEWPOST));
    return 0;
}

static int
read_cmd_search_recommend(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    read_apply_mode(cx, ctx, select_read(cx, RS_RECOMMEND));
    return 0;
}

static int
read_cmd_search_author(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    read_apply_mode(cx, ctx, select_read(cx, RS_AUTHOR));
    return 0;
}

static int
read_cmd_search_money(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    read_apply_mode(cx, ctx, select_read(cx, RS_MONEY));
    return 0;
}

static int
read_cmd_search_mark(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    int mode;
    switch (vans(currmode & MODE_SELECT ?
                 "增加條件 標記(m/s)(未輸入則取消): " :
                 "搜尋標記(m/s)(未輸入則取消): ")) {
        case 's':
            mode = select_read(cx, RS_SOLVED);
            break;
        case 'm':
            mode = select_read(cx, RS_MARK);
            break;
        default:
            mode = READ_REDRAW;
            break;
    }
    read_apply_mode(cx, ctx, mode);
    return 0;
}

static int
read_cmd_search_keyword(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    read_apply_mode(cx, ctx, select_read(cx, RS_KEYWORD));
    return 0;
}

static int
read_cmd_search_title(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    read_apply_mode(cx, ctx, select_read(cx, RS_TITLE));
    return 0;
}

static int
read_cmd_search_exclude(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    read_apply_mode(cx, ctx, select_read(cx, RS_KEYWORD_EXCLUDE));
    return 0;
}

static int
read_cmd_thread_first(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    read_move_cursor(cx, ctx, thread(cx, RELATE_FIRST), 10);
    return 0;
}

static int
read_cmd_cursor_first(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    read_move_cursor(cx, ctx, thread(cx, CURSOR_FIRST), 10);
    return 0;
}

static int
read_cmd_thread_next(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    read_move_cursor(cx, ctx, thread(cx, RELATE_NEXT), 10);
    return 0;
}

static int
read_cmd_cursor_next(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    read_move_cursor(cx, ctx, thread(cx, CURSOR_NEXT), 10);
    return 0;
}

static int
read_cmd_thread_prev(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    read_move_cursor(cx, ctx, thread(cx, RELATE_PREV), 10);
    return 0;
}

static int
read_cmd_cursor_prev(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    read_move_cursor(cx, ctx, thread(cx, CURSOR_PREV), 10);
    return 0;
}

static int
read_cmd_newpost_prev(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    read_move_cursor(cx, ctx, thread(cx, NEWPOST_PREV), 10);
    return 0;
}

static int
read_cmd_newpost_next(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    read_move_cursor(cx, ctx, thread(cx, NEWPOST_NEXT), 10);
    return 0;
}

static int
read_cmd_query(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    int row = ctx->curr - ctx->base;
    if (row >= 0 && row < cx->entries)
        read_apply_mode(cx, ctx, my_query(headers[row].owner));
    return 0;
}

static int
read_cmd_edituser(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    int row = ctx->curr - ctx->base;
    if (row >= 0 && row < cx->entries) {
        int id;
        userec_t muser;
        vs_hdr("使用者設定");
        move(1, 0);
        if ((id = getuser(headers[row].owner, &muser))) {
            user_display(&muser, 1);
            if (HasUserPerm(PERM_ACCOUNTS))
                uinfo_query(muser.userid, 1, id);
            else
                pressanykey();
        }
        read_apply_mode(cx, ctx, FULLUPDATE);
    }
    return 0;
}

static int
read_cmd_tag(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    int row = ctx->curr - ctx->base;
    if (row < 0 || row >= cx->entries)
        return 0;
    if ((currstat & RMAIL && TagBoard != 0) ||
        (!(currstat & RMAIL) && TagBoard != cx->bid)) {
        if (currstat & RMAIL)
            TagBoard = 0;
        else
            TagBoard = cx->bid;
        ClearTagList();
    }
    if (ToggleTagItem(&headers[row])) {
        int cur_ln = read_view_v2p(&cx->view, ctx->curr);
        read_move_cursor(cx, ctx, cur_ln + 1, 1);
        read_apply_mode(cx, ctx, PARTUPDATE);
    }
    return 0;
}

static int
read_cmd_clear_tag(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    if (!IsEmptyTagList()) {
        ClearTagList();
        read_apply_mode(cx, ctx, FULLUPDATE);
    }
    return 0;
}

static int
read_cmd_tag_thread(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    if ((currstat & RMAIL && TagBoard != 0) ||
        (!(currstat & RMAIL) && TagBoard != cx->bid)) {
        if (currstat & RMAIL)
            TagBoard = 0;
        else
            TagBoard = cx->bid;
        ClearTagList();
    }
    read_apply_mode(cx, ctx, TagThreadView(cx, currdirect));
    return 0;
}

static int
read_cmd_tag_prune(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    int mode;
    if (currmode & MODE_SELECT) {
        vmsg("請先離開搜尋模式(T標記會保留)再刪除檔案。");
        mode = FULLUPDATE;
    } else {
        mode = TagPruner(cx->bid);
    }
    read_apply_mode(cx, ctx, mode);
    return 0;
}

static int
read_cmd_search_prev(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    read_move_cursor(cx, ctx, search_read(cx, cx->bid, READ_PREV), 10);
    return 0;
}

static int
read_cmd_search_next(cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
    read_move_cursor(cx, ctx, search_read(cx, cx->bid, READ_NEXT), 10);
    return 0;
}

static const cmd_t read_common_cmds[] = {
    { '/', "搜尋", "搜尋標題關鍵字", read_cmd_search_keyword, 0, CMD_PRIO_NORM, true },
    { '?', NULL, NULL, read_cmd_search_keyword, 0, CMD_PRIO_NONE, true },
    { 'a', "找作者", "搜尋作者帳號", read_cmd_search_author, 0, CMD_PRIO_NORM, true },
    { ']', "主題", "同主題下一篇", read_cmd_thread_next, 0, CMD_PRIO_NORM, true },
    { '#', "找AID", "以文章代碼(AID)搜尋", read_cmd_aid, 0, CMD_PRIO_LOW, true },
    { Ctrl('H'), NULL, "只列主題首篇(不含回文)", read_cmd_search_newpost, 0, CMD_PRIO_NONE, true },
    { 'Z', "找推文數", "搜尋推文數條件", read_cmd_search_recommend, 0, CMD_PRIO_LOW, true },
    { 'A', NULL, "搜尋稿酬金額條件", read_cmd_search_money, 0, CMD_PRIO_NONE, true },
    { 'G', "找標記", "搜尋 m 或 s 標記文章", read_cmd_search_mark, 0, CMD_PRIO_LOW, true },
    { 'S', NULL, "搜尋同標題文章", read_cmd_search_title, 0, CMD_PRIO_NONE, true },
    { '!', NULL, "排除關鍵字搜尋", read_cmd_search_exclude, 0, CMD_PRIO_NONE, true },
    { '=', "首篇", "跳至同主題第一篇", read_cmd_thread_first, 0, CMD_PRIO_LOW, true },
    { '\\', NULL, "跳至游標主題首篇", read_cmd_cursor_first, 0, CMD_PRIO_NONE, true },
    { '+', NULL, "游標主題下一篇", read_cmd_cursor_next, 0, CMD_PRIO_NONE, true },
    { '[', NULL, "同主題上一篇", read_cmd_thread_prev, 0, CMD_PRIO_NONE, true },
    { '-', NULL, "游標主題上一篇", read_cmd_cursor_prev, 0, CMD_PRIO_NONE, true },
    { ',', NULL, "上一篇新主題(首篇)", read_cmd_newpost_prev, 0, CMD_PRIO_NONE, true },
    { '<', NULL, NULL, read_cmd_newpost_prev, 0, CMD_PRIO_NONE, true },
    { '.', NULL, "下一篇新主題(首篇)", read_cmd_newpost_next, 0, CMD_PRIO_NONE, true },
    { '>', NULL, NULL, read_cmd_newpost_next, 0, CMD_PRIO_NONE, true },
    { Ctrl('Q'), NULL, "查詢作者名片檔", read_cmd_query, PERM_LOGINOK, CMD_PRIO_NONE, true },
    { Ctrl('S'), NULL, "查詢/設定使用者資料", read_cmd_edituser, PERM_ACCOUNTS | PERM_SYSOP, CMD_PRIO_NONE, true },
    { 't', "標記", "標記/取消標記文章", read_cmd_tag, 0, CMD_PRIO_LOW, true },
    { Ctrl('C'), NULL, "清除所有文章標記", read_cmd_clear_tag, 0, CMD_PRIO_NONE, true },
    { '*', NULL, "標記同主題串文章", read_cmd_tag_thread, 0, CMD_PRIO_NONE, true },
    { Ctrl('T'), NULL, NULL, read_cmd_tag_thread, 0, CMD_PRIO_NONE, true },
    { Ctrl('D'), NULL, "批次刪除已標記文章", read_cmd_tag_prune, 0, CMD_PRIO_NONE, true },
    { '{', NULL, "尋找上一篇已讀文章", read_cmd_search_prev, 0, CMD_PRIO_NONE, true },
    { '}', NULL, "尋找下一篇已讀文章", read_cmd_search_next, 0, CMD_PRIO_NONE, true },
    { 0, NULL, NULL, NULL, 0, CMD_PRIO_NONE }
};

///////////////////////////////////////////////////////////////////////////
// Read Domain Execution Helpers

int
read_exec_noitem(read_noitem_func_t func, cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    if (cx->locmem) {
        cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
        cx->locmem->top_ln = read_view_v2p(&cx->view, ctx->base);
    }
    int mode = (*func)();
    read_apply_mode(cx, ctx, mode);
    return 0;
}

int
read_exec_item(read_item_func_t func, cmd_ctx_t *ctx) {
    read_ctx_t *cx = (read_ctx_t *)ctx->priv;
    int lastmode = DONOTHING;

    while (1) {
        cx->locmem->crs_ln = read_view_v2p(&cx->view, ctx->curr);
        cx->locmem->top_ln = read_view_v2p(&cx->view, ctx->base);

        int real_ent = read_view_real_recno(cx, cx->locmem->crs_ln);
        int row = ctx->curr - ctx->base;
        int mode = (*func)(real_ent, &headers[row], currdirect, row);

        if (mode == READ_SKIP)
            mode = lastmode;

        if (mode == RET_SELECTAID) {
            int new_ln = cx->locmem->crs_ln;
            char default_ch = 0;
            select_by_aid(cx, &new_ln, &newdirect_new_ln, &default_ch);
            if (new_ln >= 1 && new_ln <= last_line)
                read_move_cursor(cx, ctx, new_ln, 10);
            if (default_ch != 0) {
                ctx->key = default_ch;
                ctx->redispatch = true;
            }
            ctx->reload = true;
            ctx->redraw = true;
            return 0;
        }

        if (mode == READ_PREV || mode == READ_NEXT ||
            mode == RELATE_PREV || mode == RELATE_FIRST ||
            mode == AUTHOR_NEXT || mode == AUTHOR_PREV ||
            mode == RELATE_NEXT) {
            lastmode = mode;
            int new_ln = cx->locmem->crs_ln;
            char default_ch = 0;
            if (read_key_handle_mode_navigation(cx, mode, &new_ln, &default_ch)) {
                ctx->reload = true;
                ctx->redraw = true;
                return 0;
            }
            if (new_ln < 1 || new_ln > last_line) {
                ctx->reload = true;
                ctx->redraw = true;
                return 0;
            }
            read_move_cursor(cx, ctx, new_ln, 10);
            cx->entries = read_view_load_window(cx, currdirect, headers,
                                                ctx->base, headers_size);
            continue;
        }

        read_apply_mode(cx, ctx, mode);
        return 0;
    }
}

static const char *
i_read_caption(void)
{
    if (currstat == RMAIL)
        return " 信件列表 ";
    if (currmode & MODE_DIGEST)
        return " 文摘列表 ";
    if (currmode & MODE_SELECT)
        return " 系列文章 ";
    return " 文章列表 ";
}

static int
read_header(PSB_CTX *psbctx)
{
    read_ctx_t *cx = (read_ctx_t *)psbctx->cmd.priv;
    (*cx->dotitle)();
    return 0;
}

static int
read_footer(PSB_CTX *psbctx GCC_UNUSED)
{
    if (currstat == RMAIL)
        vs_footer(" 鴻雁往返 ",
            " (R/y)回信 (x)站內轉寄 (d/D)刪信 (^P)寄發新信 \t(←/q)離開");
    else
        vs_footer(" 文章選讀 ",
            " (y)回應(X)推文(^X)轉錄 (=[]<>)相關主題(/?a)找標題/作者 (b)進板畫面");
    return 0;
}

static int
read_empty_renderer(PSB_CTX *psbctx GCC_UNUSED)
{
    outs("    沒有文章...");
    return 0;
}

static int
read_renderer(int idx, PSB_CTX *psbctx)
{
    read_ctx_t *cx = (read_ctx_t *)psbctx->cmd.priv;
    int i = idx - psbctx->cmd.base;
    if (i >= 0 && i < cx->entries) {
        int disp_num = read_view_v2p(&cx->view, idx);
        fileheader_t fh = headers[i];   /* display copy only */
        /* Pinned state follows the loaded view, not live SHM counts: rows
         * redrawn without a reload must match what the cursor acts on. */
        if (cx->bidcache > 0 && !(currmode & (MODE_SELECT | MODE_DIGEST))) {
            if (disp_num > cx->view.bottom_line)
                fh.filemode |= FILE_BOTTOM;
            else
                fh.filemode &= ~FILE_BOTTOM;  /* original of a pinned post */
        }
        (*cx->doentry)(disp_num, &fh);
    }
    return 0;
}

static int
read_cursor(int y, PSB_CTX *psbctx GCC_UNUSED)
{
    cursor_show(y, 0);
    return 0;
}

static int
read_loader(PSB_CTX *psbctx)
{
    read_ctx_t *cx = (read_ctx_t *)psbctx->cmd.priv;
    const size_t FHSZ = sizeof(fileheader_t);

    if (currbid > 0 && time4_gt(getbcache(currbid)->perm_reload, cx->enter_time)) {
        boardheader_t *bp = getbcache(currbid);
        if (!HasBoardPerm(bp)) {
            psbctx->cmd.quit = true;
            return -1;
        }
        cx->enter_time = bp->perm_reload;
    }

    setutmpmode(cx->cmdmode);
    psbctx->cmd.caption = i_read_caption();

    if (psbctx->cmd.reload) {
        if (currmode & MODE_SELECT) {
            if (!cx->sr_just_selected && cx->sr_pred_count > 0) {
                int bid = (currstat != RMAIL && currboard[0] && currbid > 0) ? currbid : 0;
                int total = 0;
                int loaded = search_predicates_window(currdirect, bid,
                                                      cx->sr_preds, cx->sr_pred_count,
                                                      cx->view.remap_base, SEARCH_SVC_WINDOW_SIZE,
                                                      cx->view.remap, &total);
                if (loaded >= 0) {
                    cx->view.total = total;
                    cx->view.remap_count = loaded;
                }
            }
            cx->sr_just_selected = false;
            cx->bottom_line = last_line = cx->view.total;
            cx->view.bottom_count = 0;
        } else if (cx->bidcache > 0 && !(currmode & MODE_DIGEST)) {
            if (cx->view.remap) {
                free(cx->view.remap);
                cx->view.remap = NULL;
                cx->view.remap_base = 0;
                cx->view.remap_count = 0;
                cx->sr_pred_count = 0;
                cx->sr_mode_mask = 0;
            }
            if ((last_line = getbtotal(currbid)) == 0) {
                setbtotal(currbid);
                last_line = getbtotal(currbid);
            }
            cx->bottom_line = last_line;
            cx->view.bottom_count = resolve_board_bottoms(currbid, cx->view.bottom_recs);
            last_line += cx->view.bottom_count;
        } else {
            if (cx->view.remap) {
                free(cx->view.remap);
                cx->view.remap = NULL;
                cx->view.remap_base = 0;
                cx->view.remap_count = 0;
                cx->sr_pred_count = 0;
                cx->sr_mode_mask = 0;
            }
            cx->bottom_line = last_line = get_num_records(currdirect, FHSZ);
            cx->view.bottom_count = 0;
        }

        cx->bid = currbid;
        cx->view.total = last_line;
        cx->view.bottom_line = cx->bottom_line;

        if (cx->is_newdirect) {
            cx->is_newdirect = false;
            int num = last_line - p_lines + 1;
            if (currmode & MODE_SELECT) {
                cx->sr_locmem.top_ln = num < 1 ? 1 : num;
                cx->sr_locmem.crs_ln = last_line > 0 ? last_line : 1;
                cx->locmem = &cx->sr_locmem;
            } else {
                cx->locmem = getkeep(currdirect, num < 1 ? 1 : num,
                                     cx->bottom_line ? cx->bottom_line : last_line);
            }
            if (newdirect_new_ln >= 0) {
                cx->locmem->crs_ln = newdirect_new_ln + 1;
                newdirect_new_ln = -1;
            }
        }

        if (cx->locmem) {
            if (cx->locmem->top_ln > last_line) {
                int recbase = last_line - p_lines + 1;
                if (recbase < 1)
                    recbase = 1;
                cx->locmem->top_ln = recbase;
            }
            if (cx->locmem->crs_ln > last_line)
                cx->locmem->crs_ln = last_line;
            psbctx->cmd.curr = read_view_p2v(&cx->view, cx->locmem->crs_ln);
            psbctx->cmd.base = read_view_p2v(&cx->view, cx->locmem->top_ln);
        }

        psbctx->cmd.total = last_line;
        return 0;
    }

    if (headers_size != p_lines) {
        headers_size = p_lines;
        headers = (fileheader_t *)realloc(headers, headers_size * FHSZ);
        assert(headers);
    }

    if (cx->bidcache > 0 && !(currmode & (MODE_SELECT | MODE_DIGEST))) {
        int btotal = getbtotal(currbid);
        /* Always re-resolve: pinned records may have shifted (physical
         * deletion) even when the counts are unchanged. */
        cx->view.bottom_count = resolve_board_bottoms(currbid, cx->view.bottom_recs);
        int rec_num = btotal + cx->view.bottom_count;
        if (last_line != rec_num || cx->bottom_line != btotal) {
            cx->bottom_line = btotal;
            last_line = rec_num;
            cx->view.total = last_line;
            cx->view.bottom_line = cx->bottom_line;
            psbctx->cmd.total = last_line;
        }
    }

    int old_total = cx->view.total;
    cx->locmem->top_ln = read_view_v2p(&cx->view, psbctx->cmd.base);
    cx->locmem->crs_ln = read_view_v2p(&cx->view, psbctx->cmd.curr);
    cx->entries = read_view_load_window(cx, currdirect, headers,
                                        psbctx->cmd.base, headers_size);
    /* view.total may also have been changed by commands (e.g. select_by_aid,
     * read_view_ensure_window); always resync psb's total. */
    if (cx->view.total != old_total || psbctx->cmd.total != cx->view.total) {
        psbctx->cmd.total = cx->view.total;
        if (psbctx->cmd.total <= 0) {
            psbctx->cmd.curr = 0;
            psbctx->cmd.base = 0;
        } else {
            if (psbctx->cmd.curr >= psbctx->cmd.total)
                psbctx->cmd.curr = psbctx->cmd.total - 1;
            if (psbctx->cmd.base >= psbctx->cmd.total)
                psbctx->cmd.base = (psbctx->cmd.total > headers_size) ? (psbctx->cmd.total - headers_size) : 0;
        }
        cx->locmem->top_ln = read_view_v2p(&cx->view, psbctx->cmd.base);
        cx->locmem->crs_ln = read_view_v2p(&cx->view, psbctx->cmd.curr);
        cx->entries = read_view_load_window(cx, currdirect, headers,
                                            psbctx->cmd.base, headers_size);
    }
    return 0;
}

void
i_read(int cmdmode, const char *direct, void (*dotitle)(),
       void (*doentry)(int, fileheader_t *), const cmd_t *rcmdlist,
       int bidcache)
{
    char            currdirect0[PATHLEN];
    int             last_line0 = last_line;
    fileheader_t   *headers0 = headers;
    int             headers_size0 = headers_size;
    const size_t    FHSZ = sizeof(fileheader_t);

    STRLCPY(currdirect0, currdirect);
    /* Search state lives in this read_ctx_t; do not inherit (or clear) the
     * outer i_read's select mode (e.g. reading mail from a searched board). */
    int select0 = currmode & MODE_SELECT;
    int srmode0 = currsrmode;
    currmode &= ~MODE_SELECT;
    currsrmode = 0;
    headers_size = p_lines;
    headers = (fileheader_t *)calloc(headers_size, FHSZ);
    assert(headers != NULL);
    STRLCPY(currdirect, direct);

    read_ctx_t cx = {
        .rcmdlist = rcmdlist,
        .locmem = NULL,
        .cmdmode = cmdmode,
        .bid = currbid,
        .bidcache = bidcache,
        .bottom_line = 0,
        .entries = 0,
        .is_newdirect = true,
        .enter_time = now,
        .dotitle = dotitle,
        .doentry = doentry,
        .view = { .total = 0, .bottom_line = 0, .reverse_order = false },
    };

    cmd_layer_t layers[] = {
        { rcmdlist,         &cx },
        { read_common_cmds, &cx },
        { read_nav_cmds,    &cx },
        { bbs_global_cmds,  NULL },
        { NULL, NULL }
    };

    PSB_CTX psbctx = {
        .cmd = {
            .curr = 0,
            .priv = &cx,
            .caption = i_read_caption(),
        },
        .header_lines = 3,
        .footer_lines = 1,
        .layers = layers,
        .loader = read_loader,
        .header = read_header,
        .footer = read_footer,
        .renderer = read_renderer,
        .empty_renderer = read_empty_renderer,
        .cursor = read_cursor,
    };

    psb_main(&psbctx);

    if (currmode & MODE_SELECT)
        board_select();
    currmode |= select0;
    currsrmode = srmode0;
    free(cx.view.remap);
    free(headers);
    last_line = last_line0;
    headers = headers0;
    headers_size = headers_size0;
    STRLCPY(currdirect, currdirect0);
}
