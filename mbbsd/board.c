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

static boardstat_t *nbrd = NULL;
static char	choose_board_depth = 0;
static short    brdnum;
static char     yank_flag = 1;

inline int getbid(boardheader_t *fh)
{
    return (fh - bcache);
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
init_brdbuf()
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
HasPerm(boardheader_t * bptr)
{
    register int    level, brdattr;

    level = bptr->level;
    brdattr = bptr->brdattr;

    if (HAS_PERM(PERM_SYSOP))
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
    if (level && !(brdattr & BRD_POSTMASK) && !HAS_PERM(level))
	return 0;

    return 1;
}

static int
check_newpost(boardstat_t * ptr)
{				/* Ptt 改 */
    int             tbrc_num;
    time_t          ftime;
    time_t         *tbrc_list;

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
    if (ftime > now)
	ftime = B_LASTPOSTTIME(ptr) = now - 1;

    tbrc_list = brc_find_record(ptr->bid, &tbrc_num);
    if ( brc_unread_time(ftime, tbrc_num, tbrc_list) )
	ptr->myattr |= NBRD_UNREAD;
    
    return 1;
}

static void
load_uidofgid(const int gid, const int type)
{
    boardheader_t  *bptr, *currbptr;
    int             n, childcount = 0;
    currbptr = &bcache[gid - 1];
    for (n = 0; n < numboards; ++n) {
	if( !(bptr = SHM->bsorted[type][n]) || bptr->brdname[0] == '\0' )
	    continue;
	if (bptr->gid == gid) {
	    if (currbptr == &bcache[gid - 1])
		currbptr->firstchild[type] = bptr;
	    else {
		currbptr->next[type] = bptr;
		currbptr->parent = &bcache[gid - 1];
	    }
	    childcount++;
	    currbptr = bptr;
	}
    }
    bcache[gid - 1].childcount = childcount;
    if (currbptr == &bcache[gid - 1])
	currbptr->firstchild[type] = NULL;
    else
	currbptr->next[type] = NULL;
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
    if (yank_flag != 0)
	ptr->myattr &= ~NBRD_FAV;
    check_newpost(ptr);
    return ptr;
}

static int
cmpboardfriends(const void *brd, const void *tmp)
{
    return ((B_BH((boardstat_t*)tmp)->nuser) -
	    (B_BH((boardstat_t*)brd)->nuser));
}

static void
load_boards(char *key)
{
    boardheader_t  *bptr = NULL;
    int             type = cuser.uflag & BRDSORT_FLAG ? 1 : 0;
    int             i, n;
    int             state;

    if (class_bid > 0) {
	bptr = getbcache(class_bid);
	if (bptr->firstchild[type] == NULL || bptr->childcount <= 0)
	    load_uidofgid(class_bid, type);
    }
    brdnum = 0;
    if (nbrd) {
        free(nbrd);
	nbrd = NULL;
    }
    if (class_bid <= 0) {
	if( yank_flag == 0 ){ // fav mode
	    fav_t *fav = get_current_fav();

	    nbrd = (boardstat_t *)malloc(sizeof(boardstat_t) * get_data_number(fav));
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
			if( HasPerm(bptr) && strcasestr(bptr->title, key))
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
	    if (brdnum == 0)
		addnewbrdstat(0, 0);
	}
#if HOTBOARDCACHE
	else if( class_bid == -1 ){
	    nbrd = (boardstat_t *)malloc(sizeof(boardstat_t) * SHM->nHOTs);
	    for( i = 0 ; i < SHM->nHOTs ; ++i )
		addnewbrdstat(SHM->HBcache[i] - SHM->bcache,
			      HasPerm(SHM->HBcache[i]));
	}
#endif
	else { // general case
	    nbrd = (boardstat_t *) MALLOC(sizeof(boardstat_t) * numboards);
	    for (i = 0; i < numboards; i++) {
		if ((bptr = SHM->bsorted[type][i]) == NULL)
		    continue;
		n = getbid(bptr);
		if (!bptr->brdname[0] ||
		    (bptr->brdattr & (BRD_GROUPBOARD | BRD_SYMBOLIC)) ||
		    !((state = HasPerm(bptr)) || GROUPOP()) ||
		    TITLE_MATCH(bptr, key)
#if ! HOTBOARDCACHE
		    || (class_bid == -1 && bptr->nuser < 5)
#endif
		    )
		    continue;
		addnewbrdstat(n, state);
	    }
	}
#if ! HOTBOARDCACHE
	if (class_bid == -1)
	    qsort(nbrd, brdnum, sizeof(boardstat_t), cmpboardfriends);
#endif
    } else { /* load boards of a subclass */
	int     childcount = bptr->childcount;
	nbrd = (boardstat_t *) malloc((childcount+2) * sizeof(boardstat_t));
        // 預留兩個以免大量開版時掛調
	for (bptr = bptr->firstchild[type]; bptr != NULL && 
             brdnum < childcount+2; bptr = bptr->next[type]) {
	    n = getbid(bptr);
	    state = HasPerm(bptr);
	    if ( !(state || GROUPOP()) || TITLE_MATCH(bptr, key) )
		continue;

	    if (bptr->brdattr & BRD_SYMBOLIC) {

		/* Only SYSOP knows a board is symbolic */
		if (HAS_PERM(PERM_SYSOP))
		    state |= NBRD_SYMBOLIC;
		else
		    n = BRD_LINK_TARGET(bptr) - 1;
	    }
	    addnewbrdstat(n, state);
	}
        if(childcount < brdnum) //Ptt: dirty fix fix soon 
                getbcache(class_bid)->childcount = 0;
           
                 
    }
}

static int
search_board()
{
    int             num;
    char            genbuf[IDLEN + 2];
    move(0, 0);
    clrtoeol();
    CreateNameList();
    for (num = 0; num < brdnum; num++)
	if (yank_flag != 0 ||
	    (nbrd[num].myattr & NBRD_BOARD && HasPerm(B_BH(&nbrd[num]))) )
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
brdlist_foot()
{
    prints("\033[34;46m  選擇看板  \033[31;47m  (c)\033[30m新文章模式  "
	   "\033[31m(v/V)\033[30m標記已讀/未讀  \033[31m(y)\033[30m篩選%s"
	   "  \033[31m(m)\033[30m切換最愛  \033[m",
	   yank_flag == 0 ? "最愛" : yank_flag == 1 ? "部份" : "全部");
}


#define HILIGHT_COLOR	"\033[1;36m"

static void
show_brdlist(int head, int clsflag, int newflag)
{
    int             myrow = 2;
    if (unlikely(class_bid == 1)) {
	currstat = CLASS;
	myrow = 6;
	showtitle("分類看板", BBSName);
	movie(0);
	move(1, 0);
	outs(
	    "                                                              "
	    "◣  ╭—\033[33m●\n"
	    "                                                    ╬—  \033[m "
	    "◢█\033[47m⊙\033[40m██◣╤\n"
	    "  \033[44m   ︿︿︿︿︿︿︿︿                               "
	    "\033[33m║\033[m\033[44m ◣◢███▼▼▼║ \033[m\n"
	    "  \033[44m                                                  "
	    "\033[33m  \033[m\033[44m ◤◥███▲▲▲ ║\033[m\n"
	    "                                  ︿︿︿︿︿︿︿︿    \033[33m"
	    "│\033[m   ◥████◤ ║\n"
	    "                                                      \033[33m╫"
	    "——\033[m  ◤      —＋\033[m");
    } else if (clsflag) {
	showtitle("看板列表", BBSName);
	prints("[←]主選單 [→]閱\讀 [↑↓]選擇 [y]載入 [S]排序 [/]搜尋 "
	       "[TAB]文摘‧看板 [h]求助\n"
	       "\033[7m%-20s 類別 轉信%-31s人氣 板    主     \033[m",
	       newflag ? "總數 未讀 看  板" : "  編號  看  板",
	       "  中   文   敘   述");
	move(b_lines, 0);
	brdlist_foot();
    }
    if (brdnum > 0) {
	boardstat_t    *ptr;
	char    *colorset[8] = {"", "\033[32m",
	    "\033[33m", "\033[36m", "\033[34m", "\033[1m",
	"\033[1;32m", "\033[1;33m"};
	char    *unread[2] = {"\33[37m  \033[m", "\033[1;31mˇ\033[m"};

	char priv, *mark, *favcolor, *brdname, *color, *class, *icon, *desc, *bm;

	if (yank_flag == 0 && get_data_number(get_current_fav()) == 0){
	    // brdnum > 0 ???
	    move(3, 0);
	    outs("        --- 空目錄 ---");
	    return;
	}

	while (++myrow < b_lines) {
	    move(myrow, 0);
	    clrtoeol();
	    if (unlikely(head >= brdnum))
		continue;

	    ptr = &nbrd[head++];

/* board_flag_checking */

	    priv = !(B_BH(ptr)->brdattr & BRD_HIDE) ? ' ' :
		(B_BH(ptr)->brdattr & BRD_POSTMASK) ? ')' : '-';

	    if (newflag) {
		priv = ' ';
		mark = unread[ptr->myattr & NBRD_UNREAD ? 1 : 0];
	    } else {
		mark = (unlikely(ptr->myattr & NBRD_TAG)) ? "D " :
		       (B_BH(ptr)->brdattr & BRD_GROUPBOARD) ? "  " :
		       unread[ptr->myattr & NBRD_UNREAD ? 1 : 0];
	    }

	    /* special case */
	    if (unlikely(class_bid == 1)) {
		prints("          %5d%c%2s%-40.40s ", head, priv, mark, B_BH(ptr)->title + 7);
		bm = B_BH(ptr)->BM;
		goto show_BM;
	    }


/* start_of_board_decoration */

	    if (yank_flag == 0)
		favcolor = !(cuser.uflag2 & FAVNOHILIGHT) ? "\033[1;36m" : "";
	    else
		favcolor = "";

	    color = colorset[(unsigned int)
		(B_BH(ptr)->title[1] + B_BH(ptr)->title[2] +
		 B_BH(ptr)->title[3] + B_BH(ptr)->title[0]) & 07];


/* board_description */

	    if (yank_flag == 0) {

		if (ptr->myattr & NBRD_LINE) {
		    mark = "";
		    priv = ptr->myattr & NBRD_TAG ? 'D' : ' ',
		    favcolor = "";
		    brdname = "------------";
		    class = "    ";
		    icon = "  ";
		    desc = "------------------------------------------";
		    bm = "";
		}
		else if (ptr->myattr & NBRD_FOLDER) {
		    mark = "";
		    priv = ' ';
		    brdname = "MyFavFolder";
		    class = "目錄";
		    icon = "□";
		    desc = get_folder_title(ptr->bid);
		    bm = "";
		}
		else if (!HasPerm(B_BH(ptr))) {
		    mark = "";
		    priv = ' ';
		    brdname = "Unknown??";
		    class = "隱板";
		    icon = "？";
		    desc = "這個板是隱板";
		    bm = "";
		}
		else {
		    goto ugly_normal_case;
		}
	    }
	    else if (unlikely(B_BH(ptr)->brdattr & BRD_GROUPBOARD)) {
		priv = ' ';
		mark = "";
		brdname = B_BH(ptr)->brdname;
		class = B_BH(ptr)->title;
		icon = B_BH(ptr)->title + 5;
		desc = B_BH(ptr)->title + 7;
		bm = B_BH(ptr)->BM;
	    }

	    else {
		/* XXX: non-null terminated string */
ugly_normal_case:
		brdname = B_BH(ptr)->brdname;
		class = B_BH(ptr)->title;
		icon = B_BH(ptr)->title + 5;
		desc = B_BH(ptr)->title + 7;
		bm = B_BH(ptr)->BM;
	    }


	    prints("%5d%c%2s" "%s%-13s\033[m" "%s%4.4s\033[0;37m " "%2.2s\033[m%-34.34s", head, priv, mark,  favcolor, brdname,  color, class, icon, desc); 

	    if (unlikely(B_BH(ptr)->brdattr & BRD_BAD))
		outs(" X ");
	    else if (unlikely(B_BH(ptr)->nuser >= 5000))
		outs("\033[1;34m爆!\033[m");
	    else if (unlikely(B_BH(ptr)->nuser >= 2000))
		outs("\033[1;31m爆!\033[m");
	    else if (unlikely(B_BH(ptr)->nuser >= 1000))
		outs("\033[1m爆!\033[m");
	    else if (unlikely(B_BH(ptr)->nuser >= 100))
		outs("\033[1mHOT\033[m");
	    else if (unlikely(B_BH(ptr)->nuser > 50))
		prints("\033[1;31m%2d\033[m ", B_BH(ptr)->nuser);
	    else if (B_BH(ptr)->nuser > 10)
		prints("\033[1;33m%2d\033[m ", B_BH(ptr)->nuser);
	    else if (B_BH(ptr)->nuser > 0)
		prints("%2d ", B_BH(ptr)->nuser);
	    else
		prints(" %c ", B_BH(ptr)->bvote ? 'V' : ' ');

show_BM:
	    prints("%.*s\033[K", t_columns - 67, bm);

	    clrtoeol();
	}
    }
}

static char    *choosebrdhelp[] = {
    "\0看板選單輔助說明",
    "\01基本指令",
    "(p)(↑)/(n)(↓)上一個看板 / 下一個看板",
    "(P)(^B)(PgUp)  上一頁看板",
    "(N)(^F)(PgDn)  下一頁看板",
    "($)/(s)/(/)    最後一個看板 / 搜尋看板 / 以中文搜尋看板關鍵字",
    "(數字)         跳至該項目",
    "\01進階指令",
    "(^W)           迷路了 我在哪裡",
    "(r)(→)/(q)(←)進入多功\能閱\讀選單 / 回到主選單",
    "(y/Z)          我的最愛,訂閱\看板,所有看板 / 訂閱\新開看板",
    "(L/g)          加入分隔線 / 目錄至我的最愛",
    "(K/T)          備份,清理我的最愛 / 修改我的最愛目錄名稱",
    "(v/V)          通通看完/全部未讀",
    "(S)            按照字母/分類排序",
    "(t/^T/^A/^D)   標記看板/取消所有標記/ 將已標記者加入/移出我的最愛",
    "(m/M)          把看板加入或移出我的最愛,移除分隔線/搬移我的最愛",
    "\01小組長指令",
    "(E/W/B)        設定看板/設定小組備忘/開新看板",
    "(^P)           移動已標記看板到此分類",
    NULL
};


static void
set_menu_BM(char *BM)
{
    if (HAS_PERM(PERM_ALLBOARD) || is_BM(BM)) {
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

    if (gid == 0  || ! (HAS_PERM(PERM_SYSOP) || GROUPOP()) ||
        getans("貼上標記的看板?(y/N)")=='n') return 0;
    fav = get_current_fav();
    for (tmp = 0; tmp < fav->DataTail; tmp++) {
	    boardheader_t  *bh;
	    bid = fav_getid(&fav->favh[tmp]);
	    bh = getbcache(bid);
	    if( !is_set_attr(&fav->favh[tmp], FAVH_ADM_TAG))
		continue;
	    set_attr(&fav->favh[tmp], FAVH_ADM_TAG, 0);
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
	yank_flag = 2;

    do {
	if (brdnum <= 0) {
	    load_boards(keyword);
	    if (brdnum <= 0 && yank_flag > 0) {
		if (keyword[0] != 0) {
		    vmsg("沒有任何看板標題有此關鍵字 "
			    "(板主應注意看板標題命名)");
		    keyword[0] = 0;
		    brdnum = -1;
		    continue;
		}
		if (yank_flag < 2) {
		    brdnum = -1;
		    yank_flag++;
		    continue;
		}
		if (HAS_PERM(PERM_SYSOP) || GROUPOP()) {
                    if (paste_taged_brds(class_bid) || 
		        m_newbrd(0) == -1)
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
	if (class_bid == 1)
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
	case 't':
	    ptr = &nbrd[num];
	    if (yank_flag == 0 && get_fav_type(&nbrd[0]) != 0) {
		fav_tag(ptr->bid, get_fav_type(ptr), 2);
	    }
	    else if (yank_flag != 0) {
		/* 站長管理用的 tag */
		if (ptr->myattr & NBRD_TAG)
		    set_attr(getadmtag(ptr->bid), FAVH_ADM_TAG, 0);
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
	    if (class_bid>0 && HAS_PERM(PERM_SYSOP)) {
		getbcache(class_bid)->firstchild[cuser.uflag & BRDSORT_FLAG ? 1 : 0]
		    = NULL;
		brdnum = -1;
	    }
	    break;
	case 'h':
	    show_help(choosebrdhelp);
	    show_brdlist(head, 1, newflag);
	    break;
	case '/':
	    getdata_buf(b_lines - 1, 0, "請輸入看板中文關鍵字:",
			keyword, sizeof(keyword), DOECHO);
	    brdnum = -1;
	    break;
	case 'S':
	    if(yank_flag == 0){
		move(b_lines - 2, 0);
		prints("重新排序看板 "
			"\033[1;33m(注意, 這個動作會覆寫原來設定)\033[m \n");
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
	    if (get_current_fav() != NULL || yank_flag != 0)
		yank_flag = (yank_flag + 1) % 2;
	    brdnum = -1;
	    break;
	case 'D':
	    if (HAS_PERM(PERM_SYSOP)) {
		ptr = &nbrd[num];
		if (ptr->myattr & NBRD_SYMBOLIC) {
		    if (getans("確定刪除連結？[N/y]") == 'y')
			delete_symbolic_link(getbcache(ptr->bid), ptr->bid);
		}
		brdnum = -1;
	    }
	    break;
	case Ctrl('D'):
	    if (HAS_PERM(PERM_LOGINOK)) {
		fav_remove_all_tagged_item();
		brdnum = -1;
	    }
	    break;
	case Ctrl('A'):
	    if (HAS_PERM(PERM_LOGINOK)) {
		fav_add_all_tagged_item();
		brdnum = -1;
	    }
	    break;
	case Ctrl('T'):
	    if (HAS_PERM(PERM_LOGINOK)) {
		fav_remove_all_tag();
		brdnum = -1;
	    }
	    break;
	case Ctrl('P'):
            if (paste_taged_brds(class_bid))
                brdnum = -1;
            break;
	case 'L':
	    if (HAS_PERM(PERM_SYSOP) && class_bid > 0) {
		if (make_symbolic_link_interactively(class_bid) < 0)
		    break;
		brdnum = -1;
		head = 9999;
	    }
	    else if (HAS_PERM(PERM_LOGINOK) && yank_flag == 0) {
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
	    if (HAS_PERM(PERM_SYSOP) && (nbrd[num].myattr & NBRD_SYMBOLIC)) {
		replace_link_by_target(&nbrd[num]);
		head = 9999;
	    }
	    break;
*/
	case 'm':
	    if (HAS_PERM(PERM_LOGINOK)) {
		ptr = &nbrd[num];
		if (yank_flag == 0) {
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
	    if (HAS_PERM(PERM_LOGINOK)){
		if (class_bid == 0 && yank_flag == 0){
		    imovefav(num);
		    brdnum = -1;
		    head = 9999;
		}
	    }
	    break;
	case 'g':
	    if (HAS_PERM(PERM_LOGINOK) && yank_flag == 0) {
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
	    if (HAS_PERM(PERM_LOGINOK) && nbrd[num].myattr & NBRD_FOLDER) {
		fav_type_t *ft = getfolder(nbrd[num].bid);
		strlcpy(buf, get_item_title(ft), sizeof(buf));
		getdata_buf(b_lines - 1, 0, "請輸入板名:", buf, 65, DOECHO);
		fav_set_folder_title(ft, buf);
		brdnum = -1;
	    }
	    break;
	case 'K':
	    if (HAS_PERM(PERM_LOGINOK)) {
		char c, fname[80];
		if (!current_fav_at_root()) {
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
	case 'z':
	    if (HAS_PERM(PERM_LOGINOK))
		vmsg("嘿嘿 這個功\能已經被我的最愛取代掉了喔!");
	    break;
	case 'Z':
	    if (HAS_PERM(PERM_LOGINOK)) {
		char genbuf[256];
		sprintf(genbuf, "確定要 %s訂閱\ 新看板? [N/y] ", cuser.uflag2 & FAVNEW_FLAG ? "取消" : "");
		if (getans(genbuf) != 'y')
		    break;

		cuser.uflag2 ^= FAVNEW_FLAG;
		if (cuser.uflag2 & FAVNEW_FLAG) {
		    subscribe_newfav();
		    vmsg("切換為訂閱\新看板模式");
		}
		else
		    vmsg("取消訂閱\新看板");
	    }
	    break;

	case 'v':
	case 'V':
	    ptr = &nbrd[num];
	    if(nbrd[num].bid < 0 || !HasPerm(B_BH(ptr)))
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
	    if (HAS_PERM(PERM_SYSOP) || GROUPOP()) {
		ptr = &nbrd[num];
		move(1, 1);
		clrtobot();
		m_mod_board(B_BH(ptr)->brdname);
		brdnum = -1;
	    }
	    break;
	case 'R':
	    if (HAS_PERM(PERM_SYSOP) || GROUPOP()) {
		m_newbrd(1);
		brdnum = -1;
	    }
	    break;
	case 'B':
	    if (HAS_PERM(PERM_SYSOP) || GROUPOP()) {
		m_newbrd(0);
		brdnum = -1;
	    }
	    break;
	case 'W':
	    if (class_bid > 0 &&
		(HAS_PERM(PERM_SYSOP) || GROUPOP())) {
		setbpath(buf, getbcache(class_bid)->brdname);
		mkdir(buf, 0755);	/* Ptt:開群組目錄 */
		b_note_edit_bname(class_bid);
		brdnum = -1;
	    }
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
		if (yank_flag == 0) {
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
		    if (HasPerm(B_BH(ptr))) {
			brc_initial_board(B_BH(ptr)->brdname);

			if (newflag) {
			    setbdir(buf, currboard);
			    tmp = unread_position(buf, ptr);
			    head = tmp - t_lines / 2;
			    getkeep(buf, head > 1 ? head : 1, tmp + 1);
			}
			board_visit_time = getbrdtime(ptr->bid);
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
			setbfile(buf, B_BH(ptr)->brdname, fn_notes);
			if (more(buf, NA) != -1)
			    pressanykey();
		    }
		    tmp = currutmp->brc_id;
		    setutmpbid(ptr->bid);
		    free(nbrd);
		    nbrd = NULL;
	    	    if (yank_flag == 0 ) {
    			yank_flag = 1;
			choose_board(0);
			yank_flag = 0;
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
root_board()
{
    init_brdbuf();
    class_bid = 1;
    yank_flag = 1;
    choose_board(0);
    return 0;
}

int
Boards()
{
    init_brdbuf();
    class_bid = 0;
    yank_flag = 0;
    choose_board(0);
    return 0;
}


int
New()
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
