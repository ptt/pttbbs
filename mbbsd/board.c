/* $Id$ */
#include "bbs.h"

/* personal board state
 * 相對於看板的 attr (BRD_* in ../include/pttstruct.h),
 * 這些是用在 user interface 的 flag */
#define NBRD_FAV    	 1
#define NBRD_BOARD	 2
#define NBRD_LINE   	 4
#define NBRD_FOLDER	 8
#define NBRD_TAG	16
#define NBRD_UNREAD     32
#define NBRD_SYMBOLIC   64

#define TITLE_MATCH(bptr, key)	((key)[0] && !strcasestr((bptr)->title, (key)))


#define B_TOTAL(bptr)        (SHM->total[(bptr)->bid - 1])
#define B_LASTPOSTTIME(bptr) (SHM->lastposttime[(bptr)->bid - 1])
#define B_BH(bptr)           (&bcache[(bptr)->bid - 1])
typedef struct {
    int             bid;
    unsigned char   myattr;
} __attribute__ ((packed)) boardstat_t;

/**
 * class_bid 的意義
 *   class_bid < 0   熱門看板
 *   class_bid = 0   我的最愛
 *   class_bid = 1   分類看板
 *   class_bid > 1   其他目錄
 */
#define IN_HOTBOARD()	(class_bid < 0)
#define IN_FAVORITE()	(class_bid == 0)
#define IN_CLASSROOT()	(class_bid == 1)
#define IN_SUBCLASS()	(class_bid > 1)
#define IN_CLASS()	(class_bid > 0)
static int      class_bid = 0;

static boardstat_t *nbrd = NULL;
static char	choose_board_depth = 0;
static short    brdnum;
static char     yank_flag = 1;

/* These are all the states yank_flag may be. */
#define LIST_FAV()         (yank_flag = 0)
#define LIST_BRD()         (yank_flag = 1)
#define LIST_GUEST()       (yank_flag = 2)
#define IS_LISTING_FAV()   (yank_flag == 0)
#define IS_LISTING_BRD()   (yank_flag == 1)
#define IS_LISTING_GUEST() (yank_flag == 2)

inline int getbid(const boardheader_t *fh)
{
    return (fh - bcache);
}
inline boardheader_t *getparent(const boardheader_t *fh)
{
    if(fh->parent>0)
	return &bcache[fh->parent-1];
    else
	return NULL;
}

void imovefav(int old)
{
    char buf[5];
    int new;
    
    getdata(b_lines - 1, 0, "請輸入新次序:", buf, sizeof(buf), DOECHO);
    new = atoi(buf) - 1;
    if (new < 0 || brdnum <= new){
	vmsg("輸入範圍有誤!");
	return;
    }
    move_in_current_folder(old, new);
}

void
init_brdbuf(void)
{
    if (brc_initialize())
	return;
    brc_initial_board(DEFAULT_BOARD);
    set_board();
}

void
save_brdbuf(void)
{
    fav_save();
    fav_free();
}

int
HasBoardPerm(boardheader_t *bptr)
{
    register int    level, brdattr;

    level = bptr->level;
    brdattr = bptr->brdattr;

    if (HasUserPerm(PERM_SYSOP))
	return 1;

    if( is_BM_cache(bptr - bcache + 1) ) /* XXXbid */
	return 1;

    /* 祕密看板：核對首席板主的好友名單 */

    if (brdattr & BRD_HIDE) {	/* 隱藏 */
	if (hbflcheck((int)(bptr - bcache) + 1, currutmp->uid)) {
	    if (brdattr & BRD_POSTMASK)
		return 0;
	    else
		return 2;
	} else
	    return 1;
    }
    /* 限制閱讀權限 */
    if (level && !(brdattr & BRD_POSTMASK) && !HasUserPerm(level))
	return 0;

    return 1;
}

static int
check_newpost(boardstat_t * ptr)
{				/* Ptt 改 */
    int             tbrc_num;
    time4_t         ftime;
    time4_t        *tbrc_list;

    ptr->myattr &= ~NBRD_UNREAD;
    if (B_BH(ptr)->brdattr & (BRD_GROUPBOARD | BRD_SYMBOLIC))
	return 0;

    if (B_TOTAL(ptr) == 0)
       {
	setbtotal(ptr->bid);
	setbottomtotal(ptr->bid);
       }
    if (B_TOTAL(ptr) == 0)
	return 0;
    ftime = B_LASTPOSTTIME(ptr);

    /* 有些 util, 尤其是 innbbsd, 會用到比較新的 time stamp,
     * 只要不太誇張就 ok */
    if (ftime > now + 10) 
	ftime = B_LASTPOSTTIME(ptr) = now - 1;

    tbrc_list = brc_find_record(ptr->bid, &tbrc_num);
    if ( brc_unread_time(ftime, tbrc_num, tbrc_list) )
	ptr->myattr |= NBRD_UNREAD;
    
    return 1;
}

static void
load_uidofgid(const int gid, const int type)
{
    boardheader_t  *bptr, *currbptr, *parent;
    int             bid, n, childcount = 0;
    currbptr = parent = &bcache[gid - 1];
    for (n = 0; n < numboards; ++n) {
	bid = SHM->bsorted[type][n]+1;
	if( bid<=0 || !(bptr = &bcache[bid-1]) 
		|| bptr->brdname[0] == '\0' )
	    continue;
	if (bptr->gid == gid) {
	    if (currbptr == parent)
		currbptr->firstchild[type] = bid;
	    else {
		currbptr->next[type] = bid;
		currbptr->parent = gid;
	    }
	    childcount++;
	    currbptr = bptr;
	}
    }
    parent->childcount = childcount;
    if (currbptr == parent) // no child
	currbptr->firstchild[type] = -1;
    else // the last child
	currbptr->next[type] = -1;
}

static boardstat_t *
addnewbrdstat(int n, int state)
{
    boardstat_t    *ptr;

    ptr = &nbrd[brdnum++];
    //boardheader_t  *bptr = &bcache[n];
    //ptr->total = &(SHM->total[n]);
    //ptr->lastposttime = &(SHM->lastposttime[n]);
    
    ptr->bid = n + 1;
    ptr->myattr = state;
    if ((B_BH(ptr)->brdattr & BRD_HIDE) && state == NBRD_BOARD)
	B_BH(ptr)->brdattr |= BRD_POSTMASK;
    if (!IS_LISTING_FAV())
	ptr->myattr &= ~NBRD_FAV;
    check_newpost(ptr);
    return ptr;
}

#if !HOTBOARDCACHE
static int
cmpboardfriends(const void *brd, const void *tmp)
{
#ifdef USE_COOLDOWN
    if ((B_BH((boardstat_t*)tmp)->brdattr & BRD_COOLDOWN) &&
	    (B_BH((boardstat_t*)brd)->brdattr & BRD_COOLDOWN))
	return 0;
    else if ( B_BH((boardstat_t*)tmp)->brdattr & BRD_COOLDOWN ) {
	if (B_BH((boardstat_t*)brd)->nuser == 0)
	    return 0;
	else
	    return 1;
    }
    else if ( B_BH((boardstat_t*)brd)->brdattr & BRD_COOLDOWN ) {
	if (B_BH((boardstat_t*)tmp)->nuser == 0)
	    return 0;
	else
	    return -1;
    }
#endif
    return ((B_BH((boardstat_t*)tmp)->nuser) -
	    (B_BH((boardstat_t*)brd)->nuser));
}
#endif

static void
load_boards(char *key)
{
    boardheader_t  *bptr = NULL;
    int             type = cuser.uflag & BRDSORT_FLAG ? 1 : 0;
    int             i, n, bid;
    int             state;

    if (IN_CLASS()) {
	bptr = getbcache(class_bid);
	if (bptr->firstchild[type] == 0 )
	    load_uidofgid(class_bid, type);
    }
    brdnum = 0;
    if (nbrd) {
        free(nbrd);
	nbrd = NULL;
    }
    if (!IN_CLASS()) {
	if(IS_LISTING_FAV()){
	    fav_t   *fav = get_current_fav();
	    int     nfav = get_data_number(fav);
	    if( nfav == 0 ){
		nbrd = (boardstat_t *)malloc(sizeof(boardstat_t) * 1);
		goto EMPTYFAV;
	    }
	    nbrd = (boardstat_t *)malloc(sizeof(boardstat_t) * nfav);
            for( i = 0 ; i < fav->DataTail; ++i ){
		int state;
		if (!(fav->favh[i].attr & FAVH_FAV))
		    continue;

		if ( !key[0] ){
		    if (get_item_type(&fav->favh[i]) == FAVT_LINE )
			state = NBRD_LINE;
		    else if (get_item_type(&fav->favh[i]) == FAVT_FOLDER )
			state = NBRD_FOLDER;
		    else {
			bptr = getbcache(fav_getid(&fav->favh[i]));
			state = NBRD_BOARD;
			if (is_set_attr(&fav->favh[i], FAVH_UNREAD))
			    state |= NBRD_UNREAD;
		    }
		} else {
		    if (get_item_type(&fav->favh[i]) == FAVT_LINE )
			continue;
		    else if (get_item_type(&fav->favh[i]) == FAVT_FOLDER ){
			if( strcasestr(
			    get_folder_title(fav_getid(&fav->favh[i])),
			    key)
			)
			    state = NBRD_FOLDER;
			else
			    continue;
		    }else{
			bptr = getbcache(fav_getid(&fav->favh[i]));
			if( HasBoardPerm(bptr) && strcasestr(bptr->title, key))
			    state = NBRD_BOARD;
			else
			    continue;
			if (is_set_attr(&fav->favh[i], FAVH_UNREAD))
			    state |= NBRD_UNREAD;
		    }
		}

		if (is_set_attr(&fav->favh[i], FAVH_TAG))
		    state |= NBRD_TAG;
		if (is_set_attr(&fav->favh[i], FAVH_ADM_TAG))
		    state |= NBRD_TAG;
		addnewbrdstat(fav_getid(&fav->favh[i]) - 1, NBRD_FAV | state);
	    }
	EMPTYFAV:
	    if (brdnum == 0)
		addnewbrdstat(0, 0);
	}
#if HOTBOARDCACHE
	else if(IN_HOTBOARD()){
	    nbrd = (boardstat_t *)malloc(sizeof(boardstat_t) * SHM->nHOTs);
	    for( i = 0 ; i < SHM->nHOTs ; ++i ) {
		if(SHM->HBcache[i] == -1)
		    continue;
		addnewbrdstat(SHM->HBcache[i], HasBoardPerm(&bcache[SHM->HBcache[i]]));
	    }
	}
#endif
	else { // general case
	    nbrd = (boardstat_t *) malloc(sizeof(boardstat_t) * numboards);
	    for (i = 0; i < numboards; i++) {
		n = SHM->bsorted[type][i];
		if (n<0 || (bptr = &bcache[n]) == NULL)
		    continue;
		if (!bptr->brdname[0] ||
		    (bptr->brdattr & (BRD_GROUPBOARD | BRD_SYMBOLIC)) ||
		    !((state = HasBoardPerm(bptr)) || GROUPOP()) ||
		    TITLE_MATCH(bptr, key)
#if ! HOTBOARDCACHE
		    || (IN_HOTBOARD() && bptr->nuser < 5)
#endif
		    )
		    continue;
		addnewbrdstat(n, state);
	    }
	}
#if ! HOTBOARDCACHE
	if (IN_HOTBOARD())
	    qsort(nbrd, brdnum, sizeof(boardstat_t), cmpboardfriends);
#endif
    } else { /* load boards of a subclass */
	int   childcount = bptr->childcount;
	nbrd = (boardstat_t *) malloc((childcount+2) * sizeof(boardstat_t));
        // 預留兩個以免大量開板時掛調
	for (bid = bptr->firstchild[type]; bid > 0 && 
		brdnum < childcount+2; bid = bptr->next[type]) {
            bptr = getbcache(bid);
	    state = HasBoardPerm(bptr);
	    if ( !(state || GROUPOP()) || TITLE_MATCH(bptr, key) )
		continue;

	    if (bptr->brdattr & BRD_SYMBOLIC) {

		/* Only SYSOP knows a board is symbolic */
		if (HasUserPerm(PERM_SYSOP))
		    state |= NBRD_SYMBOLIC;
		else
		    bid = BRD_LINK_TARGET(bptr);
	    }
	    addnewbrdstat(bid-1, state);
	}
        if(childcount < brdnum) //Ptt: dirty fix fix soon 
                getbcache(class_bid)->childcount = 0;
           
                 
    }
}

static int
search_board(void)
{
    int             num;
    char            genbuf[IDLEN + 2];
    move(0, 0);
    clrtoeol();
    CreateNameList();
    for (num = 0; num < brdnum; num++)
	if (!IS_LISTING_FAV() ||
	    (nbrd[num].myattr & NBRD_BOARD && HasBoardPerm(B_BH(&nbrd[num]))) )
	    AddNameList(B_BH(&nbrd[num])->brdname);
    namecomplete(MSG_SELECT_BOARD, genbuf);
    FreeNameList();
    toplev = NULL;

    for (num = 0; num < brdnum; num++)
	if (!strcasecmp(B_BH(&nbrd[num])->brdname, genbuf))
	    return num;
    return -1;
}

static int
unread_position(char *dirfile, boardstat_t * ptr)
{
    fileheader_t    fh;
    char            fname[FNLEN];
    register int    num, fd, step, total;

    total = B_TOTAL(ptr);
    num = total + 1;
    if ((ptr->myattr & NBRD_UNREAD) && (fd = open(dirfile, O_RDWR)) > 0) {
	if (!brc_initial_board(B_BH(ptr)->brdname)) {
	    num = 1;
	} else {
	    num = total - 1;
	    step = 4;
	    while (num > 0) {
		lseek(fd, (off_t) (num * sizeof(fh)), SEEK_SET);
		if (read(fd, fname, FNLEN) <= 0 ||
		    !brc_unread(fname, brc_num, brc_list))
		    break;
		num -= step;
		if (step < 32)
		    step += step >> 1;
	    }
	    if (num < 0)
		num = 0;
	    while (num < total) {
		lseek(fd, (off_t) (num * sizeof(fh)), SEEK_SET);
		if (read(fd, fname, FNLEN) <= 0 ||
		    brc_unread(fname, brc_num, brc_list))
		    break;
		num++;
	    }
	}
	close(fd);
    }
    if (num < 0)
	num = 0;
    return num;
}

static char
get_fav_type(boardstat_t *ptr)
{
    if (ptr->myattr & NBRD_FOLDER)
	return FAVT_FOLDER;
    else if (ptr->myattr & NBRD_BOARD)
	return FAVT_BOARD;
    else if (ptr->myattr & NBRD_LINE)
	return FAVT_LINE;
    return 0;
}

static void
brdlist_foot(void)
{
    prints(ANSI_COLOR(34;46) "  選擇看板  " ANSI_COLOR(31;47) "  (c)" ANSI_COLOR(30) "新文章模式  "
	   ANSI_COLOR(31) "(v/V)" ANSI_COLOR(30) "標記已讀/未讀  " ANSI_COLOR(31) "(y)" ANSI_COLOR(30) "篩選%s"
	   "  " ANSI_COLOR(31) "(m)" ANSI_COLOR(30) "切換最愛  " ANSI_RESET,
	   IS_LISTING_FAV() ? "最愛" : IS_LISTING_BRD() ? "部份" : "全部");
}


static inline char * 
make_class_color(char *name)
{
    char    *colorset[8] = {"", ANSI_COLOR(32),
	ANSI_COLOR(33), ANSI_COLOR(36), ANSI_COLOR(34), ANSI_COLOR(1),
	ANSI_COLOR(1;32), ANSI_COLOR(1;33)};

    return colorset[(unsigned int)
	(name[0] + name[1] +
	 name[2] + name[3]) & 07];
}

#define HILIGHT_COLOR	ANSI_COLOR(1;36)

static void
show_brdlist(int head, int clsflag, int newflag)
{
    int             myrow = 2;
    if (unlikely(IN_CLASSROOT())) {
	currstat = CLASS;
	myrow = 6;
	showtitle("分類看板", BBSName);
	movie(0);
	move(1, 0);
	outs(
	    "                                                              "
	    "◣  ╭—" ANSI_COLOR(33) "●\n"
	    "                                                    ╬—  " ANSI_RESET " "
	    "◢█" ANSI_COLOR(47) "⊙" ANSI_COLOR(40) "██◣╤\n"
	    "  " ANSI_COLOR(44) "   ︿︿︿︿︿︿︿︿                               "
	    ANSI_COLOR(33) "║" ANSI_RESET ANSI_COLOR(44) " ◣◢███▼▼▼║ " ANSI_RESET "\n"
	    "  " ANSI_COLOR(44) "                                                  "
	    ANSI_COLOR(33) "  " ANSI_RESET ANSI_COLOR(44) " ◤◥███▲▲▲ ║" ANSI_RESET "\n"
	    "                                  ︿︿︿︿︿︿︿︿    " ANSI_COLOR(33)
	    "│" ANSI_RESET "   ◥████◤ ║\n"
	    "                                                      " ANSI_COLOR(33) "╫"
	    "——" ANSI_RESET "  ◤      —＋" ANSI_RESET);
    } else if (clsflag) {
	showtitle("看板列表", BBSName);
	prints("[←]主選單 [→]閱\讀 [↑↓]選擇 [S]排序 [/]搜尋 "
	       "[m]加入或移出我的最愛 [h]求助\n"
	       ANSI_COLOR(7) "%-20s 類別 轉信%-31s人氣 板    主     " ANSI_RESET,
	       newflag ? "總數 未讀 看  板" : "  編號  看  板",
	       "  中   文   敘   述");
	move(b_lines, 0);
	brdlist_foot();
    }
    if (brdnum > 0) {
	boardstat_t    *ptr;
	char    *color[8] = {"", ANSI_COLOR(32),
	    ANSI_COLOR(33), ANSI_COLOR(36), ANSI_COLOR(34), ANSI_COLOR(1),
	ANSI_COLOR(1;32), ANSI_COLOR(1;33)};
 	char    *unread[2] = {ANSI_COLOR(37) "  " ANSI_RESET, ANSI_COLOR(1;31) "ˇ" ANSI_RESET};
 
	if (IS_LISTING_FAV() && get_data_number(get_current_fav()) == 0){
	    // brdnum > 0 ???
	    move(3, 0);
	    outs("        --- 空目錄 ---");
	    return;
	}

	while (++myrow < b_lines) {
	    move(myrow, 0);
	    clrtoeol();
	    if (head < brdnum) {
		ptr = &nbrd[head++];
		if (ptr->myattr & NBRD_LINE){
		    if( !newflag )
			prints("%5d %c %s------------      ------------------------------------------" ANSI_RESET,
				head,
				ptr->myattr & NBRD_TAG ? 'D' : ' ',
				ptr->myattr & NBRD_FAV ? "" : ANSI_COLOR(1;30));
		    else
			prints("        %s------------      ------------------------------------------" ANSI_RESET, ptr->myattr & NBRD_FAV ? "" : ANSI_COLOR(1;30));
		    continue;
		}
		else if (ptr->myattr & NBRD_FOLDER){
		    char *title = get_folder_title(ptr->bid);
		    if( !newflag )
			prints("%5d %c %sMyFavFolder" ANSI_RESET "  目錄 □%-34s" ANSI_RESET,
				head,
				ptr->myattr & NBRD_TAG ? 'D' : ' ',
				!(cuser.uflag2 & FAVNOHILIGHT) ? ANSI_COLOR(1;36) : "",
				title);
		    else
			prints("%6d  %sMyFavFolder" ANSI_RESET "  目錄 □%-34s" ANSI_RESET,
				get_data_number(get_fav_folder(getfolder(ptr->bid))),
				!(cuser.uflag2 & FAVNOHILIGHT) ? ANSI_COLOR(1;36) : "",
				title);
		    continue;
		}

		if (IN_CLASSROOT())
		    outs("          ");
		else {
		    if (!GROUPOP() && !HasBoardPerm(B_BH(ptr))) {
			prints("%5d %c Unknown??    隱板 ？這個板是隱板",
				head, ptr->myattr & NBRD_TAG ? 'D' : ' ');
			continue;
		    }
		}

		if (!newflag) {
		    prints("%5d%c%s", head,
			   !(B_BH(ptr)->brdattr & BRD_HIDE) ? ' ' :
			   (B_BH(ptr)->brdattr & BRD_POSTMASK) ? ')' : '-',
			   (ptr->myattr & NBRD_TAG) ? "D " :
			   (B_BH(ptr)->brdattr & BRD_GROUPBOARD) ? "  " :
			   unread[ptr->myattr & NBRD_UNREAD ? 1 : 0]);
		} else {
		    if (B_BH(ptr)->brdattr & BRD_GROUPBOARD)
			outs("        ");
		    else
			prints("%6d%s", (int)(B_TOTAL(ptr)),
				unread[ptr->myattr & NBRD_UNREAD ? 1 : 0]);
		}
		if (!IN_CLASSROOT()) {
		    prints("%s%-13s" ANSI_RESET "%s%5.5s" ANSI_COLOR(0;37) "%2.2s" ANSI_RESET
			    "%-34.34s",
			    ((!(cuser.uflag2 & FAVNOHILIGHT) &&
			      getboard(ptr->bid) != NULL))? ANSI_COLOR(1;36) : "",
			    B_BH(ptr)->brdname,
			    color[(unsigned int)
			    (B_BH(ptr)->title[1] + B_BH(ptr)->title[2] +
			     B_BH(ptr)->title[3] + B_BH(ptr)->title[0]) & 07],
			    B_BH(ptr)->title, B_BH(ptr)->title + 5, B_BH(ptr)->title + 7);

#ifdef USE_COOLDOWN
		    if (B_BH(ptr)->brdattr & BRD_COOLDOWN)
			outs("靜 ");
		    else if (B_BH(ptr)->brdattr & BRD_BAD)
#else
		    if (B_BH(ptr)->brdattr & BRD_BAD)
#endif
			outs(" X ");

		    else if (B_BH(ptr)->nuser <= 0)
			prints(" %c ", B_BH(ptr)->bvote ? 'V' : ' ');
		    else if (B_BH(ptr)->nuser <= 10)
			prints("%2d ", B_BH(ptr)->nuser);
		    else if (B_BH(ptr)->nuser <= 50)
			prints(ANSI_COLOR(1;33) "%2d" ANSI_RESET " ", B_BH(ptr)->nuser);

		    else if (B_BH(ptr)->nuser >= 5000)
			outs(ANSI_COLOR(1;34) "爆!" ANSI_RESET);
		    else if (B_BH(ptr)->nuser >= 2000)
			outs(ANSI_COLOR(1;31) "爆!" ANSI_RESET);
		    else if (B_BH(ptr)->nuser >= 1000)
			outs(ANSI_COLOR(1) "爆!" ANSI_RESET);
		    else if (B_BH(ptr)->nuser >= 100)
			outs(ANSI_COLOR(1) "HOT" ANSI_RESET);
		    else //if (B_BH(ptr)->nuser > 50)
			prints(ANSI_COLOR(1;31) "%2d" ANSI_RESET " ", B_BH(ptr)->nuser);
		    prints("%.*s" ANSI_CLRTOEND, t_columns - 67, B_BH(ptr)->BM);
		} else {
		    prints("%-40.40s %.*s", B_BH(ptr)->title + 7,
			   t_columns - 67, B_BH(ptr)->BM);
		}
	    }
	    clrtoeol();
	}
    }
}

static void
set_menu_BM(char *BM)
{
    if (!HasUserPerm(PERM_NOCITIZEN) && (HasUserPerm(PERM_ALLBOARD) || is_BM(BM))) {
	currmode |= MODE_GROUPOP;
	cuser.userlevel |= PERM_SYSSUBOP;
    }
}

static void replace_link_by_target(boardstat_t *board)
{
    board->bid = BRD_LINK_TARGET(getbcache(board->bid));
    board->myattr &= ~NBRD_SYMBOLIC;
}
static int
paste_taged_brds(int gid)
{
    fav_t *fav;
    int  bid, tmp;

    if (gid == 0  || ! (HasUserPerm(PERM_SYSOP) || GROUPOP()) ||
        getans("貼上標記的看板?(y/N)")!='y') return 0;
    fav = get_current_fav();
    for (tmp = 0; tmp < fav->DataTail; tmp++) {
	    boardheader_t  *bh;
	    bid = fav_getid(&fav->favh[tmp]);
	    bh = getbcache(bid);
	    if( !is_set_attr(&fav->favh[tmp], FAVH_ADM_TAG))
		continue;
	    set_attr(&fav->favh[tmp], FAVH_ADM_TAG, FALSE);
	    if (bh->gid != gid) {
		bh->gid = gid;
		substitute_record(FN_BOARD, bh,
				  sizeof(boardheader_t), bid);
		reset_board(bid);
		log_usies("SetBoardGID", bh->brdname);
	    }
	}
    sort_bcache();
    return 1;
}

static void
choose_board(int newflag)
{
    static short    num = 0;
    boardstat_t    *ptr;
    int             head = -1, ch = 0, currmodetmp, tmp, tmp1, bidtmp;
    char            keyword[13] = "", buf[64];

    setutmpmode(newflag ? READNEW : READBRD);
    if( get_current_fav() == NULL )
	fav_load();
    ++choose_board_depth;
    brdnum = 0;
    if (!cuser.userlevel)	/* guest yank all boards */
	LIST_GUEST();

    do {
	if (brdnum <= 0) {
	    load_boards(keyword);
	    if (brdnum <= 0 && !IS_LISTING_FAV()) {
		if (keyword[0] != 0) {
		    vmsg("沒有任何看板標題有此關鍵字 "
			    "(板主應注意看板標題命名)");
		    keyword[0] = 0;
		    brdnum = -1;
		    continue;
		}
		if (!IS_LISTING_GUEST()) {
		    brdnum = -1;
		    yank_flag++; /* FAV => BRD, BRD => GUEST */
		    continue;
		}
		if (HasUserPerm(PERM_SYSOP) || GROUPOP()) {
                    if (paste_taged_brds(class_bid) || 
		        m_newbrd(class_bid, 0) == -1)
			break;
		    brdnum = -1;
		    continue;
		} else
		    break;
	    }
	    head = -1;
	}

	/* reset the cursor when out of range */
	if (num < 0)
	    num = 0;
	else if (num >= brdnum)
	    num = brdnum - 1;

	if (head < 0) {
	    if (newflag) {
		tmp = num;
		while (num < brdnum) {
		    ptr = &nbrd[num];
		    if (ptr->myattr & NBRD_UNREAD)
			break;
		    num++;
		}
		if (num >= brdnum)
		    num = tmp;
	    }
	    head = (num / p_lines) * p_lines;
	    show_brdlist(head, 1, newflag);
	} else if (num < head || num >= head + p_lines) {
	    head = (num / p_lines) * p_lines;
	    show_brdlist(head, 0, newflag);
	}
	if (IN_CLASSROOT())
	    ch = cursor_key(7 + num - head, 10);
	else
	    ch = cursor_key(3 + num - head, 0);

	switch (ch) {
	case Ctrl('W'):
	    whereami(0, NULL, NULL);
	    head = -1;
	    break;
	case 'e':
	case KEY_LEFT:
	case EOF:
	    ch = 'q';
	case 'q':
	    if (keyword[0]) {
		keyword[0] = 0;
		brdnum = -1;
		ch = ' ';
	    }
	    break;
	case 'c':
	    show_brdlist(head, 1, newflag ^= 1);
	    break;
	case KEY_PGUP:
	case 'P':
	case 'b':
	case Ctrl('B'):
	    if (num) {
		num -= p_lines;
		break;
	    }
	case KEY_END:
	case '$':
	    num = brdnum - 1;
	    break;
	case ' ':
	case KEY_PGDN:
	case 'N':
	case Ctrl('F'):
	    if (num == brdnum - 1)
		num = 0;
	    else
		num += p_lines;
	    break;
	case Ctrl('I'):
	    t_idle();
	    show_brdlist(head, 1, newflag);
	    break;
	case KEY_UP:
	case 'p':
	case 'k':
	    if (num-- <= 0)
		num = brdnum - 1;
	    break;
	case '*':
	    {
		int i = 0;
		for (i = 0; i < brdnum; i++)
		{
		    ptr = &nbrd[i];
		    if (IS_LISTING_FAV()){
			if(get_fav_type(&nbrd[0]) != 0)
			    fav_tag(ptr->bid, get_fav_type(ptr), 2);
		    }
		    ptr->myattr ^= NBRD_TAG;
		}
		head = 9999;
	    }
	    break;
	case 't':
	    ptr = &nbrd[num];
	    if (IS_LISTING_FAV()){
		if(get_fav_type(&nbrd[0]) != 0)
		    fav_tag(ptr->bid, get_fav_type(ptr), 2);
	    }
	    else{
		/* 站長管理用的 tag */
		if (ptr->myattr & NBRD_TAG)
		    set_attr(getadmtag(ptr->bid), FAVH_ADM_TAG, FALSE);
		else
		    fav_add_admtag(ptr->bid);
	    }
	    ptr->myattr ^= NBRD_TAG;
	    head = 9999;
	case KEY_DOWN:
	case 'n':
	case 'j':
	    if (++num < brdnum)
		break;
	case '0':
	case KEY_HOME:
	    num = 0;
	    break;
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	    if ((tmp = search_num(ch, brdnum)) >= 0)
		num = tmp;
	    brdlist_foot();
	    break;
	case 'F':
	case 'f':
	    if (IN_SUBCLASS() && HasUserPerm(PERM_SYSOP)) {
		getbcache(class_bid)->firstchild[cuser.uflag & BRDSORT_FLAG ? 1 : 0] = 0;
		brdnum = -1;
	    }
	    break;
	case 'h':
	    show_helpfile(fn_boardlisthelp);
	    show_brdlist(head, 1, newflag);
	    break;
	case '/':
	    getdata_buf(b_lines - 1, 0, "請輸入看板中文關鍵字:",
			keyword, sizeof(keyword), DOECHO);
	    brdnum = -1;
	    break;
	case 'S':
	    if(IS_LISTING_FAV()){
		move(b_lines - 2, 0);
		outs("重新排序看板 "
			ANSI_COLOR(1;33) "(注意, 這個動作會覆寫原來設定)" ANSI_RESET " \n");
		tmp = getans("排序方式 (1)按照板名排序 (2)按照類別排序 ==> [0]取消 ");
		if( tmp == '1' )
		    fav_sort_by_name();
		else if( tmp == '2' )
		    fav_sort_by_class();
	    }
	    else
		cuser.uflag ^= BRDSORT_FLAG;
	    brdnum = -1;
	    break;
	case 'y':
	    if (get_current_fav() != NULL || !IS_LISTING_FAV()){
		if (cuser.userlevel)
		    yank_flag ^= 1; /* FAV <=> BRD */
		else
		    yank_flag ^= 2; /* guest, FAV <=> GUEST */
	    }
	    brdnum = -1;
	    break;
	case 'D':
	    if (HasUserPerm(PERM_SYSOP)) {
		ptr = &nbrd[num];
		if (ptr->myattr & NBRD_SYMBOLIC) {
		    if (getans("確定刪除連結？[N/y]") == 'y')
			delete_symbolic_link(getbcache(ptr->bid), ptr->bid);
		}
		brdnum = -1;
	    }
	    break;
	case Ctrl('D'):
	    if (HasUserPerm(PERM_LOGINOK)) {
		fav_remove_all_tagged_item();
		brdnum = -1;
	    }
	    break;
	case Ctrl('A'):
	    if (HasUserPerm(PERM_LOGINOK)) {
		fav_add_all_tagged_item();
		brdnum = -1;
	    }
	    break;
	case Ctrl('T'):
	    if (HasUserPerm(PERM_LOGINOK)) {
		fav_remove_all_tag();
		brdnum = -1;
	    }
	    break;
	case Ctrl('P'):
            if (paste_taged_brds(class_bid))
                brdnum = -1;
            break;
	case 'L':
	    if (HasUserPerm(PERM_SYSOP) && IN_CLASS()) {
		if (make_symbolic_link_interactively(class_bid) < 0)
		    break;
		brdnum = -1;
		head = 9999;
	    }
	    else if (HasUserPerm(PERM_LOGINOK) && IS_LISTING_FAV()) {
		if (fav_add_line() == NULL) {
		    vmsg("新增失敗，分隔線/總最愛 數量達最大值。");
		    break;
		}
		/* done move if it's the first item. */
		if (get_fav_type(&nbrd[0]) != 0)
		    move_in_current_folder(brdnum, num);
		brdnum = -1;
		head = 9999;
	    }
	    break;
/*
	case 'l':
	    if (HasUserPerm(PERM_SYSOP) && (nbrd[num].myattr & NBRD_SYMBOLIC)) {
		replace_link_by_target(&nbrd[num]);
		head = 9999;
	    }
	    break;
*/
	case 'z':
	case 'm':
	    if (HasUserPerm(PERM_LOGINOK)) {
		ptr = &nbrd[num];
		if (IS_LISTING_FAV()) {
		    if (ptr->myattr & NBRD_FAV) {
			if (getans("你確定刪除嗎? [N/y]") != 'y')
			    break;
			fav_remove_item(ptr->bid, get_fav_type(ptr));
			ptr->myattr &= ~NBRD_FAV;
		    }
		}
		else {
		    if (getboard(ptr->bid) != NULL) {
			fav_remove_item(ptr->bid, FAVT_BOARD);
			ptr->myattr &= ~NBRD_FAV;
		    }
		    else {
			if (fav_add_board(ptr->bid) == NULL)
			    vmsg("你的最愛太多了啦 真花心");
			else
			    ptr->myattr |= NBRD_FAV;
		    }
		}
		brdnum = -1;
		head = 9999;
	    }
	    break;
	case 'M':
	    if (HasUserPerm(PERM_LOGINOK)){
		if (IN_FAVORITE() && IS_LISTING_FAV()){
		    imovefav(num);
		    brdnum = -1;
		    head = 9999;
		}
	    }
	    break;
	case 'g':
	    if (HasUserPerm(PERM_LOGINOK) && IS_LISTING_FAV()) {
		fav_type_t  *ft;
		if (fav_stack_full()){
		    vmsg("目錄已達最大層數!!");
		    break;
		}
		if ((ft = fav_add_folder()) == NULL) {
		    vmsg("新增失敗，目錄/總最愛 數量達最大值。");
		    break;
		}
		fav_set_folder_title(ft, "新的目錄");
		/* don't move if it's the first item */
		if (get_fav_type(&nbrd[0]) != 0)
		    move_in_current_folder(brdnum, num);
		brdnum = -1;
    		head = 9999;
	    }
	    break;
	case 'T':
	    if (HasUserPerm(PERM_LOGINOK) && nbrd[num].myattr & NBRD_FOLDER) {
		fav_type_t *ft = getfolder(nbrd[num].bid);
		strlcpy(buf, get_item_title(ft), sizeof(buf));
		getdata_buf(b_lines - 1, 0, "請輸入板名:", buf, 65, DOECHO);
		fav_set_folder_title(ft, buf);
		brdnum = -1;
	    }
	    break;
	case 'K':
	    if (HasUserPerm(PERM_LOGINOK)) {
		char c, fname[80];
		if (get_current_fav() != get_fav_root()) {
		    vmsg("請到我的最愛最上層執行本功\能");
		    break;
		}

		c = getans("請選擇 1)清除不可見看板 2)備份我的最愛 3)取回最愛備份 [Q]");
		if(!c)
		    break;
		if(getans("確定嗎 [y/N] ") != 'y')
		    break;
		switch(c){
		    case '1':
			fav_clean_invisible();
			break;
		    case '2':
			fav_save();
			setuserfile(fname, FAV4);
			sprintf(buf, "%s.bak", fname);
                        Copy(fname, buf);
			break;
		    case '3':
			setuserfile(fname, FAV4);
			sprintf(buf, "%s.bak", fname);
			if (!dashf(buf)){
			    vmsg("你沒有備份你的最愛喔");
			    break;
			}
                        Copy(buf, fname);
			fav_free();
			fav_load();
			break;
		}
		brdnum = -1;
	    }
	    break;
#if 0
	case 'z':
	    if (HasUserPerm(PERM_LOGINOK))
		vmsg("這個功\能已經被我的最愛取代掉了喔!");
	    break;

	case 'Z':
	    if (HasUserPerm(PERM_LOGINOK)) {
		vmsg("為避免誤按此功\能已取消，請改至個人設定區修改設定");
	    break;

	    if (HasUserPerm(PERM_LOGINOK)) {
		char genbuf[64];
		sprintf(genbuf, "確定要 %s訂閱\ 新看板? [N/y] ", cuser.uflag2 & FAVNEW_FLAG ? "取消" : "");
		if (getans(genbuf) != 'y')
		    break;

		cuser.uflag2 ^= FAVNEW_FLAG;
		if (cuser.uflag2 & FAVNEW_FLAG)
		    vmsg("切換為訂閱\新看板模式");
		else
		    vmsg("取消訂閱\新看板");
	    }
	    break;
#endif

	case 'v':
	case 'V':
	    ptr = &nbrd[num];
	    if(nbrd[num].bid < 0 || !HasBoardPerm(B_BH(ptr)))
		break;
	    if (ch == 'v') {
		ptr->myattr &= ~NBRD_UNREAD;
		brc_trunc(ptr->bid, now);
		setbrdtime(ptr->bid, now);
	    } else {
		brc_trunc(ptr->bid, 1);
		setbrdtime(ptr->bid, 1);
		ptr->myattr |= NBRD_UNREAD;
	    }
	    show_brdlist(head, 0, newflag);
	    break;
	case 's':
	    if ((tmp = search_board()) == -1) {
		show_brdlist(head, 1, newflag);
		break;
	    }
	    num = tmp;
	    break;
	case 'E':
	    if (HasUserPerm(PERM_SYSOP | PERM_BOARD) || GROUPOP()) {
		ptr = &nbrd[num];
		move(1, 1);
		clrtobot();
		m_mod_board(B_BH(ptr)->brdname);
		brdnum = -1;
	    }
	    break;
	case 'R':
	    if (HasUserPerm(PERM_SYSOP) || GROUPOP()) {
		m_newbrd(class_bid, 1);
		brdnum = -1;
	    }
	    break;
	case 'B':
	    if (HasUserPerm(PERM_SYSOP) || GROUPOP()) {
		m_newbrd(class_bid, 0);
		brdnum = -1;
	    }
	    break;
	case 'W':
	    if (IN_SUBCLASS() &&
		(HasUserPerm(PERM_SYSOP) || GROUPOP())) {
		setbpath(buf, getbcache(class_bid)->brdname);
		mkdir(buf, 0755);	/* Ptt:開群組目錄 */
		b_note_edit_bname(class_bid);
		brdnum = -1;
	    }
	    break;

	case 'a':
	case 'i':
	    if(IS_LISTING_FAV() && HasUserPerm(PERM_LOGINOK)){
		char         bname[IDLEN + 1];
		int          bid;
		move(0, 0);
		clrtoeol();
		/* use CompleteBoard or CompleteBoardAndGroup ? */
		CompleteBoard(ANSI_COLOR(7) "【 增加我的最愛 】" ANSI_RESET "\n"
			"請輸入欲加入的看板名稱(按空白鍵自動搜尋)：",
			bname);

		if (bname[0] && (bid = getbnum(bname)) &&
			HasBoardPerm(getbcache(bid))) {
		    fav_type_t * ptr = getboard(bid);
		    if (ptr != NULL) { // already in fav list
			// move curser to item
			for (num = 0; bid != nbrd[num].bid; ++num);
		    } else {
			ptr = fav_add_board(bid);

			if (ptr == NULL)
			    vmsg("你的最愛太多了啦 真花心");
			else {
			    ptr->attr |= NBRD_FAV;

			    if (ch == 'i')
				move_in_current_folder(brdnum, num);
			    else
				num = brdnum;
			}
		    }
		}
	    }
	    brdnum = -1;
	    head = 9999;
	    break;

#ifdef DEBUG
	case 'w':
	    brc_finalize();
	    break;
#endif /* defined(DEBUG) */

	case KEY_RIGHT:
	case '\n':
	case '\r':
	case 'r':
	    {
		ptr = &nbrd[num];
		if (IS_LISTING_FAV()) {
		    if (get_fav_type(&nbrd[0]) == 0)
			break;
		    else if (ptr->myattr & NBRD_LINE)
			break;
		    else if (ptr->myattr & NBRD_FOLDER){
			int t = num;
			num = 0;
			fav_folder_in(ptr->bid);
			choose_board(0);
			fav_folder_out();
			num = t;
			brdnum = -1;
			head = 9999;
			break;
		    }
		}
		else if (ptr->myattr & NBRD_SYMBOLIC) {
		    replace_link_by_target(ptr);
		}

		if (!(B_BH(ptr)->brdattr & BRD_GROUPBOARD)) {	/* 非sub class */
		    if (HasBoardPerm(B_BH(ptr))) {
			brc_initial_board(B_BH(ptr)->brdname);

			if (newflag) {
			    setbdir(buf, currboard);
			    tmp = unread_position(buf, ptr);
			    head = tmp - t_lines / 2;
			    getkeep(buf, head > 1 ? head : 1, tmp + 1);
			}
			Read();
			check_newpost(ptr);
			head = -1;
			setutmpmode(newflag ? READNEW : READBRD);
		    }
		} else {	/* sub class */
		    move(12, 1);
		    bidtmp = class_bid;
		    currmodetmp = currmode;
		    tmp1 = num;
		    num = 0;
		    if (!(B_BH(ptr)->brdattr & BRD_TOP))
			class_bid = ptr->bid;
		    else
			class_bid = -1;	/* 熱門群組用 */

		    if (!GROUPOP())	/* 如果還沒有小組長權限 */
			set_menu_BM(B_BH(ptr)->BM);

		    if (now < B_BH(ptr)->bupdate) {
			int mr = 0;

			setbfile(buf, B_BH(ptr)->brdname, fn_notes);
			mr = more(buf, NA);
			if (mr != -1 && mr != READ_NEXT)
			    pressanykey();
		    }
		    tmp = currutmp->brc_id;
		    setutmpbid(ptr->bid);
		    free(nbrd);
		    nbrd = NULL;
	    	    if (IS_LISTING_FAV()) {
			LIST_BRD();
			choose_board(0);
			LIST_FAV();
    		    }
		    else
			choose_board(0);
		    currmode = currmodetmp;	/* 離開板板後就把權限拿掉喔 */
		    num = tmp1;
		    class_bid = bidtmp;
		    setutmpbid(tmp);
		    brdnum = -1;
		}
	    }
	}
    } while (ch != 'q');
    free(nbrd);
    nbrd = NULL;
    --choose_board_depth;
}

int
root_board(void)
{
    init_brdbuf();
    class_bid = 1;
/*    class_bid = 0; */
    LIST_BRD();
    choose_board(0);
    return 0;
}

int
Boards(void)
{
    init_brdbuf();
    class_bid = 0;
    LIST_FAV();
    choose_board(0);
    return 0;
}


int
New(void)
{
    int             mode0 = currutmp->mode;
    int             stat0 = currstat;

    class_bid = 0;
    init_brdbuf();
    choose_board(1);
    currutmp->mode = mode0;
    currstat = stat0;
    return 0;
}
