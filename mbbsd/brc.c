/* $Id$ */
#include "bbs.h"

#ifndef BRC_MAXNUM
#define BRC_STRLEN      15	/* Length of board name */
#define BRC_MAXSIZE     24576   /* Effective size of brc rc file, 8192 * 3 */
#define BRC_MAXNUM      80      /* Upper bound of brc_num, size of brc_list  */
#endif

#define BRC_BLOCKSIZE   1024

#if MAX_BOARD > 32767 || BRC_MAXSIZE > 32767
#error Max number of boards or BRC_MAXSIZE cannot fit in unsighed short, \
please rewrite brc.c
#endif

typedef unsigned short brcbid_t;
typedef unsigned short brcnbrd_t;

/* old brc rc file form:
 * board_name     15 bytes
 * brc_num         1 byte, binary integer
 * brc_list       brc_num * sizeof(int) bytes, brc_num binary integer(s) */

static time_t   brc_expire_time;
 /* Will be set to the time one year before login. All the files created
  * before then will be recognized read. */

static int      brc_changed = 0;
/* The below two will be filled by read_brc_buf() and brc_update() */
static char     *brc_buf = NULL;
static int       brc_size;
static int       brc_alloc;

static char * const fn_oldboardrc = ".boardrc";
static char * const fn_brc = ".brc2";

#if 0
/* unused after brc2 */
static char *
brc_getrecord(char *ptr, char *endp, brcbid_t *bid, brcnbrd_t *pnum, time_t *list)
{
    brcnbrd_t num;
    char   *tmp;

    if (ptr + sizeof(brcbid_t) + sizeof(brcnbrd_t) > endp)
	return endp + 1; /* dangling, ignoring it */
    *bid = *(brcbid_t*)ptr;    /* bid */
    ptr += sizeof(brcbid_t);
    num = *(brcnbrd_t*)ptr;    /* brc_num */
    ptr += sizeof(brcnbrd_t);
    tmp = ptr + num * sizeof(time_t);     /* end of this record */
    if (tmp <= endp){
	memcpy(list, ptr, num * sizeof(time_t)); /* brc_list */
	if (num > BRC_MAXNUM)
	    num = BRC_MAXNUM;
	*pnum = num;
    }
    return tmp;
}
#endif

/* Returns the address of the record strictly between begin and endp with
 * bid equal to the parameter. Returns 0 if not found.
 * brcnbrd_t *num is an output parameter which will filled with brc_num
 * if the record is found. If not found the record, *num will be the number
 * of dangling bytes. */
static char *
brc_findrecord_in(char *begin, char *endp, brcbid_t bid, brcnbrd_t *num)
{
    char     *tmpp, *ptr = begin;
    brcbid_t tbid;
    while (ptr + sizeof(brcbid_t) + sizeof(brcnbrd_t) < endp) {
	/* for each available records */
	tmpp = ptr;
	tbid = *(brcbid_t*)tmpp;
	tmpp += sizeof(brcbid_t);
	*num = *(brcnbrd_t*)tmpp;
	tmpp += sizeof(brcnbrd_t) + *num * sizeof(time_t); /* end of record */

	if ( tmpp > endp ){
	    /* dangling, ignore the trailing data */
	    *num = (brcnbrd_t)(endp - ptr); /* for brc_insert_record() */
	    return 0;
	}
	if ( tbid == bid )
	    return ptr;
	ptr = tmpp;
    }

    *num = 0;
    return 0;
}

time_t *
brc_find_record(int bid, int *num)
{
    char *p;
    *num = 0;
    p = brc_findrecord_in(brc_buf, brc_buf + brc_size, bid, (brcnbrd_t*)num);
    if (p)
	return (time_t*)(p + sizeof(brcbid_t) + sizeof(brcnbrd_t));
    *num = 0;
    return 0;
}

static char *
brc_putrecord(char *ptr, char *endp, brcbid_t bid, brcnbrd_t num, const time_t *list)
{
    char * tmp;
    if (num > 0 && list[0] > brc_expire_time &&
	    ptr + sizeof(brcbid_t) + sizeof(brcnbrd_t) <= endp) {
	if (num > BRC_MAXNUM)
	    num = BRC_MAXNUM;

	while (num > 0 && list[num - 1] < brc_expire_time)
	    num--; /* don't write the times before brc_expire_time */

	if (num == 0) return ptr;

	*(brcbid_t*)ptr  = bid;         /* write in bid */
	ptr += sizeof(brcbid_t);
	*(brcnbrd_t*)ptr = num;         /* write in brc_num */
	ptr += sizeof(brcnbrd_t);
	tmp = ptr + num * sizeof(time_t);
	if (tmp <= endp)
	    memcpy(ptr, list, num * sizeof(time_t)); /* write in brc_list */
	ptr = tmp;
    }
    return ptr;
}

static inline int
brc_enlarge_buf()
{
    char buffer[BRC_MAXSIZE - BRC_BLOCKSIZE];
    if (brc_alloc >= BRC_MAXSIZE)
	return 0;
    memcpy(buffer, brc_buf, brc_alloc);
    free(brc_buf);
    brc_alloc += BRC_BLOCKSIZE;
    brc_buf = (char*)malloc(brc_alloc);
    memcpy(brc_buf, buffer, brc_alloc - BRC_BLOCKSIZE);
    return 1;
}

static inline void
brc_get_buf(int size){
    if (!size)
	brc_alloc = BRC_BLOCKSIZE;
    else
	brc_alloc = (size + BRC_BLOCKSIZE - 1) / BRC_BLOCKSIZE * BRC_BLOCKSIZE;
    if (brc_alloc > BRC_MAXSIZE)
	brc_alloc = BRC_MAXSIZE;
    brc_buf = (char*)malloc(brc_alloc);
}

static inline void
brc_insert_record(brcbid_t bid, brcnbrd_t num, time_t* list)
{
    char           *ptr;
    int             new_size, end_size;
    brcnbrd_t       tnum;

    ptr = brc_findrecord_in(brc_buf, brc_buf + brc_size, bid, &tnum);

    /* FIXME: this loop is copied from brc_putrecord() */
    while (num > 0 && list[num - 1] < brc_expire_time)
	num--; /* don't write the times before brc_expire_time */

    if (!ptr) {
	brc_size -= tnum;

	/* put on the beginning */
	if (num && (new_size =
		sizeof(brcbid_t) + sizeof(brcnbrd_t) + num * sizeof(time_t))){
	    brc_size += new_size;
	    if (brc_size > brc_alloc && !brc_enlarge_buf())
		brc_size = BRC_MAXSIZE;
	    if (brc_size > new_size)
		memmove(brc_buf + new_size, brc_buf, brc_size - new_size);
	    brc_putrecord(brc_buf, brc_buf + new_size, bid, num, list);
	}
    } else {
	/* ptr points to the old current brc list.
	 * tmpp is the end of it (exclusive).       */
	char *tmpp = ptr + sizeof(brcbid_t) + sizeof(brcnbrd_t)
	    + tnum * sizeof(time_t);
	end_size = brc_buf + brc_size - tmpp;
	if (num) {
	    new_size = sizeof(brcbid_t) + sizeof(brcnbrd_t)
		+ num * sizeof(time_t);
	    brc_size += new_size - (tmpp - ptr);
	    if (brc_size > brc_alloc && ! brc_enlarge_buf() ) {
		end_size -= brc_size - BRC_MAXSIZE;
		brc_size = BRC_MAXSIZE;
	    }
	    if (end_size > 0 && ptr + new_size != tmpp)
		memmove(ptr + new_size, tmpp, end_size);
	    brc_putrecord(ptr, brc_buf + brc_alloc, bid, num, list);
	} else { /* deleting record */
	    memmove(ptr, tmpp, end_size);
	    brc_size -= (tmpp - ptr);
	}
    }

    brc_changed = 0;
}

void
brc_update(){
    if (brc_changed && cuser->userlevel && brc_num > 0)
	brc_insert_record(currbid, brc_num, brc_list);
}

/* return 1 if successfully read from old .boardrc file.
 * otherwise, return 0. */
inline static void
read_old_brc(int fd)
{
    char        brdname[BRC_STRLEN + 1];
    char       *ptr;
    brcnbrd_t   num;
    brcbid_t    bid;
    brcbid_t    read_brd[512];
    int         nRead = 0, i;

    ptr = brc_buf;
    brc_size = 0;
    while (read(fd, brdname, BRC_STRLEN + 1) == BRC_STRLEN + 1) {
	num = brdname[BRC_STRLEN];
	brdname[BRC_STRLEN] = 0;
	bid = getbnum(brdname);

	for (i = 0; i < nRead; ++i)
	    if (read_brd[i] == bid)
		break;
	if (i != nRead){
	    lseek(fd, num * sizeof(int), SEEK_CUR);
	    continue;
	}
	read_brd[nRead >= 512 ? nRead = 0 : nRead++] = bid;

	*(brcbid_t*)ptr = bid;
	ptr += sizeof(brcbid_t);
	*(brcnbrd_t*)ptr = num;
	ptr += sizeof(brcnbrd_t);
	if (read(fd, ptr, sizeof(int) * num) != sizeof(int) * num)
	    break;

	brc_size += sizeof(brcbid_t) + sizeof(brcnbrd_t)
	    + sizeof(time_t) * num;
	ptr += sizeof(time_t) * num;
    }
}

inline static void
read_brc_buf()
{
    if (brc_buf == NULL) {
	char            brcfile[STRLEN];
	int             fd;
	struct stat     brcstat;

	setuserfile(brcfile, fn_brc);
	if ((fd = open(brcfile, O_RDONLY)) != -1) {
	    fstat(fd, &brcstat);
	    brc_get_buf(brcstat.st_size);
	    brc_size = read(fd, brc_buf, brc_alloc);
	    close(fd);
	} else {
	    setuserfile(brcfile, fn_oldboardrc);
	    if ((fd = open(brcfile, O_RDONLY)) != -1) {
		fstat(fd, &brcstat);
		brc_get_buf(brcstat.st_size);
		read_old_brc(fd);
		close(fd);
	    } else
		brc_size = 0;
	}
    }
}

void
brc_finalize(){
    char brcfile[STRLEN];
    int fd;
    brc_update();
    setuserfile(brcfile, fn_brc);
    if (brc_buf != NULL &&
	(fd = open(brcfile, O_WRONLY | O_CREAT | O_TRUNC, 0644)) != -1) {
	write(fd, brc_buf, brc_size);
	close(fd);
    }
}

int
brc_initialize(){
    static char done = 0;
    if (done)
	return 1;
    done = 1;
    brc_expire_time = login_start_time - 365 * 86400;
    read_brc_buf();
    return 0;
}

int
brc_read_record(int bid, int *num, time_t *list){
    char *ptr;
    *num = 0;
    ptr = brc_findrecord_in(brc_buf, brc_buf + brc_size, bid, (brcnbrd_t*)num);
    if ( ptr ){
	memcpy(list, ptr + sizeof(brcbid_t) + sizeof(brcnbrd_t),
		*num * sizeof(time_t));
	return *num;
    }
    list[0] = *num = 1;
    return 0;
}

int
brc_initial_board(const char *boardname)
{
    brc_initialize();

    if (strcmp(currboard, boardname) == 0) {
	return brc_num;
    }

    brc_update(); /* write back first */
    currbid = getbnum(boardname);
    currboard = bcache[currbid - 1].brdname;
    currbrdattr = bcache[currbid - 1].brdattr;

    return brc_read_record(currbid, &brc_num, brc_list);
}

void
brc_trunc(int bid, time_t ftime){
    brc_insert_record(bid, 1, &ftime);
    if ( bid == currbid ){
	brc_num = 1;
	brc_list[0] = ftime;
	brc_changed = 0;
    }
}

void
brc_addlist(const char *fname)
{
    int             n, i;
    time_t          ftime;

    if (!cuser->userlevel)
	return;

    ftime = atoi(&fname[2]);
    if (ftime <= brc_expire_time /* too old, don't do any thing  */
	 /* || fname[0] != 'M' || fname[1] != '.' */ ) {
	return;
    }
    if (brc_num <= 0) { /* uninitialized */
	brc_list[0] = ftime;
	brc_num = 1;
	brc_changed = 1;
	return;
    }
    if ((brc_num == 1) && (ftime < brc_list[0])) /* most when after 'v' */
	return;
    for (n = 0; n < brc_num; n++) { /* using linear search */
	if (ftime == brc_list[n]) {
	    return;
	} else if (ftime > brc_list[n]) {
	    if (brc_num < BRC_MAXNUM)
		brc_num++;
	    /* insert ftime into brc_list */
	    for (i = brc_num - 1; --i >= n; brc_list[i + 1] = brc_list[i]);
	    brc_list[n] = ftime;
	    brc_changed = 1;
	    return;
	}
    }
}

int
brc_unread_time(time_t ftime, int bnum, const time_t *blist)
{
    int             n;

    if (ftime <= brc_expire_time) /* too old */
	return 0;

    if (bnum <= 0)
	return 1;
    for (n = 0; n < bnum; n++) { /* using linear search */
	if (ftime > blist[n])
	    return 1;
	else if (ftime == blist[n])
	    return 0;
    }
    return 0;
}

int
brc_unread(const char *fname, int bnum, const time_t *blist)
{
    int             ftime, n;

    ftime = atoi(&fname[2]); /* this will get the time of the file created */

    if (ftime <= brc_expire_time) /* too old */
	return 0;

    if (bnum <= 0)
	return 1;
    for (n = 0; n < bnum; n++) { /* using linear search */
	if (ftime > blist[n])
	    return 1;
	else if (ftime == blist[n])
	    return 0;
    }
    return 0;
}
