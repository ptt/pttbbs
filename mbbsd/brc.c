#include "bbs.h"
#include "daemons.h"

/**
 * 關於本檔案的細節，請見 docs/brc.txt。
 * v3: add last modified time for comment system. double max size.
 *     original time_t as 'create'.
 */

// WARNING: Check ../pttbbs.conf, you may have overide these value there
// TODO MAXSIZE may be better smaller to fit into memory page.
#ifndef BRC_MAXNUM
#define BRC_MAXSIZE     65536   /* Effective size of brc rc file, 64KB (8192 * 8) */
#define BRC_MAXNUM      80      /* Upper bound of brc_num, size of brc_list  */
#endif

#define BRC_BLOCKSIZE   1024

// Note: BRC v3 should already support BRC_MAXSIZE > 65535,
// but not widely tested yet.
// MAX_BOARD >65535 is already tested on PTT2 since 2009/09/10.
#if BRC_MAXSIZE > 65536
#error BRC_MAXSIZE exceeds 64KB
#endif

typedef uint32_t brcbid_t;
typedef uint16_t brcnbrd_t;

typedef struct {
    time4_t create;
    time4_t modified;
} brc_rec;

/* old brc rc file form:
 * board_name     15 bytes
 * brc_num         1 byte, binary integer
 * brc_list       brc_num * sizeof(brc_rec) bytes  */

static char brc_initialized = 0;
static time4_t brc_expire_time;
 /* Will be set to the time one year before login. All the files created
  * before then will be recognized read. */

static int     brc_changed = 0;	/**< brc_list/brc_num changed */
/* The below two will be filled by read_brc_buf() and brc_update() */
static char   *brc_buf = NULL;
static int     brc_size;
static int     brc_alloc;
static int     brc_needs_file_update;

// read records for currbid
static int             brc_currbid;
static int             brc_num;
static brc_rec         brc_list[BRC_MAXNUM];

static char * const fn_brc GCC_UNUSED = ".brc3";

static inline brcbid_t brc_read_bid(const void *p) {
    brcbid_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}
static inline brcnbrd_t brc_read_num(const void *p) {
    brcnbrd_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}
static inline void brc_write_num(void *p, brcnbrd_t v) {
    memcpy(p, &v, sizeof(v));
}

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
brc_findrecord_in(char *begin, char *endp, brcbid_t bid, int *num)
{
    char     *tmpp, *ptr = begin;
    brcbid_t tbid;
    while (ptr + sizeof(brcbid_t) + sizeof(brcnbrd_t) < endp) {
	/* for each available records */
	tmpp = ptr;
	tbid = brc_read_bid(tmpp);
	tmpp += sizeof(brcbid_t);
	*num = brc_read_num(tmpp);
	tmpp += sizeof(brcnbrd_t) + *num * sizeof(brc_rec); /* end of record */

	if ( tmpp > endp ){
	    /* dangling, ignore the trailing data */
	    *num = (int)(endp - ptr); /* for brc_insert_record() */
	    return 0;
	}
	if ( tbid == bid )
	    return ptr;
	ptr = tmpp;
    }

    *num = 0;
    return 0;
}

static brc_rec *
brc_find_record(int bid, int *num)
{
    char *p;
    int tnum;
    p = brc_findrecord_in(brc_buf, brc_buf + brc_size, bid, &tnum);
    *num = tnum;
    if (p)
	return (brc_rec*)(p + sizeof(brcbid_t) + sizeof(brcnbrd_t));
    *num = 0;
    return 0;
}

static char *
brc_putrecord(char *ptr, char *endp, brcbid_t bid,
	      brcnbrd_t num, const brc_rec *list)
{
    char * tmp;
    if (num > 0 && time4_gt(list[0].create, brc_expire_time) &&
	    ptr + sizeof(brcbid_t) + sizeof(brcnbrd_t) < endp) {
	if (num > BRC_MAXNUM)
	    num = BRC_MAXNUM;

	if (num == 0) return ptr;

	*(brcbid_t*)ptr  = bid;         /* write in bid */
	ptr += sizeof(brcbid_t);
	*(brcnbrd_t*)ptr = num;         /* write in brc_num */
	ptr += sizeof(brcnbrd_t);
	tmp = ptr + num * sizeof(brc_rec);
	if (tmp <= endp)
	    memcpy(ptr, list, num * sizeof(brc_rec)); /* write in brc_list */
	ptr = tmp;
    }
    return ptr;
}

static int
brc_enlarge_buf(void)
{
    if (brc_alloc >= BRC_MAXSIZE)
	return 0;

    brc_alloc += BRC_BLOCKSIZE;
    brc_buf = (char*)realloc(brc_buf, brc_alloc);
    assert(brc_buf);

#ifdef DEBUG
    vmsgf("brc enlarged to %d bytes", brc_alloc);
#endif

    return 1;
}

static void
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

static void
brc_compact_tail_drop(int needed)
{
    while (brc_size + needed > BRC_MAXSIZE && brc_size > 0) {
	char *ptr = brc_buf;
	char *last_rec = NULL;
	char *endp = brc_buf + brc_size;

	while (ptr + sizeof(brcbid_t) + sizeof(brcnbrd_t) < endp) {
	    brcnbrd_t num = brc_read_num(ptr + sizeof(brcbid_t));
	    int rec_len = sizeof(brcbid_t) + sizeof(brcnbrd_t) + num * sizeof(brc_rec);
	    if (ptr + rec_len > endp)
		break;
	    last_rec = ptr;
	    ptr += rec_len;
	}

	if (last_rec) {
	    brc_size = last_rec - brc_buf;
	} else {
	    break;
	}
    }
}

/**
 * On-demand compaction:
 * Compact records from the tail (oldest / least-recently accessed boards)
 * down to num = 1 (retaining the latest article timestamp blist[0]).
 * Stops as soon as needed bytes are freed.
 */
static void
brc_compact(int needed, brcbid_t protect_bid)
{
    while (brc_size + needed > BRC_MAXSIZE && brc_size > 0) {
	/* Find the tail-most candidate record with num > 1 */
	char *ptr = brc_buf;
	char *endp = brc_buf + brc_size;
	char *candidate = NULL;

	while (ptr + sizeof(brcbid_t) + sizeof(brcnbrd_t) < endp) {
	    brcbid_t bid = brc_read_bid(ptr);
	    brcnbrd_t num = brc_read_num(ptr + sizeof(brcbid_t));
	    int rec_len = sizeof(brcbid_t) + sizeof(brcnbrd_t) + num * sizeof(brc_rec);
	    if (ptr + rec_len > endp)
		break;
	    if (bid != protect_bid && num > 1) {
		candidate = ptr;
	    }
	    ptr += rec_len;
	}

	if (!candidate)
	    break; /* No more boards can be compacted to num=1 */

	/* Compact candidate to num = 1, keeping blist[0] (latest read article timestamp) */
	brcnbrd_t cnum = brc_read_num(candidate + sizeof(brcbid_t));
	int old_len = sizeof(brcbid_t) + sizeof(brcnbrd_t) + cnum * sizeof(brc_rec);
	int new_len = sizeof(brcbid_t) + sizeof(brcnbrd_t) + 1 * sizeof(brc_rec);
	int diff = old_len - new_len;
	char *tail = candidate + old_len;
	int tail_size = (brc_buf + brc_size) - tail;

	brc_write_num(candidate + sizeof(brcbid_t), 1);
	if (tail_size > 0)
	    memmove(candidate + new_len, tail, tail_size);
	brc_size -= diff;
    }

}

static void
brc_insert_record(brcbid_t bid, brcnbrd_t num, const brc_rec* list)
{
    char           *ptr;
    int             new_size;
    int             tnum;

    brc_needs_file_update = 1;
    ptr = brc_findrecord_in(brc_buf, brc_buf + brc_size, bid, &tnum);

    while (num > 0 && time4_lt(list[num - 1].create, brc_expire_time))
	num--; /* don't write the times before brc_expire_time */

    new_size = num ? (sizeof(brcbid_t) + sizeof(brcnbrd_t) + num * sizeof(brc_rec)) : 0;

    if (ptr) {
	int len = sizeof(brcbid_t) + sizeof(brcnbrd_t) + tnum * sizeof(brc_rec);

	/* If already at the head and size is identical, just overwrite in-place */
	if (ptr == brc_buf && new_size == len) {
	    brc_putrecord(brc_buf, brc_buf + new_size, bid, num, list);
	    brc_changed = 0;
	    return;
	}

	/* Move-to-front (MRU/LRU): remove old record from its current position */
	char *tmpp = ptr + len;
	int end_size = brc_buf + brc_size - tmpp;
	if (end_size > 0)
	    memmove(ptr, tmpp, end_size);
	brc_size -= len;
    } else {
	/* Clean trailing corrupted/dangling bytes if any */
	brc_size -= (int)tnum;
    }

    /* If deleting record (num == 0), record was already removed above */
    if (!num) {
	brc_changed = 0;
	return;
    }

    /* Insert at the head of brc_buf */
    if (brc_size + new_size > BRC_MAXSIZE)
	brc_compact(new_size, bid);

    /* If other boards were all compacted to num=1 but buffer still exceeds,
     * compact current board to num=1 before dropping other boards from tail */
    if (brc_size + new_size > BRC_MAXSIZE && num > 1) {
	num = 1;
	new_size = sizeof(brcbid_t) + sizeof(brcnbrd_t) + sizeof(brc_rec);
    }

    if (!brc_buf)
	brc_get_buf(new_size);

    while (brc_size + new_size > brc_alloc && brc_enlarge_buf())
	;

    /* Absolute fallback: even with every board at num=1, buffer exceeds -> drop tail */
    if (brc_size + new_size > BRC_MAXSIZE)
	brc_compact_tail_drop(new_size);

    if (brc_size + new_size <= BRC_MAXSIZE) {
	if (brc_size > 0)
	    memmove(brc_buf + new_size, brc_buf, brc_size);
	brc_size += new_size;
	brc_putrecord(brc_buf, brc_buf + new_size, bid, num, list);
    }

    brc_changed = 0;
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

/**
 * write  brc_num and  brc_list back to  brc_buf.
 */
void
brc_update(){
    if (brc_currbid && brc_changed && cuser.userlevel && brc_num > 0) {
	brc_initialize();
	brc_insert_record(brc_currbid, brc_num, brc_list);
    }
}

static inline int
use_brcstore(void)
{
    return IS_ENABLED(CONFIG_BRC_STORE);
}

static void
get_brc_alt_filename(char *filename)
{
    unsigned hash = StringHash(cuser.userid) & 0xffff;

    assert(is_validuserid(cuser.userid));
    snprintf(filename, PATHLEN, BRCSTORE_DIR "/%02x/%02x/brc.%s",
	    (hash >> 8) & 0xff, hash & 0xff,
	    cuser.userid);
}

static void
get_brc_filename(char *filename)
{
    if (use_brcstore()) {
	get_brc_alt_filename(filename);
    } else {
	setuserfile(filename, fn_brc);
    }
}

int
load_local_brc() {
    char            brcfile[PATHLEN];
    int             fd;
    struct stat     brcstat;

    brc_size = 0;
    get_brc_filename(brcfile);

    if ((fd = open(brcfile, O_RDONLY)) == -1) {
	if (use_brcstore()) {
	    /* Migration fallback: load legacy home .brc3 if brcstore file is not created yet */
	    char homebrc[PATHLEN];
	    setuserfile(homebrc, fn_brc);
	    fd = open(homebrc, O_RDONLY);
	}
	if (fd == -1)
	    return 0;
    }

    posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);
    fstat(fd, &brcstat);
    brc_get_buf(brcstat.st_size);
    brc_size = read(fd, brc_buf, brc_alloc);
    posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
    close(fd);

    brc_needs_file_update = 0;
    return 1;
}

int
save_local_brc() {
    if (!brc_needs_file_update)
	return 1;

    int ok = 1;
    char brcfile[PATHLEN];
    char tmpfile[PATHLEN];

    get_brc_filename(brcfile);
    if (use_brcstore()) {
	snprintf(tmpfile, sizeof(tmpfile), BRCSTORE_DIR "/tmp/brc.%s.%x", cuser.userid, getpid());
    } else {
	snprintf(tmpfile, sizeof(tmpfile), "%s.tmp.%x", brcfile, getpid());
    }

    if (brc_buf != NULL) {
	int fd = OpenCreate(tmpfile, O_WRONLY | O_TRUNC);
	if (fd == -1 && use_brcstore()) {
	    /* Fallback if BRCSTORE_DIR/tmp is missing or inaccessible */
	    snprintf(tmpfile, sizeof(tmpfile), "%s.tmp.%x", brcfile, getpid());
	    fd = OpenCreate(tmpfile, O_WRONLY | O_TRUNC);
	}
	if (fd != -1) {
	    int write_ok = 0;
	    posix_fallocate(fd, 0, brc_size);
	    if (write(fd, brc_buf, brc_size) == brc_size)
		write_ok = 1;
	    close(fd);
	    if (write_ok) {
		Rename(tmpfile, brcfile);
	    } else {
		unlink(tmpfile);
		ok = 0;
	    }
	} else {
	    ok = 0;
	}
    }
    brc_needs_file_update = 0;
    return ok;
}

void
read_brc_buf(void)
{
    if (brc_buf != NULL)
	return;

    load_local_brc();
}

void
brc_finalize(){
    if(!brc_initialized)
	return;

    brc_update();
    save_local_brc();

    brc_release();
    brc_initialized = 0;
}

int
brc_initialize(){
    if (brc_initialized)
	return 1;
    brc_initialized = 1;
    brc_expire_time = login_start_time - 365 * DAY_SECONDS;
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
brc_read_record(int bid, int *num, brc_rec *list){
    char *ptr;
    int tnum;
    ptr = brc_findrecord_in(brc_buf, brc_buf + brc_size, bid, &tnum);
    *num = tnum;
    if ( ptr ){
	assert(0 <= *num && *num <= BRC_MAXNUM);
	memcpy(list, ptr + sizeof(brcbid_t) + sizeof(brcnbrd_t),
	       *num * sizeof(brc_rec));
	if(*num < BRC_MAXNUM)
	{
	    memset(&list[*num], 0, sizeof(brc_rec));
	}
	return *num;
    }
    list[0].create = *num = 1;
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
    brc_rec r;

    r.create = ftime;
    r.modified = ftime;
    brc_insert_record(bid, 1, &r);
    if ( bid == brc_currbid ){
	brc_num = 1;
	brc_list[0] = r;
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
brc_toggle_read(int bid, time4_t newtime)
{
    brc_initialize();
    brc_trunc(bid, newtime);
}

void
brc_addlist(const char *fname, time4_t modified)
{
    int             n, i;
    brc_rec         frec;

    assert(currbid == brc_currbid);
    if (!cuser.userlevel)
	return;
    brc_initialize();

    frec.create = get_fhdr_stamp_ts(fname);
    frec.modified = modified;

    if (time4_le(frec.create, brc_expire_time) /* too old, don't do any thing  */
	 /* || fname[0] != 'M' || fname[1] != '.' */ ) {
	return;
    }
    if (brc_num <= 0) { /* uninitialized */
	brc_list[0] = frec;
	brc_num = 1;
	brc_changed = 1;
	return;
    }
    if ((brc_num == 1) && time4_lt(frec.create, brc_list[0].create)) /* most when after 'v' */
	return;
    for (n = 0; n < brc_num; n++) { /* using linear search */
	if (frec.create == brc_list[n].create) {
	    // check if we got bad value.
	    if (time4_is_invalid(modified))
		return;

	    if (time4_is_invalid(brc_list[n].modified))
		brc_list[n].modified = 0;

	    // special case here:
	    // in order to support special system like 'always bottom',
	    // they may share same create time and different time.
	    if (time4_lt(brc_list[n].modified, modified))
	    {
		brc_list[n].modified = modified;
		brc_changed = 1;
	    }
	    return;
	} else if (time4_gt(frec.create, brc_list[n].create)) {
	    if (brc_num < BRC_MAXNUM)
		brc_num++;
	    /* insert frec into brc_list */
	    for (i = brc_num - 1; --i >= n; brc_list[i + 1] = brc_list[i]);
	    brc_list[n] = frec;
	    brc_changed = 1;
	    return;
	}
    }
}

// return:
//   0 - read
//   1 - unread (by create)
//   2 - unread (by modified)
int
brc_unread_time(int bid, time4_t ftime, time4_t modified)
{
    int             i;
    int	bnum;
    const brc_rec *blist;

    brc_initialize();
    if (time4_le(ftime, brc_expire_time)) /* too old */
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
	if (time4_gt(ftime, blist[i].create))
	    return 1;
	else if (ftime == blist[i].create)
	{
	    time4_t brcm = blist[i].modified;
	    if (modified == 0 || brcm == 0)
		return 0;

	    // bad  case... seems like that someone is making -1.
	    if (time4_is_invalid(modified) || time4_is_invalid(brcm))
		return 0;

	    // one case is, some special file headers (ex,
	    // always bottom) may cause issue. They share create
	    // time (filename) and apply different modify time.
	    // so let's back to 'greater'.
	    return time4_gt(modified, brcm) ? 2 : 0;
	}
    }
    return 0;
}

int
brc_unread(int bid, const char *fname, time4_t modified)
{
    int             ftime;

    ftime = get_fhdr_stamp_ts(fname); /* this will get the time of the file created */

    return brc_unread_time(bid, ftime, modified);
}

/*
 *  從 ftime 該篇文章開始, 往上或往下尋找第一篇已讀過的文章
 *    forward == 1: 往更新的文章找 (往下找)
 *
 *  假設: blist[] 是按照 .create 由大而小往後排序好的.
 *        blist[] 最後一個是 .create = 1 (see brc_read_record())
 *
 *  Return: -1 -- 沒有任何已讀文章的紀錄
 *           0 -- 找不到任何已讀的文章
 *           1 -- 找到已讀文章, 並將該文章放在 result 中傳回.
 */
int
brc_search_read(int bid, time4_t ftime, int forward, time4_t *result)
{
    int i;
    int	bnum;
    const brc_rec *blist;

    brc_initialize();

    if (brc_currbid && bid == brc_currbid) {
	blist = brc_list;
	bnum = brc_num;
    } else {
	blist = brc_find_record(bid, &bnum);
    }

    if( bnum == 0 || !blist ||
        ( bnum == 1 && blist[0].create == 1 ) ) {
        // 本版沒有已讀文章紀錄 (有可能是太久沒看, 已讀文章列表被丟棄, 或是
        // 從來就沒有進來過這個版 ...).
        return -1;
    }

    // [0].create is the biggest.
    // 首先要找到 ftime 所在的區間, 然後再視 forward 的值來取前後的已讀文章.
    for( i = 0; i < bnum; i++ ) { /* using linear search */
	if( time4_gt(ftime, blist[i].create) ) {
	    if( forward ) {
                if( i ) {
                    goto return_newer;
                } else {
                    return 0;
                }
            } else {
                // 回傳此篇為已讀文章
                if( result ) *result = blist[i].create;
                return 1;
            }
        }
        // 游標所在的檔案本身就是已讀過
        else if( ftime == blist[i].create ) {
            if( forward && i ) {
                goto return_newer;
            } else if( !forward && (i + 1) < bnum ) {
                goto return_older;
            }
            return 0;
        }
    }
    // 區間落在最後一個的後面 (更早之前的文章)
    if ( forward && i ) {
        goto return_newer;
    }
    return 0;

return_newer:
    // 回傳後一篇已讀文章
    if( result ) *result = blist[i - 1].create;
    return 1;

return_older:
    // 回傳前一篇已讀文章
    if( result ) *result = blist[i + 1].create;
    return 1;
}

