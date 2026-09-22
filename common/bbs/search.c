#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
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
	     mbs_strcasehash(pred->keyword));
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
	return mbs_strcasestr(fh->owner, keyword) != NULL;
    else if (sr_mode & RS_KEYWORD)
	return mbs_strcasestr(fh->title, keyword) != NULL;
    else if (sr_mode & RS_KEYWORD_EXCLUDE)
	return mbs_strcasestr(fh->title, keyword) == NULL;
    else if (sr_mode & RS_TITLE)
	return strcasecmp(subject(fh->title), keyword) == 0;
    else if (sr_mode & RS_RECOMMEND)
	return pred->recommend > 0 ?
	    (fh->recommend >= pred->recommend) :
	    (fh->recommend <= pred->recommend);
    else if (sr_mode & RS_MONEY) {
	if (fh->filemode & INVALIDMONEY_MODES ||
	    fh->multi.money > MAX_POST_MONEY) {
	    return 0;
	}
	return fh->multi.money >= pred->money;
    }
    return 0;
}

typedef struct {
    uint32_t magic;
    int32_t  bid;
    int32_t  offset;
    int32_t  limit;
    int32_t  num_preds;
    char     direct[256];
} PACKSTRUCT search_svc_req_hdr_t;

typedef struct {
    int32_t status;
    int32_t total;
    int32_t count;
} PACKSTRUCT search_svc_resp_hdr_t;

typedef struct {
    uint32_t magic;
    int32_t  bid;
    char     direct[256];
} PACKSTRUCT search_inval_req_hdr_t;

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0  /* SO_NOSIGPIPE is set by toconnect3() instead. */
#endif

/* Seconds to wait for search.svc before falling back to local scans. */
#define SEARCH_SVC_IO_TIMEOUT 2

/*
 * Connect to search.svc with timeout; a hung service must not freeze mbbsd.
 */
int
search_svc_connect(void)
{
    char sock_path[PATHLEN];
    snprintf(sock_path, sizeof(sock_path), "%s/run/search.svc.sock", BBSHOME);

    int sfd = toconnect_timed(sock_path, SEARCH_SVC_IO_TIMEOUT, 0, SEARCH_SVC_IO_TIMEOUT);
    return sfd;
}

/*
 * Read or write exactly len bytes. Unlike toread()/towrite(), a timeout
 * (EAGAIN) is a failure and writes never raise SIGPIPE.
 * Returns len on success, -1 on error, timeout or EOF.
 */
int
search_svc_io(int fd, void *buf, size_t len, int is_write)
{
    char *p = (char *)buf;
    size_t left = len;

    while (left > 0) {
        ssize_t n = is_write ? send(fd, p, left, MSG_NOSIGNAL) :
                               recv(fd, p, left, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        p += n;
        left -= (size_t)n;
    }
    return (int)len;
}

int
search_svc_invalidate(const char *direct, int bid)
{
    int sfd = search_svc_connect();
    if (sfd < 0)
        return -1;

    search_inval_req_hdr_t req;
    memset(&req, 0, sizeof(req));
    req.magic = SEARCH_INVAL_MAGIC;
    req.bid = bid;
    if (direct)
        strlcpy(req.direct, direct, sizeof(req.direct));

    if (search_svc_io(sfd, &req, sizeof(req), 1) != (int)sizeof(req)) {
        close(sfd);
        return -1;
    }

    int32_t status = -1;
    (void)search_svc_io(sfd, &status, sizeof(status), 0);
    close(sfd);
    return status == 0 ? 0 : -1;
}

int
search_predicates_local(const char *direct,
                        const fileheader_predicate_t *preds, int num_preds,
                        int offset, int limit,
                        int32_t *out_indices, int *out_total)
{
    int fd;
    if (!direct || num_preds <= 0 || !preds)
        return -1;
    if ((fd = open(direct, O_RDONLY)) < 0)
        return -1;

    fileheader_t fhs[8192 / sizeof(fileheader_t)];
    int len;
    int32_t recno = 0;
    int total = 0;
    int count = 0;

    if (offset < 0)
        offset = 0;
    if (limit < 0)
        limit = 0;

    while ((len = read(fd, fhs, sizeof(fhs))) > 0) {
        int n = len / (int)sizeof(fileheader_t);
        if (NEED_STORAGE_CONV)
            fileheader_storage_to_mem(fhs, n);
        for (int i = 0; i < n; i++) {
            recno++;
            /* Skip soft-deleted records */
            if (!fhs[i].filename[0] || fhs[i].filename[0] == '.' || fhs[i].owner[0] == '-')
                continue;
            int matched = 1;
            for (int p = 0; p < num_preds; p++) {
                if (!match_fileheader_predicate(&fhs[i], (void *)&preds[p])) {
                    matched = 0;
                    break;
                }
            }
            if (!matched)
                continue;
            if (total >= offset && count < limit && out_indices) {
                out_indices[count++] = recno;
            }
            total++;
        }
    }
    close(fd);
    if (out_total)
        *out_total = total;
    return count;
}

static int
search_predicates_via_svc(const char *direct, int bid,
                          const fileheader_predicate_t *preds, int num_preds,
                          int offset, int limit,
                          int32_t *out_indices, int *out_total)
{
    if (!direct || num_preds <= 0 || num_preds > MAX_SEARCH_PREDICATES)
        return -1;

    int sfd = search_svc_connect();
    if (sfd < 0)
        return -1;

    search_svc_req_hdr_t req;
    memset(&req, 0, sizeof(req));
    req.magic = SEARCH_SVC_MAGIC;
    req.bid = bid;
    req.offset = offset;
    req.limit = out_indices ? limit : 0;
    req.num_preds = num_preds;
    strlcpy(req.direct, direct, sizeof(req.direct));

    fileheader_predicate_t svc_preds[MAX_SEARCH_PREDICATES];
    const fileheader_predicate_t *send_preds = preds;
    if (NEED_STORAGE_CONV) {
        memcpy(svc_preds, preds, (size_t)num_preds * sizeof(fileheader_predicate_t));
        for (int i = 0; i < num_preds; i++) {
            mb_to_storage(preds[i].keyword, svc_preds[i].keyword,
                          sizeof(svc_preds[i].keyword));
        }
        send_preds = svc_preds;
    }

    size_t preds_bytes = (size_t)num_preds * sizeof(fileheader_predicate_t);
    if (search_svc_io(sfd, &req, sizeof(req), 1) != (int)sizeof(req) ||
        search_svc_io(sfd, (void *)send_preds, preds_bytes, 1) != (int)preds_bytes) {
        close(sfd);
        return -1;
    }

    search_svc_resp_hdr_t resp;
    if (search_svc_io(sfd, &resp, sizeof(resp), 0) != (int)sizeof(resp) ||
        resp.status != 0 || resp.count < 0 || (out_indices && resp.count > limit)) {
        close(sfd);
        return -1;
    }

    if (resp.count > 0) {
        if (out_indices) {
            size_t idx_bytes = (size_t)resp.count * sizeof(int32_t);
            if (search_svc_io(sfd, out_indices, idx_bytes, 0) != (int)idx_bytes) {
                close(sfd);
                return -1;
            }
        } else {
            /* Drain socket buffer cleanly to avoid sending TCP/UNIX RST */
            int32_t discard[64];
            size_t left = (size_t)resp.count * sizeof(int32_t);
            while (left > 0) {
                size_t to_read = left < sizeof(discard) ? left : sizeof(discard);
                if (search_svc_io(sfd, discard, to_read, 0) != (int)to_read) {
                    close(sfd);
                    return -1;
                }
                left -= to_read;
            }
        }
    }
    close(sfd);
    if (out_total)
        *out_total = resp.total;
    return resp.count;
}

int
search_predicates_window(const char *direct, int bid,
                         const fileheader_predicate_t *preds, int num_preds,
                         int offset, int limit,
                         int32_t *out_indices, int *out_total)
{
    int ret = search_predicates_via_svc(direct, bid, preds, num_preds,
                                        offset, limit, out_indices, out_total);
    if (ret >= 0)
        return ret;
    return search_predicates_local(direct, preds, num_preds,
                                   offset, limit, out_indices, out_total);
}

static int
find_resume_point_compar(const void *key, const void *memb)
{
    const time4_t *ts = (const time4_t *) key;
    const fileheader_t *fh = (const fileheader_t *) memb;
    time4_t fts = get_fhdr_stamp_ts(fh->filename);
    return time4_cmp(*ts, fts);
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
		  int src_direct_has_reference GCC_UNUSED, time4_t resume_from,
		  int dst_count,
		  int (*match)(const fileheader_t *fh, void *arg), void *arg)
{
    int fr = -1, fd;

    if (!dashf(src_direct))
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

    if ((fd = open(dst_direct, filemode, DEFAULT_FILE_CREATE_PERM)) == -1)
	return -1;

    fileheader_t fhs[8192 / sizeof(fileheader_t)];
    int i, len;
    while ((len = get_fileheaders_keep(src_direct, fhs, (int)resume_off + 1,
                                       ARRAY_SIZE(fhs), &fr)) > 0) {
	for (i = 0; i < len; ++i) {
	    resume_off++;
	    if (!match(&fhs[i], arg))
		continue;

	    ++dst_count;
	    write_fileheader(fd, &fhs[i]);
	}
    }
    if (fr >= 0)
	close(fr);

    // Do not create black hole.
    off_t current_size = lseek(fd, 0, SEEK_CUR);
    if (current_size >= 0 && (off_t)(dst_count * sizeof(fileheader_t)) <= current_size)
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
	    if (time4_gt(bp->SRexpire, now)) // invalid expire time.
		bp->SRexpire = now;

	    if (time4_gt(bp->SRexpire, filecreate))
		filetime = 0;
	}
    }

    if (filetime == 0 || time4_diff(now, filetime) > 60*60) {
	*resume_from = 0;
	return 1;
    } else if (time4_diff(now, filetime) > 3*60) {
	*resume_from = filetime;
	return 1;
    } else {
	/* use cached data */
	*resume_from = 0;
	return 0;
    }
}
