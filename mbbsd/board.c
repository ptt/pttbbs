/* $Id$ */
#include "bbs.h"

/* personal board state
 * ¬Û¹ï©ó¬ÝªOªº attr (BRD_* in ../include/pttstruct.h),
 * ³o¨Ç¬O¥Î¦b user interface ªº flag */
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
    
    getdata(b_lines - 1, 0, "½Ð¿é¤J·s¦¸§Ç:", buf, sizeof(buf), DOECHO);
    new = atoi(buf) - 1;
    if (new < 0 || brdnum <= new){
	vmsg("¿é¤J½d³ò¦³»~!");
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

    /* ¯¦±K¬ÝªO¡G®Ö¹ï­º®uªO¥Dªº¦n¤Í¦W³æ */

    if (brdattr & BRD_HIDE) {	/* ÁôÂÃ */
	if (hbflcheck((int)(bptr - bcache) + 1, currutmp->uid)) {
	    if (brdattr & BRD_POSTMASK)
		return 0;
	    else
		return 2;
	} else
	    return 1;
    }
    /* ­­¨î¾\ÅªÅv­­ */
    if (level && !(brdattr & BRD_POSTMASK) && !HAS_PERM(level))
	return 0;

    return 1;
}

#if 0
static int
have_author(char *brdname)
{
    char            dirname[100];

    snprintf(dirname, sizeof(dirname),
	     "¥¿¦b·j´M§@ªÌ[33m%s[m ¬ÝªO:[1;33m%s[0m.....",
	     currauthor, brdname);
    move(b_lines, 0);
    clrtoeol();
    outs(dirname);
    refresh();

    setbdir(dirname, brdname);
    str_lower(currowner, currauthor);

    return search_rec(dirname, cmpfowner);
}
#endif

static int
check_newpost(boardstat_t * ptr)
{				/* Ptt §ï */
    int             tbrc_num;
    time_t          ftime;
    time_t         *tbrc_list;

    ptr->myattr &= ~NBRD_UNREAD;
    if (B_BH(ptr)->brdattr & BRD_GROUPBOARD)
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
	bptr = SHM->bsorted[type][n];
	if (bptr->brdname[0] == '\0')
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
    int             type = cuser->uflag & BRDSORT_FLAG ? 1 : 0;
    int             i, n;
    int             state;
    char            byMALLOC = 0, needREALLOC = 0;

    if (class_bid > 0) {
	bptr = &bcache[class_bid - 1];
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
			bptr = &bcache[ fav_getid(&fav->favh[i]) - 1];
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
			bptr = &bcache[ fav_getid(&fav->favh[i]) - 1];
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
	    byMALLOC = 0;
	    needREALLOC = (get_data_number(fav) != brdnum);
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
#ifdef CRITICAL_MEMORY
	    byMALLOC = 1;
#else
	    byMALLOC = 0;
#endif
	    needREALLOC = 1;
	}
#if ! HOTBOARDCACHE
	if (class_bid == -1)
	    qsort(nbrd, brdnum, sizeof(boardstat_t), cmpboardfriends);
#endif
    } else { /* load boards of a subclass */
	int     childcount = bptr->childcount;
	nbrd = (boardstat_t *) malloc(childcount * sizeof(boardstat_t));
	for (bptr = bptr->firstchild[type]; bptr != NULL;
	     bptr = bptr->next[type]) {
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
	byMALLOC = 0;
	needREALLOC = (childcount != brdnum);
    }

    if( needREALLOC ){
	if( byMALLOC ){
	    boardstat_t *newnbrd;
	    newnbrd = (boardstat_t *)malloc(sizeof(boardstat_t) * brdnum);
	    memcpy(newnbrd, nbrd, sizeof(boardstat_t) * brdnum);
	    FREE(nbrd);
	    nbrd = newnbrd;
	}
	else {
	    nbrd = (boardstat_t *)realloc(nbrd, sizeof(boardstat_t) * brdnum);
	}
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
    prints("\033[34;46m  ¿ï¾Ü¬ÝªO  \033[31;47m  (c)\033[30m·s¤å³¹¼Ò¦¡  "
	   "\033[31m(v/V)\033[30m¼Ð°O¤wÅª/¥¼Åª  \033[31m(y)\033[30m¿z¿ï%s"
	   "  \033[31m(m)\033[30m¤Á´«³Ì·R  \033[m",
	   yank_flag == 0 ? "³Ì·R" : yank_flag == 1 ? "³¡¥÷" : "¥þ³¡");
}

static void
show_brdlist(int head, int clsflag, int newflag)
{
    int             myrow = 2;
    if (class_bid == 1) {
	currstat = CLASS;
	myrow = 6;
	showtitle("¤ÀÃþ¬ÝªO", BBSName);
	movie(0);
	move(1, 0);
	outs(
	    "                                                              "
	    "¢©  ¢~¡X\033[33m¡´\n"
	    "                                                    ùá¡X  \033[m "
	    "¢¨¢i\033[47m¡ó\033[40m¢i¢i¢©ùç\n"
	    "  \033[44m   ¡s¡s¡s¡s¡s¡s¡s¡s                               "
	    "\033[33mùø\033[m\033[44m ¢©¢¨¢i¢i¢i¡¿¡¿¡¿ùø \033[m\n"
	    "  \033[44m                                                  "
	    "\033[33m  \033[m\033[44m ¢«¢ª¢i¢i¢i¡¶¡¶¡¶ ùø\033[m\n"
	    "                                  ¡s¡s¡s¡s¡s¡s¡s¡s    \033[33m"
	    "¢x\033[m   ¢ª¢i¢i¢i¢i¢« ùø\n"
	    "                                                      \033[33mùó"
	    "¡X¡X\033[m  ¢«      ¡X¡Ï\033[m");
    } else if (clsflag) {
	showtitle("¬ÝªO¦Cªí", BBSName);
	prints("[¡ö]¥D¿ï³æ [¡÷]¾\\Åª [¡ô¡õ]¿ï¾Ü [y]¸ü¤J [S]±Æ§Ç [/]·j´M "
	       "[TAB]¤åºK¡E¬ÝªO [h]¨D§U\n"
	       "\033[7m%-20s Ãþ§O Âà«H%-31s¤H®ð ªO    ¥D     \033[m",
	       newflag ? "Á`¼Æ ¥¼Åª ¬Ý  ªO" : "  ½s¸¹  ¬Ý  ªO",
	       "  ¤¤   ¤å   ±Ô   ­z");
	move(b_lines, 0);
	brdlist_foot();
    }
    if (brdnum > 0) {
	boardstat_t    *ptr;
	char    *color[8] = {"", "\033[32m",
	    "\033[33m", "\033[36m", "\033[34m", "\033[1m",
	"\033[1;32m", "\033[1;33m"};
	char    *unread[2] = {"\33[37m  \033[m", "\033[1;31m£¾\033[m"};

	if (yank_flag == 0 && get_fav_type(&nbrd[0]) == 0){
	    move(3, 0);
	    prints("        --- ªÅ¥Ø¿ý ---");
	    return;
	}

	while (++myrow < b_lines) {
	    move(myrow, 0);
	    clrtoeol();
	    if (head < brdnum) {
		ptr = &nbrd[head++];
		if (ptr->myattr & NBRD_LINE){
		    if( !newflag )
			prints("%5d %c %s------------      ------------------------------------------\033[m",
				head,
				ptr->myattr & NBRD_TAG ? 'D' : ' ',
				ptr->myattr & NBRD_FAV ? "" : "\033[1;30m");
		    else
			prints("        %s------------      ------------------------------------------\033[m", ptr->myattr & NBRD_FAV ? "" : "\033[1;30m");
		    continue;
		}
		else if (ptr->myattr & NBRD_FOLDER){
		    char *title = get_folder_title(ptr->bid);
		    if( !newflag )
			prints("%5d %c %sMyFavFolder\033[m  ¥Ø¿ý ¡¼%-34s\033[m",
				head,
				ptr->myattr & NBRD_TAG ? 'D' : ' ',
				!(cuser->uflag2 & FAVNOHILIGHT) ? "\033[1;36m" : "",
				title);
		    else
			prints("%6d  %sMyFavFolder\033[m  ¥Ø¿ý ¡¼%-34s\033[m",
				get_data_number(get_fav_folder(getfolder(ptr->bid))),
				!(cuser->uflag2 & FAVNOHILIGHT) ? "\033[1;36m" : "",
				title);
		    continue;
		}

		if (class_bid == 1)
		    prints("          ");
		if (!newflag) {
		    prints("%5d%c%s", head,
			   !(B_BH(ptr)->brdattr & BRD_HIDE) ? ' ' :
			   (B_BH(ptr)->brdattr & BRD_POSTMASK) ? ')' : '-',
			   (ptr->myattr & NBRD_TAG) ? "D " :
			   (B_BH(ptr)->brdattr & BRD_GROUPBOARD) ? "  " :
			   unread[ptr->myattr & NBRD_UNREAD ? 1 : 0]);
		} else {
		    if (B_BH(ptr)->brdattr & BRD_GROUPBOARD)
			prints("        ");
		    else
			prints("%6d%s", (int)(B_TOTAL(ptr)),
				unread[ptr->myattr & NBRD_UNREAD ? 1 : 0]);
		}
		if (class_bid != 1) {
		    if (!GROUPOP() && !HasPerm(B_BH(ptr))) {
			prints("Unknown??    ÁôªO ¡H³o­ÓªO¬OÁôªO");
		    }
		    else {
			prints("%s%-13s\033[m%s%5.5s\033[0;37m%2.2s\033[m"
				"%-34.34s",
				((!(cuser->uflag2 & FAVNOHILIGHT) &&
				  getboard(ptr->bid) != NULL))? "\033[1;36m" : "",
				B_BH(ptr)->brdname,
				color[(unsigned int)
				(B_BH(ptr)->title[1] + B_BH(ptr)->title[2] +
				 B_BH(ptr)->title[3] + B_BH(ptr)->title[0]) & 07],
				B_BH(ptr)->title, B_BH(ptr)->title + 5, B_BH(ptr)->title + 7);

			if (B_BH(ptr)->brdattr & BRD_BAD)
			    prints(" X ");
			else if (B_BH(ptr)->nuser >= 5000)
			    prints("\033[1;34mÃz!\033[m");
			else if (B_BH(ptr)->nuser >= 2000)
			    prints("\033[1;31mÃz!\033[m");
			else if (B_BH(ptr)->nuser >= 1000)
			    prints("\033[1mÃz!\033[m");
			else if (B_BH(ptr)->nuser >= 100)
			    prints("\033[1mHOT\033[m");
			else if (B_BH(ptr)->nuser > 50)
			    prints("\033[1;31m%2d\033[m ", B_BH(ptr)->nuser);
			else if (B_BH(ptr)->nuser > 10)
			    prints("\033[1;33m%2d\033[m ", B_BH(ptr)->nuser);
			else if (B_BH(ptr)->nuser > 0)
			    prints("%2d ", B_BH(ptr)->nuser);
			else
			    prints(" %c ", B_BH(ptr)->bvote ? 'V' : ' ');
			prints("%.*s\033[K", t_columns - 67, B_BH(ptr)->BM);
		    }
		} else {
		    prints("%-40.40s %.*s", B_BH(ptr)->title + 7,
			   t_columns - 67, B_BH(ptr)->BM);
		}
	    }
	    clrtoeol();
	}
    }
}

static char    *choosebrdhelp[] = {
    "\0¬ÝªO¿ï³æ»²§U»¡©ú",
    "\01°ò¥»«ü¥O",
    "(p)(¡ô)/(n)(¡õ)¤W¤@­Ó¬ÝªO / ¤U¤@­Ó¬ÝªO",
    "(P)(^B)(PgUp)  ¤W¤@­¶¬ÝªO",
    "(N)(^F)(PgDn)  ¤U¤@­¶¬ÝªO",
    "($)/(s)/(/)    ³Ì«á¤@­Ó¬ÝªO / ·j´M¬ÝªO / ¥H¤¤¤å·j´M¬ÝªOÃöÁä¦r",
    "(¼Æ¦r)         ¸õ¦Ü¸Ó¶µ¥Ø",
    "\01¶i¶¥«ü¥O",
    "(^W)           °g¸ô¤F §Ú¦b­þ¸Ì",
    "(r)(¡÷)/(q)(¡ö)¶i¤J¦h¥\\¯à¾\\Åª¿ï³æ / ¦^¨ì¥D¿ï³æ",
    "(y/Z)          §Úªº³Ì·R,­q¾\\¬ÝªO,©Ò¦³¬ÝªO / ­q¾\\·s¶}¬ÝªO",
    "(L/g)          ¥[¤J¤À¹j½u / ¥Ø¿ý¦Ü§Úªº³Ì·R",
    "(K/T)          ³Æ¥÷,²M²z§Úªº³Ì·R / ­×§ï§Úªº³Ì·R¥Ø¿ý¦WºÙ",
    "(v/V)          ³q³q¬Ý§¹/¥þ³¡¥¼Åª",
    "(S)            «ö·Ó¦r¥À/¤ÀÃþ±Æ§Ç",
    "(t/^T/^A/^D)   ¼Ð°O¬ÝªO/¨ú®ø©Ò¦³¼Ð°O/ ±N¤w¼Ð°OªÌ¥[¤J/²¾¥X§Úªº³Ì·R",
    "(m/M)          §â¬ÝªO¥[¤J©Î²¾¥X§Úªº³Ì·R,²¾°£¤À¹j½u/·h²¾§Úªº³Ì·R",
    "\01¤p²Õªø«ü¥O",
    "(E/W/B)        ³]©w¬ÝªO/³]©w¤p²Õ³Æ§Ñ/¶}·s¬ÝªO",
    "(^P)           ²¾°Ê¤w¼Ð°O¬ÝªO¨ì¦¹¤ÀÃþ",
    NULL
};


static void
set_menu_BM(char *BM)
{
    if (HAS_PERM(PERM_ALLBOARD) || is_BM(BM)) {
	currmode |= MODE_GROUPOP;
	cuser->userlevel |= PERM_SYSSUBOP;
    }
}

static void replace_link_by_target(boardstat_t *board)
{
    board->bid = BRD_LINK_TARGET(&bcache[board->bid - 1]);
    board->myattr &= ~NBRD_SYMBOLIC;
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
    if (!cuser->userlevel)	/* guest yank all boards */
	yank_flag = 2;

    do {
	if (brdnum <= 0) {
	    load_boards(keyword);
	    if (brdnum <= 0 && yank_flag > 0) {
		if (keyword[0] != 0) {
		    mprints(b_lines - 1, 0, "¨S¦³¥ô¦ó¬ÝªO¼ÐÃD¦³¦¹ÃöÁä¦r "
			    "(ªO¥DÀ³ª`·N¬ÝªO¼ÐÃD©R¦W)");
		    pressanykey();
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
		    if (m_newbrd(0) == -1)
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
		/* ¯¸ªøºÞ²z¥Îªº tag */
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
		bcache[class_bid - 1].firstchild[cuser->uflag & BRDSORT_FLAG ? 1 : 0]
		    = NULL;
		brdnum = -1;
	    }
	    break;
	case 'h':
	    show_help(choosebrdhelp);
	    show_brdlist(head, 1, newflag);
	    break;
	case '/':
	    getdata_buf(b_lines - 1, 0, "½Ð¿é¤J¬ÝªO¤¤¤åÃöÁä¦r:",
			keyword, sizeof(keyword), DOECHO);
	    brdnum = -1;
	    break;
	case 'S':
	    if(yank_flag == 0){
		move(b_lines - 2, 0);
		prints("­«·s±Æ§Ç¬ÝªO "
			"\033[1;33m(ª`·N, ³o­Ó°Ê§@·|ÂÐ¼g­ì¨Ó³]©w)\033[m \n");
		tmp = getans("±Æ§Ç¤è¦¡ (1)«ö·ÓªO¦W±Æ§Ç (2)«ö·ÓÃþ§O±Æ§Ç ==> [0]¨ú®ø ");
		if( tmp == '1' )
		    fav_sort_by_name();
		else if( tmp == '2' )
		    fav_sort_by_class();
	    }
	    else
		cuser->uflag ^= BRDSORT_FLAG;
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
		    if (getans("½T©w§R°£³sµ²¡H[N/y]") == 'y')
			delete_symbolic_link(&bcache[ptr->bid - 1], ptr->bid);
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
	    if (class_bid != 0 &&
		(HAS_PERM(PERM_SYSOP) || GROUPOP())) {
		fav_t *fav = get_current_fav();
		for (tmp = 0; tmp < fav->DataTail; tmp++) {
		    short   bid = fav_getid(&fav->favh[tmp]);
		    boardheader_t  *bh = &bcache[ bid - 1 ];
		    if( !is_set_attr(&fav->favh[tmp], FAVH_ADM_TAG))
			continue;
		    set_attr(&fav->favh[tmp], FAVH_ADM_TAG, 0);
		    if (bh->gid != class_bid) {
			bh->gid = class_bid;
			substitute_record(FN_BOARD, bh,
					  sizeof(boardheader_t), bid);
			reset_board(bid);
			log_usies("SetBoardGID", bh->brdname);
		    }
		}
		brdnum = -1;
	    }
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
		    vmsg("·s¼W¥¢±Ñ¡A¤À¹j½u/Á`³Ì·R ¼Æ¶q¹F³Ì¤j­È¡C");
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
			if (getans("§A½T©w§R°£¶Ü? [N/y]") != 'y')
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
			    vmsg("§Aªº³Ì·R¤Ó¦h¤F°Õ ¯uªá¤ß");
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
		    vmsg("¥Ø¿ý¤w¹F³Ì¤j¼h¼Æ!!");
		    break;
		}
		if ((ft = fav_add_folder()) == NULL) {
		    vmsg("·s¼W¥¢±Ñ¡A¥Ø¿ý/Á`³Ì·R ¼Æ¶q¹F³Ì¤j­È¡C");
		    break;
		}
		fav_set_folder_title(ft, "·sªº¥Ø¿ý");
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
		getdata_buf(b_lines - 1, 0, "½Ð¿é¤JªO¦W:", buf, 65, DOECHO);
		fav_set_folder_title(ft, buf);
		brdnum = -1;
	    }
	    break;
	case 'K':
	    if (HAS_PERM(PERM_LOGINOK)) {
		char c, fname[80];
		if (!current_fav_at_root()) {
		    vmsg("½Ð¨ì§Úªº³Ì·R³Ì¤W¼h°õ¦æ¥»¥\\¯à");
		    break;
		}

		c = getans("½Ð¿ï¾Ü 1)²M°£¤£¥i¨£¬ÝªO 2)³Æ¥÷§Úªº³Ì·R 3)¨ú¦^³Ì·R³Æ¥÷ [Q]");
		if(!c)
		    break;
		if(getans("½T©w¶Ü [y/N] ") != 'y')
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
			    vmsg("§A¨S¦³³Æ¥÷§Aªº³Ì·R³á");
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
		vmsg("¼K¼K ³o­Ó¥\\¯à¤w¸g³Q§Úªº³Ì·R¨ú¥N±¼¤F³á!");
	    break;
	case 'Z':
	    if (HAS_PERM(PERM_LOGINOK)) {
		char genbuf[256];
		sprintf(genbuf, "½T©w­n %s­q¾\\ ·s¬ÝªO? [N/y] ", cuser->uflag2 & FAVNEW_FLAG ? "¨ú®ø" : "");
		if (getans(genbuf) != 'y')
		    break;

		cuser->uflag2 ^= FAVNEW_FLAG;
		if (cuser->uflag2 & FAVNEW_FLAG) {
		    subscribe_newfav();
		    vmsg("¤Á´«¬°­q¾\\·s¬ÝªO¼Ò¦¡");
		}
		else
		    vmsg("¨ú®ø­q¾\\·s¬ÝªO");
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
		setbpath(buf, bcache[class_bid - 1].brdname);
		mkdir(buf, 0755);	/* Ptt:¶}¸s²Õ¥Ø¿ý */
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

		if (!(B_BH(ptr)->brdattr & BRD_GROUPBOARD)) {	/* «Dsub class */
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
			class_bid = -1;	/* ¼öªù¸s²Õ¥Î */

		    if (!GROUPOP())	/* ¦pªGÁÙ¨S¦³¤p²ÕªøÅv­­ */
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
		    currmode = currmodetmp;	/* Â÷¶}ªOªO«á´N§âÅv­­®³±¼³á */
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
    choose_board(1);
    currutmp->mode = mode0;
    currstat = stat0;
    return 0;
}
