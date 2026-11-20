
#include "bbs.h"
#include "activity.h"

#ifdef _BBS_UTIL_C_
#error sorry, mbbsd/record.c does not support utility mode anymore. please use libcmbbs or libcmsys instead.
#endif

#define BUFSIZE 512

int
get_sum_records(const char *fpath, int size)
{
    struct stat     st;
    int             ans = 0;
    FILE           *fp;
    fileheader_t    fhdr;
    char            buf[PATHLEN], *p;

    // Ptt : should avoid big loop
    if ((fp = fopen(fpath, "r"))==NULL)
	return -1;
    posix_fadvise(fileno(fp), 0, 0, POSIX_FADV_SEQUENTIAL);

    STRLCPY(buf, fpath);
    p = strrchr(buf, '/');
    assert(p);
    p++;

    while (fread(&fhdr, size, 1, fp)==1) {
	strlcpy(p, fhdr.filename, sizeof(buf) - (p - buf));
	if (stat(buf, &st) == 0 && S_ISREG(st.st_mode) && st.st_nlink == 1)
	    ans += st.st_size;
    }
    fclose(fp);
    return ans / 1024;
}

// What delete_range() removes: the records in
// [id1, id2], or (with id1 == 0) the tagged ones.
typedef struct {
    int id1, id2;
} range_t;

static int
range_selected(const fileheader_t *fh, int index, void *ctx)
{
    const range_t *r = ctx;
    if (r->id1)
        return index >= r->id1 && index <= r->id2;
    return FindTaggedItem(fh) != NULL;
}

static void
range_removed(const fileheader_t *fh, void *ctx GCC_UNUSED)
{
    RemoveTagItem(fh);
}

// Marked, pinned and digest records are kept; in mail, FILE_BOTTOM is
// FILE_MULTI and FILE_DIGEST is FILE_REPLIED.
static uint16_t
range_keep_modes(void)
{
    return currstat == RMAIL ? FILE_MARKED :
        (FILE_MARKED | FILE_BOTTOM | FILE_DIGEST);
}

int
delete_range(const char *fpath, int id1, int id2)
{
    range_t r = { id1, id2 };
    return bof_files_prune(fpath, range_keep_modes(), 0, range_selected,
                           range_removed, &r, cuser.userid);
}

void
set_safedel_fhdr(fileheader_t *fhdr, const char *newtitle)
{
    bof_files_set_tombstone(fhdr, newtitle, cuser.userid);
}

int
safe_article_delete(int ent, const fileheader_t *fhdr, const char *direct, const char *newtitle)
{
    return bof_files_tombstone(direct, ent, fhdr, newtitle, cuser.userid);
}

int
safe_article_delete_range(const char *direct, int from, int to)
{
    fileheader_t newfhdr;
    int     fd;
    char    fn[128], *ptr;

    STRLCPY(fn, direct);
    if( (ptr = rindex(fn, '/')) == NULL )
	return -1;

    ++ptr;
    if( (fd = open(direct, O_RDWR)) != -1) {
	if (lseek(fd, sizeof(fileheader_t) * (from - 1), SEEK_SET) == -1 ){
	    close(fd);
	    return -1;
	}

	for( ; from <= to ; ++from ){
	    // the (from, to) range may be invalid...
	    if (read_fileheader(fd, &newfhdr) != 1)
		break;
	    if (!is_valid_fileheader(&newfhdr))
		continue;
	    if( newfhdr.filemode & (FILE_MARKED | FILE_DIGEST | FILE_BOTTOM) )
		continue;
	    if(newfhdr.filename[0]=='L') newfhdr.filename[0]='M';
	    strlcpy(ptr, newfhdr.filename, sizeof(newfhdr.filename));
	    unlink(fn);

	    set_safedel_fhdr(&newfhdr, NULL);
	    // because off_t is unsigned, we could NOT seek backward.
	    lseek(fd, sizeof(fileheader_t) * (from - 1), SEEK_SET);
	    write_fileheader(fd, &newfhdr);
	}
	close(fd);
	return 0;
    }
    return -1;
}

// Delete and archive the physical (except header) of file
//
// if backup_direct is provided, backup according to that directory.
// if backup_path points to buffer, return back the backuped file
// Return -1 if cannot delete file, 0 for success,
// 1 if delete success but backup failed.
int
delete_file_content(const char *direct, const fileheader_t *fh,
                    const char *backup_direct,
                    char *backup_path, size_t sz_backup_path) {
    return bof_files_delete_content(direct, fh, backup_direct, backup_path,
                                    sz_backup_path, NULL, cuser.userid,
                                    fromhost);
}

// XXX announce(man) directory uses a smaller range of file names.
// TODO merge with common/bbs/fhdr_stamp.c
int
stampadir(char *fpath, fileheader_t * fh, int large_set)
{
    register char  *ip = fpath;
    time4_t         dtime = COMMON_TIME;
    const int	    mask_small = 0xFFF,	    // basic (4096)
		    mask_large = 0xFFFF;    // large (65536)
    int		    mask = mask_small;
    int retries = 0;

    // try to create root path
    if (access(fpath, X_OK | R_OK | W_OK) != 0)
	Mkdir(fpath);

    // find tail
    while (*(++ip));
    *ip++ = '/';

    // loop to create file
    memset(fh, 0, sizeof(fileheader_t));
    do {
	if (++retries > mask_small && mask == mask_small)
	{
	    if (!large_set)
		return -1;
	    mask = mask_large;
	}
	if (retries > mask_large)
	    return -1;

	// create minimal length file name.
	sprintf(ip, "D%X", (unsigned)(time4_t)++dtime & mask);

    } while (Mkdir(fpath) == -1);

    STRLCPY(fh->filename, ip);
    fhdr_set_date(fh, dtime);

    return 0;
}

// Appends record to the mailbox fpath of origid, and sends it to its auto
// forward address. Unless `checked` (doforward already asked), the activity
// service is asked first; if it denies, the mail stays in the mailbox only.
int
append_record_forward(char *fpath, fileheader_t * record, int size,
                      const char *origid, int checked)
{
    FILE *fp;
    char address[64] = "";
    char fwd_title[STRLEN] = "";
    int r;

    // No matter what, append it, and return if that failed.
    (void)size;
    r = append_fileheader(fpath, record);
    if (r < 0)
        return r;

// #ifdef USE_MAIL_AUTO_FORWARD

    if (strlen(fpath) + strlen(FN_FORWARD) >= PATHLEN) {
        log_filef("log/invalid_append_record_forward",
                  "%s %s\n", cuser.userid, fpath);
        return -1;
    }
    AUTO_DIRPATH(fwd_path, fpath, FN_FORWARD);
    fp = fopen(fwd_path, "r");
    if (!fp)
        return 0;

    // Load and setup address
    address[0] = 0;
    fscanf(fp, "%63s", address);
    fclose(fp);
    chomp(address);
    strip_blank(address, address);



    if (!*address ||
        strchr(address, '@') == NULL ||
        strcasestr(address, str_mail_address)) {
        unlink(fwd_path);
        LOG_IF(LOG_CONF_INTERNETMAIL,
               log_filef("log/internet_mail.log",
                         "[%s] Removed bad address: %s (%s)\n",
                         __FUNCTION__,
                         address, origid));
        return 0;
    }

    // because too many user set wrong forward address,
    // let's put their own address instead.
    // and again because some really stupid user
    // does not understand they've set auto-forward,
    // let's mark this in the title.
    if (!checked &&
        activity_forward(cuser.userid, cuser.firstlogin, fromhost,
                         get_client_code(), HasUserPerm(ACTIVITY_EXEMPT_PERMS),
                         0, record->filename,
                         strcasecmp(origid, cuser.userid) == 0 ?
                         ACTIVITY_FWD_OWN : ACTIVITY_FWD_OTHER,
                         address, origid,
                         dasht(TEMP_DIRPATH(fpath, record->filename)),
                         NULL, 0)) {
        LOG_IF(LOG_CONF_INTERNETMAIL,
               log_filef("log/internet_mail.log",
                         "[%s] %s -> (%s) %s: denied by activity: %s\n",
                         __FUNCTION__,
                         cuser.userid, origid, address, record->title));
        return 0;
    }

    snprintf(fwd_title, sizeof(fwd_title)-1,
             "[¦Û°ÊÂà±H] %s", record->title);
    bsmtp(TEMP_DIRPATH(fpath, record->filename), fwd_title, address, origid);
    LOG_IF(LOG_CONF_INTERNETMAIL,
           log_filef("log/internet_mail.log",
                     "[%s] %s -> (%s) %s: %s\n",
                     __FUNCTION__,
                     cuser.userid, origid, address, fwd_title));
// #endif // USE_MAIL_AUTO_FORWARD
    return 0;
}

void
setaidfile(char *buf, const char *bn, aidu_t aidu)
{
    // try to load by AID
    int bid = 0;
    int n = 0, fd = 0;
    boardheader_t  *bh = NULL;
    fileheader_t fh;

    buf[0] = 0;
    bid = getbnum(bn);

    if (bid <= 0) return;
    assert(0<=bid-1 && bid-1<MAX_BOARD);
    bh = getbcache(bid);
    if (!HasBoardPerm(bh)) return;

    AUTO_BFILE(bfpath, bh->brdname, FN_DIR);
    n = search_aidu(bfpath, aidu, SEARCH_SRC_MBBSD_LUA);

    if (n < 0) return;
    fd = open(bfpath, O_RDONLY);
    if (fd < 0) return;

    lseek(fd, n*sizeof(fileheader_t), SEEK_SET);
    memset(&fh, 0, sizeof(fh));
    if (read(fd, &fh, sizeof(fh)) > 0)
    {
	setbfile(buf, bh->brdname, fh.filename);
	if (!dashf(buf))
	    buf[0] = 0;
    }
    close(fd);
}
