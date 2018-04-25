#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include "cmbbs.h"
#include "modes.h"
#include "pttstruct.h"
#include "var.h"

void
select_read_name(char *buf, size_t size, const char *base,
		 const fileheader_predicate_t *pred)
{
    snprintf(buf, size, "%s%X.%X.%X",
	     base ? base : "SR.",
	     pred->mode, (int) strlen(pred->keyword),
	     DBCS_StringHash(pred->keyword));
}

int
match_fileheader_predicate(const fileheader_t *fh, void *arg)
{
    const fileheader_predicate_t *pred = (const fileheader_predicate_t *) arg;
    const char * const keyword = pred->keyword;
    int sr_mode = pred->mode;

    // The order does not matter. Only single sr_mode at a time.
    if (sr_mode & RS_MARK)
	return fh->filemode & FILE_MARKED;
    else if (sr_mode & RS_SOLVED)
	return fh->filemode & FILE_SOLVED;
    else if (sr_mode & RS_NEWPOST)
	return strncmp(fh->title, "Re:", 3) != 0;
    else if (sr_mode & RS_AUTHOR)
	return DBCS_strcasestr(fh->owner, keyword) != NULL;
    else if (sr_mode & RS_KEYWORD)
	return DBCS_strcasestr(fh->title, keyword) != NULL;
    else if (sr_mode & RS_KEYWORD_EXCLUDE)
	return DBCS_strcasestr(fh->title, keyword) == NULL;
    else if (sr_mode & RS_TITLE)
	return strcasecmp(subject(fh->title), keyword) == 0;
    else if (sr_mode & RS_RECOMMEND)
	return pred->recommend > 0 ?
	    (fh->recommend >= pred->recommend) :
	    (fh->recommend <= pred->recommend);
    else if (sr_mode & RS_MONEY) {
	// We don't know the money if a ref is instead stored.
	// This can happen in DIR generated after select, bottom DIR, etc.
	if (fh->multi.refer.flag || fh->filemode & INVALIDMONEY_MODES ||
	    fh->multi.money > MAX_POST_MONEY) {
	    return 0;
	}
	return fh->multi.money >= pred->money;
    }
    return 0;
}

static int
find_resume_point_compar(const void *key, const void *memb)
{
    const time4_t *ts = (const time4_t *) key;
    const fileheader_t *fh = (const fileheader_t *) memb;
    return *ts - atoi(fh->filename + 2);
}

static size_t
find_resume_point(const char *direct, time4_t timestamp)
{
    fileheader_t fh;
    size_t num;
    ssize_t index = upper_bound_record(
        direct, &timestamp, find_resume_point_compar, sizeof(fh), &fh, &num);
    return index < 0 ? 0 : index;
}

int
select_read_build(const char *src_direct, const char *dst_direct,
		  int src_direct_has_reference, time4_t resume_from,
		  int dst_count,
		  int (*match)(const fileheader_t *fh, void *arg), void *arg)
{
    int fr, fd;

    if ((fr = open(src_direct, O_RDONLY, 0)) < 0)
	return -1;

    // Find incremental selection start point.
    size_t resume_off = resume_from ?
	find_resume_point(src_direct, resume_from) : 0;

    int filemode;
    if (resume_off) {
	filemode = O_APPEND | O_RDWR;
    } else {
	filemode = O_CREAT | O_RDWR;
	dst_count = 0;
    }

    if ((fd = open(dst_direct, filemode, DEFAULT_FILE_CREATE_PERM)) == -1) {
	close(fr);
	return -1;
    }

    if (resume_off > 0)
	lseek(fr, resume_off * sizeof(fileheader_t), SEEK_SET);

    fileheader_t fhs[8192 / sizeof(fileheader_t)];
    int i, len;
    while ((len = read(fr, fhs, sizeof(fhs))) > 0) {
	len /= sizeof(fileheader_t);
	for (i = 0; i < len; ++i) {
	    resume_off++;
	    if (!match(&fhs[i], arg))
		continue;

	    if (!src_direct_has_reference) {
		fhs[i].multi.refer.flag = 1;
		fhs[i].multi.refer.ref = resume_off; // 1-based.
	    }
	    ++dst_count;
	    write(fd, &fhs[i], sizeof(fileheader_t));
	}
    }
    close(fr);

    // Do not create black hole.
    off_t current_size = lseek(fd, 0, SEEK_CUR);
    if (current_size >= 0 && dst_count * sizeof(fileheader_t) <= current_size)
	ftruncate(fd, dst_count * sizeof(fileheader_t));
    close(fd);

    return dst_count;
}

int
select_read_should_build(const char *dst_direct, int bid, time4_t *resume_from,
			 int *count)
{
    struct stat st;
    if (stat(dst_direct, &st) < 0)
    {
	*resume_from = 0;
	*count = 0;
	return 1;
    }

    *count = st.st_size / sizeof(fileheader_t);

    time4_t filetime = st.st_mtime;

    if (bid > 0)
    {
	time4_t filecreate = st.st_ctime;
	boardheader_t *bp = getbcache(bid);
	assert(bp);

	if (bp->SRexpire)
	{
	    if (bp->SRexpire > now) // invalid expire time.
		bp->SRexpire = now;

	    if (bp->SRexpire > filecreate)
		filetime = -1;
	}
    }

    if (filetime < 0 || now-filetime > 60*60) {
	*resume_from = 0;
	return 1;
    } else if (now-filetime > 3*60) {
	*resume_from = filetime;
	return 1;
    } else {
	/* use cached data */
	*resume_from = 0;
	return 0;
    }
}
