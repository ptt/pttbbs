/* $Id$ */
#include "bbs.h"

/**
 * 關於本檔案的細節，請見 docs/brc.txt。
 */

#ifndef BRC_MAXNUM
#define BRC_STRLEN      15	/* Length of board name, for old brc */
#define BRC_MAXSIZE     24576   /* Effective size of brc rc file, 8192 * 3 */
#define BRC_MAXNUM      80      /* Upper bound of brc_num, size of brc_list  */
#endif

#define BRC_BLOCKSIZE   1024

#if MAX_BOARD > 65535 || BRC_MAXSIZE > 65535
#error Max number of boards or BRC_MAXSIZE cannot fit in unsighed short, \
please rewrite brc.c
#endif

typedef unsigned short brcbid_t;
typedef unsigned short brcnbrd_t;

/* old brc rc file form:
 * board_name     15 bytes
 * brc_num         1 byte, binary integer
 * brc_list       brc_num * sizeof(int) bytes, brc_num binary integer(s) */

static char brc_initialized = 0;
static time4_t brc_expire_time;
 /* Will be set to the time one year before login. All the files created
  * before then will be recognized read. */

static int     brc_changed = 0;	/**< brc_list/brc_num changed */
/* The below two will be filled by read_brc_buf() and brc_update() */
static char   *brc_buf = NULL;
static int     brc_size;
static int     brc_alloc;

// read records for currbid
static int             brc_currbid;
static int             brc_num;
static time4_t         brc_list[BRC_MAXNUM];

static char * const fn_oldboardrc = ".boardrc";
static char * const fn_brc = ".brc2";

/**
 * find read records of bid in given buffer region
 *
 * @param[in]	begin
 * @param[in]	endp
 * @param	bid
 * @param[out]	num	number of records, which could be written for \a bid
 * 			= brc_num		if found
 * 			= 0 or dangling size	if not found
 * 					
 * @return	address of record. \a begin <= addr < \a endp.
 * 		0	if not found
 */
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
	tmpp += sizeof(brcnbrd_t) + *num * sizeof(time4_t); /* end of record */

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

static time4_t *
brc_find_record(int bid, int *num)
{
    char *p;
    brcnbrd_t tnum;
    p = brc_findrecord_in(brc_buf, brc_buf + brc_size, bid, &tnum);
    *num = tnum;
    if (p)
	return (time4_t*)(p + sizeof(brcbid_t) + sizeof(brcnbrd_t));
    *num = 0;
    return 0;
}

static char *
brc_putrecord(char *ptr, char *endp, brcbid_t bid,
	      brcnbrd_t num, const time4_t *list)
{
    char * tmp;
    if (num > 0 && list[0] > brc_expire_time &&
	    ptr + sizeof(brcbid_t) + sizeof(brcnbrd_t) < endp) {
	if (num > BRC_MAXNUM)
	    num = BRC_MAXNUM;

	if (num == 0) return ptr;

	*(brcbid_t*)ptr  = bid;         /* write in bid */
	ptr += sizeof(brcbid_t);
	*(brcnbrd_t*)ptr = num;         /* write in brc_num */
	ptr += sizeof(brcnbrd_t);
	tmp = ptr + num * sizeof(time4_t);
	if (tmp <= endp)
	    memcpy(ptr, list, num * sizeof(time4_t)); /* write in brc_list */
	ptr = tmp;
    }
    return ptr;
}

static inline int
brc_enlarge_buf(void)
{
    char *buffer;
    if (brc_alloc >= BRC_MAXSIZE)
	return 0;

#ifdef CRITICAL_MEMORY
#define THE_MALLOC(X) MALLOC(X)
#define THE_FREE(X) FREE(X)
#else
#define THE_MALLOC(X) alloca(X)
#define THE_FREE(X) (void)(X)
    /* alloca get memory from stack and automatically freed when
     * function returns. */
#endif

    buffer = (char*)THE_MALLOC(brc_alloc);
    assert(buffer);

    memcpy(buffer, brc_buf, brc_alloc);
    free(brc_buf);
    brc_alloc += BRC_BLOCKSIZE;
    brc_buf = (char*)malloc(brc_alloc);
    assert(brc_buf);
    memcpy(brc_buf, buffer, brc_alloc - BRC_BLOCKSIZE);

#ifdef DEBUG
    vmsgf("brc enlarged to %d bytes", brc_alloc);
#endif

    THE_FREE(buffer);
    return 1;

#undef THE_MALLOC
#undef THE_FREE
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
    assert(brc_buf);
}

static inline void
brc_insert_record(brcbid_t bid, brcnbrd_t num, const time4_t* list)
{
    char           *ptr;
    int             new_size, end_size;
    brcnbrd_t       tnum;

    ptr = brc_findrecord_in(brc_buf, brc_buf + brc_size, bid, &tnum);

    while (num > 0 && list[num - 1] < brc_expire_time)
	num--; /* don't write the times before brc_expire_time */

    if (!ptr) {
	brc_size -= (int)tnum;

	/* put on the beginning */
	if (num){
	    new_size = sizeof(brcbid_t) + sizeof(brcnbrd_t)
		+ num * sizeof(time4_t);
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
	int len = sizeof(brcbid_t) + sizeof(brcnbrd_t) + tnum * sizeof(time4_t);
	char *tmpp = ptr + len;
	end_size = brc_buf + brc_size - tmpp;
	if (num) {
	    int sindex = ptr - brc_buf;
	    new_size = (sizeof(brcbid_t) + sizeof(brcnbrd_t)
			+ num * sizeof(time4_t));
	    brc_size += new_size - len;
	    if (brc_size > brc_alloc) {
		if (brc_enlarge_buf()) {
		    ptr = brc_buf + sindex;
		    tmpp = ptr + len;
		} else {
		    end_size -= brc_size - BRC_MAXSIZE;
		    brc_size = BRC_MAXSIZE;
		}
	    }
	    if (end_size > 0 && ptr + new_size != tmpp)
		memmove(ptr + new_size, tmpp, end_size);
	    brc_putrecord(ptr, brc_buf + brc_alloc, bid, num, list);
	} else { /* deleting record */
	    memmove(ptr, tmpp, end_size);
	    brc_size -= len;
	}
    }

    brc_changed = 0;
}

/**
 * write \a brc_num and \a brc_list back to \a brc_buf.
 */
void
brc_update(){
    if (brc_currbid && brc_changed && cuser.userlevel && brc_num > 0) {
	brc_initialize();
	brc_insert_record(brc_currbid, brc_num, brc_list);
    }
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
	    + sizeof(time4_t) * num;
	ptr += sizeof(time4_t) * num;
    }
}

inline static void
read_brc_buf(void)
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

/* release allocated memory
 *
 * Do not destory brc_currbid, brc_num, brc_list.
 */
void
brc_release()
{
    if (brc_buf) {
	free(brc_buf);
	brc_buf = NULL;
    }
    brc_changed = 0;
    brc_size = brc_alloc = 0;
}

void
brc_finalize(){
    char brcfile[STRLEN];
    char tmpfile[STRLEN];

    if(!brc_initialized)
	return;

    brc_update();
    setuserfile(brcfile, fn_brc);
    snprintf(tmpfile, sizeof(tmpfile), "%s.tmp.%x", brcfile, getpid());
    if (brc_buf != NULL) {
	int fd = open(tmpfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd != -1) {
	    int ok=0;
	    if(write(fd, brc_buf, brc_size)==brc_size)
		ok=1;
	    close(fd);
	    if(ok)
		Rename(tmpfile, brcfile);
	    else
		unlink(tmpfile);
	}
    }

    brc_release();
    brc_initialized = 0;
}

int
brc_initialize(){
    if (brc_initialized)
	return 1;
    brc_initialized = 1;
    brc_expire_time = login_start_time - 365 * 86400;
    read_brc_buf();
    return 0;
}

/** 
 * get the read records for bid
 *
 * @param bid
 * @param[out] num	number of record for \a bid. 1 <= \a num <= \a BRC_MAXNUM
 * 			\a num = 1 if no records.
 * @param[out] list	the list of records, length \a num
 *
 * @return number of read record, 0 if no records
 */
static int
brc_read_record(int bid, int *num, time4_t *list){
    char *ptr;
    brcnbrd_t tnum;
    ptr = brc_findrecord_in(brc_buf, brc_buf + brc_size, bid, &tnum);
    *num = tnum;
    if ( ptr ){
	assert(0 <= *num && *num <= BRC_MAXNUM);
	memcpy(list, ptr + sizeof(brcbid_t) + sizeof(brcnbrd_t),
	       *num * sizeof(time4_t));
	return *num;
    }
    list[0] = *num = 1;
    return 0;
}

/**
 * @return number of records in \a boardname
 */
int
brc_initial_board(const char *boardname)
{
    brc_initialize();

    if (strcmp(currboard, boardname) == 0) {
	assert(currbid == brc_currbid);
	return brc_num;
    }

    brc_update(); /* write back first */
    currbid = getbnum(boardname);
    if( currbid == 0 )
	currbid = getbnum(DEFAULT_BOARD);
    assert(0<=currbid-1 && currbid-1<MAX_BOARD);
    brc_currbid = currbid;
    currboard = bcache[currbid - 1].brdname;
    currbrdattr = bcache[currbid - 1].brdattr;

    return brc_read_record(brc_currbid, &brc_num, brc_list);
}

static void
brc_trunc(int bid, time4_t ftime){
    brc_insert_record(bid, 1, &ftime);
    if ( bid == brc_currbid ){
	brc_num = 1;
	brc_list[0] = ftime;
	brc_changed = 0;
    }
}

void
brc_toggle_all_read(int bid, int is_all_read)
{
    brc_initialize();
    if (is_all_read)
        brc_trunc(bid, now);
    else
        brc_trunc(bid, 1);
}

void
brc_addlist(const char *fname)
{
    int             n, i;
    time4_t         ftime;

    assert(currbid == brc_currbid);
    if (!cuser.userlevel)
	return;
    brc_initialize();

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
brc_unread_time(int bid, time4_t ftime)
{
    int             i;
    int	bnum;
    const time4_t *blist;

    brc_initialize();
    if (ftime <= brc_expire_time) /* too old */
	return 0;

    if (brc_currbid && bid == brc_currbid) {
	blist = brc_list;
	bnum = brc_num;
    } else {
	blist = brc_find_record(bid, &bnum);
    }

    if (bnum <= 0)
	return 1;
    for (i = 0; i < bnum; i++) { /* using linear search */
	if (ftime > blist[i])
	    return 1;
	else if (ftime == blist[i])
	    return 0;
    }
    return 0;
}

int
brc_unread(int bid, const char *fname)
{
    int             ftime;

    ftime = atoi(&fname[2]); /* this will get the time of the file created */

    return brc_unread_time(bid, ftime);
}
