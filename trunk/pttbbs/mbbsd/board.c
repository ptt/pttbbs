/* $Id: board.c,v 1.85 2003/03/15 12:06:39 in2 Exp $ */
#include "bbs.h"
#define BRC_STRLEN 15		/* Length of board name */
#define BRC_MAXSIZE     24576
#define BRC_ITEMSIZE    (BRC_STRLEN + 1 + BRC_MAXNUM * sizeof( int ))
#define BRC_MAXNUM      80

static char    *
brc_getrecord(char *ptr, char *name, int *pnum, int *list)
{
    int             num;
    char           *tmp;

    strncpy(name, ptr, BRC_STRLEN);
    ptr += BRC_STRLEN;
    num = (*ptr++) & 0xff;
    tmp = ptr + num * sizeof(int);
    if (num > BRC_MAXNUM)
	num = BRC_MAXNUM;
    *pnum = num;
    memcpy(list, ptr, num * sizeof(int));
    return tmp;
}

static time_t   brc_expire_time;

static char    *
brc_putrecord(char *ptr, char *name, int num, int *list)
{
    if (num > 0 && list[0] > brc_expire_time) {
	if (num > BRC_MAXNUM)
	    num = BRC_MAXNUM;

	while (num > 1 && list[num - 1] < brc_expire_time)
	    num--;

	strncpy(ptr, name, BRC_STRLEN);
	ptr += BRC_STRLEN;
	*ptr++ = num;
	memcpy(ptr, list, num * sizeof(int));
	ptr += num * sizeof(int);
    }
    return ptr;
}

static int      brc_changed = 0;
static char     brc_buf[BRC_MAXSIZE];
static char     brc_name[BRC_STRLEN];
static char    *fn_boardrc = ".boardrc";
static int      brc_size;
char *brc_buf_addr=brc_buf;

void
brc_update()
{
    if (brc_changed && cuser.userlevel) {
	char            dirfile[STRLEN], *ptr;
	char            tmp_buf[BRC_MAXSIZE - BRC_ITEMSIZE], *tmp;
	char            tmp_name[BRC_STRLEN];
	int             tmp_list[BRC_MAXNUM], tmp_num;
	int             fd, tmp_size;

	ptr = brc_buf;
	if (brc_num > 0)
	    ptr = brc_putrecord(ptr, brc_name, brc_num, brc_list);

	setuserfile(dirfile, fn_boardrc);
	if ((fd = open(dirfile, O_RDONLY)) != -1) {
	    tmp_size = read(fd, tmp_buf, sizeof(tmp_buf));
	    close(fd);
	} else {
	    tmp_size = 0;
	}

	tmp = tmp_buf;
	while (tmp < &tmp_buf[tmp_size] && (*tmp >= ' ' && *tmp <= 'z')) {
	    tmp = brc_getrecord(tmp, tmp_name, &tmp_num, tmp_list);
	    if (strncmp(tmp_name, currboard, BRC_STRLEN))
		ptr = brc_putrecord(ptr, tmp_name, tmp_num, tmp_list);
	}
	brc_size = (int)(ptr - brc_buf);

	if ((fd = open(dirfile, O_WRONLY | O_CREAT, 0644)) != -1) {
	    ftruncate(fd, 0);
	    write(fd, brc_buf, brc_size);
	    close(fd);
	}
	brc_changed = 0;
    }
}

static void
read_brc_buf()
{
    char            dirfile[STRLEN];
    int             fd;

    if (brc_buf[0] == '\0') {
	setuserfile(dirfile, fn_boardrc);
	if ((fd = open(dirfile, O_RDONLY)) != -1) {
	    brc_size = read(fd, brc_buf, sizeof(brc_buf));
	    close(fd);
	} else {
	    brc_size = 0;
	}
    }
}

int
brc_initial(char *boardname)
{
    char           *ptr;
    if (strcmp(currboard, boardname) == 0) {
	return brc_num;
    }
    brc_update();
    strlcpy(currboard, boardname, sizeof(currboard));
    currbid = getbnum(currboard);
    currbrdattr = bcache[currbid - 1].brdattr;
    read_brc_buf();

    ptr = brc_buf;
    while (ptr < &brc_buf[brc_size] && (*ptr >= ' ' && *ptr <= 'z')) {
	ptr = brc_getrecord(ptr, brc_name, &brc_num, brc_list);
	if (strncmp(brc_name, currboard, BRC_STRLEN) == 0)
	    return brc_num;
    }
    strncpy(brc_name, boardname, BRC_STRLEN);
    brc_num = brc_list[0] = 1;
    return 0;
}

void
brc_addlist(char *fname)
{
    int             ftime, n, i;

    if (!cuser.userlevel)
	return;

    ftime = atoi(&fname[2]);
    if (ftime <= brc_expire_time
	 /* || fname[0] != 'M' || fname[1] != '.' */ ) {
	return;
    }
    if (brc_num <= 0) {
	brc_list[brc_num++] = ftime;
	brc_changed = 1;
	return;
    }
    if ((brc_num == 1) && (ftime < brc_list[0]))
	return;
    for (n = 0; n < brc_num; n++) {
	if (ftime == brc_list[n]) {
	    return;
	} else if (ftime > brc_list[n]) {
	    if (brc_num < BRC_MAXNUM)
		brc_num++;
	    for (i = brc_num - 1; --i >= n; brc_list[i + 1] = brc_list[i]);
	    brc_list[n] = ftime;
	    brc_changed = 1;
	    return;
	}
    }
    if (brc_num < BRC_MAXNUM) {
	brc_list[brc_num++] = ftime;
	brc_changed = 1;
    }
}

static int
brc_unread_time(time_t ftime, int bnum, int *blist)
{
    int             n;

    if (ftime <= brc_expire_time)
	return 0;

    if (brc_num <= 0)
	return 1;
    for (n = 0; n < bnum; n++) {
	if (ftime > blist[n])
	    return 1;
	else if (ftime == blist[n])
	    return 0;
    }
    return 0;
}

int
brc_unread(char *fname, int bnum, int *blist)
{
    int             ftime, n;

    ftime = atoi(&fname[2]);

    if (ftime <= brc_expire_time)
	return 0;

    if (brc_num <= 0)
	return 1;
    for (n = 0; n < bnum; n++) {
	if (ftime > blist[n])
	    return 1;
	else if (ftime == blist[n])
	    return 0;
    }
    return 0;
}

#define BRD_UNREAD 1
#define BRD_FAV  2
#define BRD_ZAP  4
#define BRD_TAG  8

#define B_TOTAL(bptr)        (SHM->total[(bptr)->bid - 1])
#define B_LASTPOSTTIME(bptr) (SHM->lastposttime[(bptr)->bid - 1])
#define B_BH(bptr)           (&bcache[(bptr)->bid - 1])
typedef struct {
    int             bid;
    unsigned char   myattr;
} __attribute__ ((packed)) boardstat_t;

static int     *zapbuf = NULL;
static char    *favbuf = NULL;
static boardstat_t *nbrd = NULL;
char   zapchange = 0, favchange = 0, choose_board_depth = 0;

#define STR_BBSRC ".bbsrc"
#define STR_FAV   ".fav"
#define STR_FAV2  ".fav2"

#ifdef CRITICAL_MEMORY
void *MALLOC(int size)
{
    int     *p;
    p = (int *)mmap(NULL, (size + 4), PROT_READ | PROT_WRITE, MAP_ANON, -1, 0);
    p[0] = size;
#ifdef DEBUG
    vmsg("critical malloc %d bytes", size);
#endif
    return (void *)&p[1];
}

void FREE(void *ptr)
{
    int     size = ((int *)ptr)[-1];
    munmap((void *)(&(((int *)ptr)[-1])), size);
#ifdef DEBUG
    vmsg("critical free %d bytes", size);
#endif
}
#endif

void load_brdbuf(void)
{
    static  char    firsttime = 1;
    int     fd, size, favsize, i;
    char    fname[80];

    size = (numboards + 32) * sizeof(int);
    favsize = (numboards + 32) * sizeof(char);
#ifdef MEM_CHECK
    size += sizeof(int);
    favsize += sizeof(int);
#endif

    zapbuf = (int *)malloc(size);
    favbuf = (char *)malloc(favsize);

#ifdef MEM_CHECK
    zapbuf[0] = MEM_CHECK;
    zapbuf = &zapbuf[1];
    ((int *)favbuf)[0] = MEM_CHECK;
    favbuf = &favbuf[ sizeof(int) ];
    size -= sizeof(int);
    favsize -= sizeof(int);
#endif
    zapchange = favchange = 0;

    if( firsttime ){
	memset(favbuf, 0, favsize);
	for( i = (numboards + 32) - 1 ; i >= 0 ; --i )
	    zapbuf[i] = login_start_time;
    }

    setuserfile(fname, STR_BBSRC);
    if ((fd = open(fname, O_RDONLY, 0600)) != -1) {
	read(fd, zapbuf, size);
	close(fd);
    }
    setuserfile(fname, STR_FAV2);
    if ((fd = open(fname, O_RDONLY, 0600)) != -1) {
	read(fd, favbuf, favsize);
	close(fd);
    }
    else{
	// translate old fav format(int array) to new format(char array)
	setuserfile(fname, STR_FAV);
	if( (fd = open(fname, O_RDONLY, 0600)) != -1 ){
	    int     *oldfav = (int *)malloc(size);
	    read(fd, oldfav, size);
	    close(fd);

	    for( i = (numboards + 32) - 1 ; i >= 0 ; --i )
		favbuf[i] = oldfav[i];
	    free(oldfav);

	    setuserfile(fname, STR_FAV2);
	    if( (fd = open(fname, O_WRONLY | O_CREAT, 0600)) != -1 ){
		size = numboards * sizeof(char);
		write(fd, favbuf, size);
		close(fd);

		// remove old favorite file
		setuserfile(fname, STR_FAV);
		unlink(fname);
	    }
	    else
		favchange = 1;
	}
    }

    if( firsttime ){
	for (i = 0; i < numboards; i++)
	    favbuf[i] &= ~BRD_TAG;
    }
    firsttime = 0;
}

void
init_brdbuf()
{
    brc_expire_time = login_start_time - 365 * 86400;
}

void
save_brdbuf(void)
{
    int             fd, size;
    char            fname[60];
    static char     reentrant = 0;

    if( reentrant )
	return;
    reentrant = 1;

    size = numboards * sizeof(int);
    setuserfile(fname, STR_BBSRC);
    if ( zapbuf != NULL ){
	if( ( 
#ifdef MEM_CHECK
	     (unsigned)zapbuf[-1] == (unsigned)MEM_CHECK &&
#endif
	     zapchange &&
	     (fd = open(fname, O_WRONLY | O_CREAT, 0600)) != -1) ){
	    write(fd, zapbuf, size);
	    close(fd);
	}
#ifdef MEM_CHECK
	free(&zapbuf[-1]);
#else
	free(zapbuf);
#endif
	zapbuf = NULL;
    }

    setuserfile(fname, STR_FAV2);
    if ( favbuf != NULL ){
	if( (
#ifdef MEM_CHECK
	     (unsigned)((int*)favbuf)[-1] == (unsigned)MEM_CHECK &&
#endif
	     favchange &&
	     (fd = open(fname, O_WRONLY | O_CREAT, 0600)) != -1) ){
	    size = numboards * sizeof(char);
	    write(fd, favbuf, size);
	    close(fd);
	}
#ifdef MEM_CHECK
	free(&favbuf[-sizeof(int)]);
#else
	free(favbuf);
#endif
	favbuf = NULL;
    }
    reentrant = 0;
}

int
Ben_Perm(boardheader_t * bptr)
{
    register int    level, brdattr;
    register char  *ptr;

    level = bptr->level;
    brdattr = bptr->brdattr;

    if (HAS_PERM(PERM_SYSOP))
	return 1;

    ptr = bptr->BM;
    if (is_BM(ptr))
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
    int             tbrc_list[BRC_MAXNUM], tbrc_num;
    char            bname[BRC_STRLEN];
    char           *po;
    time_t          ftime;

    ptr->myattr &= ~BRD_UNREAD;
    if (B_BH(ptr)->brdattr & BRD_GROUPBOARD)
	return 0;

    if (B_TOTAL(ptr) == 0)
	setbtotal(ptr->bid);
    if (B_TOTAL(ptr) == 0)
	return 0;
    ftime = B_LASTPOSTTIME(ptr);
    read_brc_buf();
    po = brc_buf;
    while (po < &brc_buf[brc_size] && (*po >= ' ' && *po <= 'z')) {
	po = brc_getrecord(po, bname, &tbrc_num, tbrc_list);
	if (strncmp(bname, B_BH(ptr)->brdname, BRC_STRLEN) == 0) {
	    if (brc_unread_time(ftime, tbrc_num, tbrc_list)) {
		ptr->myattr |= BRD_UNREAD;
	    }
	    return 1;
	}
    }

    ptr->myattr |= BRD_UNREAD;
    return 1;
}

static short    brdnum;
static char     yank_flag = 1;
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
	currbptr->firstchild[type] = (boardheader_t *) ~ 0;
    else
	currbptr->next[type] = (boardheader_t *) ~ 0;
}
static boardstat_t *
addnewbrdstat(int n, int state)
{
    boardstat_t    *ptr = &nbrd[brdnum++];
    //boardheader_t  *bptr = &bcache[n];
    //ptr->total = &(SHM->total[n]);
    //ptr->lastposttime = &(SHM->lastposttime[n]);
    ptr->bid = n + 1;
    ptr->myattr = 0;
    ptr->myattr = (favbuf[n] & ~BRD_ZAP);
    if (zapbuf[n] == 0)
	ptr->myattr |= BRD_ZAP;
    //ptr->bh = bptr;
    if ((B_BH(ptr)->brdattr & BRD_HIDE) && state == 1)
	B_BH(ptr)->brdattr |= BRD_POSTMASK;
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
    register int    i, n;
    register int    state;

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
	nbrd = (boardstat_t *) malloc(sizeof(boardstat_t) * numboards);
	for (i = 0; i < numboards; i++) {
	    if ((bptr = SHM->bsorted[type][i]) == NULL)
		continue;
	    n = (int)(bptr - bcache);
	    if (!bptr->brdname[0] || (yank_flag != 0 && bptr->brdattr & BRD_GROUPBOARD) ||
		!((state = Ben_Perm(bptr)) || (currmode & MODE_MENU)) ||
		(yank_flag == 0 && !(favbuf[n] & BRD_FAV)) ||
		(yank_flag == 1 && !zapbuf[n]) ||
		(key[0] && !strcasestr(bptr->title, key)) ||
		(class_bid == -1 && bptr->nuser < 5))
		continue;
	    addnewbrdstat(n, state);
	}
	if (class_bid == -1)
	    qsort(nbrd, brdnum, sizeof(boardstat_t), cmpboardfriends);
	nbrd = realloc(nbrd, sizeof(boardstat_t) * brdnum);
    } else {
	nbrd = (boardstat_t *) malloc(bptr->childcount * sizeof(boardstat_t));
	for (bptr = bptr->firstchild[type]; bptr != (boardheader_t *) ~ 0;
	     bptr = bptr->next[type]) {
	    n = (int)(bptr - bcache);
	    if (!((state = Ben_Perm(bptr)) || (currmode & MODE_MENU))
		|| (yank_flag == 0 && !(favbuf[n] & BRD_FAV))
		|| (yank_flag == 1 && !zapbuf[n]) ||
		(key[0] && !strcasestr(bptr->title, key)))
		continue;
	    addnewbrdstat(n, state);
	}
	nbrd = realloc(nbrd, sizeof(boardstat_t) * brdnum);
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
    if ((ptr->myattr & BRD_UNREAD) && (fd = open(dirfile, O_RDWR)) > 0) {
	if (!brc_initial(B_BH(ptr)->brdname)) {
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

static void
brdlist_foot()
{
    prints("\033[34;46m  ¿ï¾Ü¬ÝªO  \033[31;47m  (c)\033[30m·s¤å³¹¼Ò¦¡  "
	   "\033[31m(v/V)\033[30m¼Ð°O¤wÅª/¥¼Åª  \033[31m(y)\033[30m¿z¿ï%s"
	   "  \033[31m(z)\033[30m¤Á´«¿ï¾Ü  \033[m",
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

	while (++myrow < b_lines) {
	    move(myrow, 0);
	    clrtoeol();
	    if (head < brdnum) {
		ptr = &nbrd[head++];
		if (class_bid == 1)
		    prints("          ");
		if (!newflag) {
		    prints("%5d%c%s", head,
			   !(B_BH(ptr)->brdattr & BRD_HIDE) ? ' ' :
			   (B_BH(ptr)->brdattr & BRD_POSTMASK) ? ')' : '-',
			   (ptr->myattr & BRD_TAG) ? "D " :
			   (ptr->myattr & BRD_ZAP) ? "- " :
			   (B_BH(ptr)->brdattr & BRD_GROUPBOARD) ? "  " :
			   unread[ptr->myattr & BRD_UNREAD]);
		} else if (ptr->myattr & BRD_ZAP) {
		    ptr->myattr &= ~BRD_UNREAD;
		    prints("   ¡ß ¡ß");
		} else {
		    if (newflag) {
			if ((B_BH(ptr)->brdattr & BRD_GROUPBOARD))
			    prints("        ");
			else
			    prints("%6d%s", (int)(B_TOTAL(ptr)),
				   unread[ptr->myattr & BRD_UNREAD]);
		    }
		}
		if (class_bid != 1) {
		    prints("%s%-13s\033[m%s%5.5s\033[0;37m%2.2s\033[m"
			   "%-34.34s",
			   (ptr->myattr & BRD_FAV) ? "\033[1;36m" : "",
			   B_BH(ptr)->brdname,
			   color[(unsigned int)
				 (B_BH(ptr)->title[1] + B_BH(ptr)->title[2] +
				  B_BH(ptr)->title[3] + B_BH(ptr)->title[0]) & 07],
			   B_BH(ptr)->title, B_BH(ptr)->title + 5, B_BH(ptr)->title + 7);

		    if (B_BH(ptr)->brdattr & BRD_BAD)
			prints(" X ");
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
    "(r)(¡÷)(Rtn)   ¶i¤J¦h¥\\¯à¾\\Åª¿ï³æ",
    "(q)(¡ö)        ¦^¨ì¥D¿ï³æ",
    "(z)(Z)         ­q¾\\/¤Ï­q¾\\¬ÝªO ­q¾\\/¤Ï­q¾\\©Ò¦³¬ÝªO",
    "(y)            §Úªº³Ì·R/­q¾\\¬ÝªO/¥X©Ò¦³¬ÝªO",
    "(v/V)          ³q³q¬Ý§¹/¥þ³¡¥¼Åª",
    "(S)            «ö·Ó¦r¥À/¤ÀÃþ±Æ§Ç",
    "(t/^T/^A/^D)   ¼Ð°O¬ÝªO/¨ú®ø©Ò¦³¼Ð°O/ ±N¤w¼Ð°OªÌ¥[¤J/²¾¥X§Úªº³Ì·R",
    "(m)            §â¬ÝªO¥[¤J©Î²¾¥X§Úªº³Ì·R",
    "\01¤p²Õªø«ü¥O",
    "(E/W/B)        ³]©w¬ÝªO/³]©w¤p²Õ³Æ§Ñ/¶}·s¬ÝªO",
    "(^P)           ²¾°Ê¤w¼Ð°O¬ÝªO¨ì¦¹¤ÀÃþ",
    NULL
};


static void
set_menu_BM(char *BM)
{
    if (HAS_PERM(PERM_ALLBOARD) || is_BM(BM)) {
	currmode |= MODE_MENU;
	cuser.userlevel |= PERM_SYSSUBOP;
    }
}

static char    *privateboard =
"\n\n\n\n         ¹ï¤£°_ ¦¹ªO¥Ø«e¥u­ã¬ÝªO¦n¤Í¶i¤J  ½Ð¥ý¦VªO¥D¥Ó½Ð¤J¹Ò³\\¥i";
static void
dozap(int num)
{
    boardstat_t    *ptr;
    ptr = &nbrd[num];
    ptr->myattr ^= BRD_ZAP;
    if (B_BH(ptr)->brdattr & BRD_NOZAP)
	ptr->myattr &= ~BRD_ZAP;
    if (!(ptr->myattr & BRD_ZAP))
	check_newpost(ptr);
    zapchange = 1;
    zapbuf[ptr->bid - 1] = (ptr->myattr & BRD_ZAP ? 0 : login_start_time);
}

static void
choose_board(int newflag)
{
    static int      num = 0;
    boardstat_t    *ptr;
    int             head = -1, ch = 0, currmodetmp, tmp, tmp1, bidtmp;
    char            keyword[13] = "";

    setutmpmode(newflag ? READNEW : READBRD);
    if( zapbuf == NULL || favbuf == NULL )
	load_brdbuf();
    ++choose_board_depth;
    brdnum = 0;
    if (!cuser.userlevel)	/* guest yank all boards */
	yank_flag = 2;

    do {
	if (brdnum <= 0) {
	    load_boards(keyword);
	    if (brdnum <= 0) {
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
		if (HAS_PERM(PERM_SYSOP) || (currmode & MODE_MENU)) {
		    if (m_newbrd(0) == -1)
			break;
		    brdnum = -1;
		    continue;
		} else
		    break;
	    }
	    head = -1;
	}
	if (num < 0)
	    num = 0;
	else if (num >= brdnum)
	    num = brdnum - 1;

	if (head < 0) {
	    if (newflag) {
		tmp = num;
		while (num < brdnum) {
		    ptr = &nbrd[num];
		    if (ptr->myattr & BRD_UNREAD)
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
	case Ctrl('C'):
	    cal();
	    show_brdlist(head, 1, newflag);
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
	    ptr->myattr ^= BRD_TAG;
	    favchange = 1;
	    favbuf[ptr->bid - 1] = ptr->myattr;
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
		bcache[class_bid - 1].firstchild[cuser.uflag & BRDSORT_FLAG ? 1 : 0]
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
	    cuser.uflag ^= BRDSORT_FLAG;
	    brdnum = -1;
	    break;
	case 'y':
	    if (class_bid == 0)
		yank_flag = (yank_flag + 1) % 3;
	    else
		yank_flag = yank_flag % 2 + 1;
	    brdnum = -1;
	    break;
	case Ctrl('D'):
	    for (tmp = 0; tmp < numboards; tmp++) {
		if (favbuf[tmp] & BRD_TAG) {
		    favchange = 1;
		    favbuf[tmp] &= ~BRD_FAV;
		    favbuf[tmp] &= ~BRD_TAG;
		}
	    }
	    brdnum = -1;
	    break;
	case Ctrl('A'):
	    for (tmp = 0; tmp < numboards; tmp++) {
		if (favbuf[tmp] & BRD_TAG) {
		    favchange = 1;
		    favbuf[tmp] |= BRD_FAV;
		    favbuf[tmp] &= ~BRD_TAG;
		}
	    }
	    brdnum = -1;
	    break;
	case Ctrl('T'):
	    for (tmp = 0; tmp < numboards; tmp++)
		favbuf[tmp] &= ~BRD_TAG;
	    favchange = 1;
	    brdnum = -1;
	    break;
	case Ctrl('P'):
	    if (class_bid != 0 &&
		(HAS_PERM(PERM_SYSOP) || (currmode & MODE_MENU))) {
		for (tmp = 0; tmp < numboards; tmp++) {
		    boardheader_t  *bh = &bcache[tmp];
		    if (!(favbuf[tmp] & BRD_TAG) || bh->gid == class_bid)
			continue;
		    favchange = 1;
		    favbuf[tmp] &= ~BRD_TAG;
		    if (bh->gid != class_bid) {
			bh->gid = class_bid;
			substitute_record(FN_BOARD, bh,
					  sizeof(boardheader_t), tmp + 1);
			reset_board(tmp + 1);
			log_usies("SetBoardGID", bh->brdname);
		    }
		}
		brdnum = -1;
	    }
	    break;
	case 'm':
	    if (HAS_PERM(PERM_BASIC)) {
		ptr = &nbrd[num];
		ptr->myattr ^= BRD_FAV;
		favchange = 1;
		favbuf[ptr->bid - 1] = ptr->myattr;
		head = 9999;
	    }
	    break;
	case 'z':
	    if (HAS_PERM(PERM_BASIC)) {
		dozap(num);
		head = 9999;
	    }
	    break;
	case 'Z':		/* Ptt */
	    if (HAS_PERM(PERM_BASIC)) {
		for (tmp = 0; tmp < brdnum; tmp++) {
		    dozap(tmp);
		}
		head = 9999;
	    }
	    break;
	case 'v':
	case 'V':
	    ptr = &nbrd[num];
	    brc_initial(B_BH(ptr)->brdname);
	    if (ch == 'v') {
		ptr->myattr &= ~BRD_UNREAD;
		zapchange = 1;
		zapbuf[ptr->bid - 1] = brc_list[0] = now;
	    } else {
		zapchange = 1;
		zapbuf[ptr->bid - 1] = brc_list[0] = 1;
		ptr->myattr |= BRD_UNREAD;
	    }
	    brc_num = brc_changed = 1;
	    brc_update();
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
	    if (HAS_PERM(PERM_SYSOP) || (currmode & MODE_MENU)) {
		ptr = &nbrd[num];
		move(1, 1);
		clrtobot();
		m_mod_board(B_BH(ptr)->brdname);
		brdnum = -1;
	    }
	    break;
	case 'R':
	    if (HAS_PERM(PERM_SYSOP) || (currmode & MODE_MENU)) {
		m_newbrd(1);
		brdnum = -1;
	    }
	    break;
	case 'B':
	    if (HAS_PERM(PERM_SYSOP) || (currmode & MODE_MENU)) {
		m_newbrd(0);
		brdnum = -1;
	    }
	    break;
	case 'W':
	    if (class_bid > 0 &&
		(HAS_PERM(PERM_SYSOP) || (currmode & MODE_MENU))) {
		char            buf[128];
		setbpath(buf, bcache[class_bid - 1].brdname);
		mkdir(buf, 0755);	/* Ptt:¶}¸s²Õ¥Ø¿ý */
		b_note_edit_bname(class_bid);
		brdnum = -1;
	    }
	    break;
	case KEY_RIGHT:
	case '\n':
	case '\r':
	case 'r':
	    {
		char            buf[STRLEN];

		ptr = &nbrd[num];

		if (!(B_BH(ptr)->brdattr & BRD_GROUPBOARD)) {	/* «Dsub class */
		    if (!(B_BH(ptr)->brdattr & BRD_HIDE) ||
			(B_BH(ptr)->brdattr & BRD_POSTMASK)) {
			brc_initial(B_BH(ptr)->brdname);

			if (newflag) {
			    setbdir(buf, currboard);
			    tmp = unread_position(buf, ptr);
			    head = tmp - t_lines / 2;
			    getkeep(buf, head > 1 ? head : 1, tmp + 1);
			}
			board_visit_time = zapbuf[ptr->bid - 1];
			if (!(ptr->myattr & BRD_ZAP)){
			    zapchange = 1;
			    zapbuf[ptr->bid - 1] = now;
			}
			Read();
			check_newpost(ptr);
			head = -1;
			setutmpmode(newflag ? READNEW : READBRD);
		    } else {
			setbfile(buf, B_BH(ptr)->brdname, FN_APPLICATION);
			if (more(buf, YEA) == -1) {
			    move(1, 0);
			    clrtobot();
			    outs(privateboard);
			    pressanykey();
			}
			head = -1;
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

		    if (!(currmode & MODE_MENU))	/* ¦pªGÁÙ¨S¦³¤p²ÕªøÅv­­ */
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
    class_bid = 1;
    yank_flag = 1;
    choose_board(0);
    return 0;
}

int
Boards()
{
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

/*
 * int v_favorite(){ char fname[256]; char inbuf[2048]; FILE* fp; int nGroup;
 * char* strtmp;
 * 
 * setuserfile(fname,str_favorite);
 * 
 * if (!(fp=fopen(fname,"r"))) return -1; move(0,0); clrtobot();
 * fgets(inbuf,sizeof(inbuf),fp); nGroup=atoi(inbuf);
 * 
 * currutmp->nGroup=0; currutmp->ninRoot=0;
 * 
 * while(nGroup!=currutmp->nGroup+1){ fgets(inbuf,sizeof(inbuf),fp);
 * prints("%s\n",strtmp=strtok(inbuf," \n"));
 * strcpy(currutmp->gfavorite[currutmp->nGroup++],strtmp);
 * while((strtmp=strtok(NULL, " \n"))){ prints("     %s
 * %d\n",strtmp,getbnum(strtmp)); } currutmp->nGroup++; }
 * prints("+++%d+++\n",currutmp->nGroup);
 * 
 * fgets(inbuf,sizeof(inbuf),fp);
 * 
 * for(strtmp=strtok(inbuf, " \n");strtmp;strtmp=strtok(NULL, " \n")){ if
 * (strtmp[0]!='#') prints("*** %s %d\n",strtmp, getbnum(strtmp)); else
 * prints("*** %s %d\n",strtmp+1, -1); currutmp->ninRoot++; }
 * 
 * fclose(fp); pressanykey(); return 0; }
 */
