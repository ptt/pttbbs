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

#define SEARCH_SVC_MAGIC_V2  0x53524332  /* "SRC2" in little-endian */

/* Legacy V1 header (without explicit source field) */
typedef struct {
    uint32_t magic;
    int32_t  bid;
    int32_t  offset;
    int32_t  limit;
    int32_t  num_preds;
    char     direct[256];
} PACKSTRUCT search_svc_req_hdr_v1_t;

/* V2 header with explicit source */
typedef struct {
    uint32_t magic;
    int32_t  bid;
    int32_t  offset;
    int32_t  limit;
    int32_t  num_preds;
    int32_t  source;
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

static int
search_svc_hint_send(const search_hint_req_t *req)
{
    char sock_path[PATHLEN];
    snprintf(sock_path, sizeof(sock_path), "%s/run/search.svc.sock", BBSHOME);

    int sfd = toconnect_timed(sock_path, 0, 20000, 0);
    if (sfd < 0)
        return -1;

    ssize_t n = send(sfd, req, sizeof(*req), MSG_DONTWAIT | MSG_NOSIGNAL);
    close(sfd);
    return n == (ssize_t)sizeof(*req) ? 0 : -1;
}

int
search_svc_hint_post(const char *direct, int bid, int recno, aidu_t aidu, const fileheader_t *fh)
{
    if (recno <= 0)
        return -1;
    search_hint_req_t req;
    memset(&req, 0, sizeof(req));
    req.magic = SEARCH_HINT_MAGIC;
    req.type = HINT_TYPE_POST;
    req.bid = bid;
    req.recno = recno;
    req.aidu = aidu_raw(aidu);
    if (fh)
        req.fh = *fh;
    if (direct)
        strlcpy(req.direct, direct, sizeof(req.direct));
    return search_svc_hint_send(&req);
}

int
search_svc_hint_comment(const char *direct, int bid, int recno, int recommend)
{
    if (recno <= 0)
        return -1;
    search_hint_req_t req;
    memset(&req, 0, sizeof(req));
    req.magic = SEARCH_HINT_MAGIC;
    req.type = HINT_TYPE_COMMENT;
    req.bid = bid;
    req.recno = recno;
    req.data = recommend;
    if (direct)
        strlcpy(req.direct, direct, sizeof(req.direct));
    return search_svc_hint_send(&req);
}

int
search_svc_hint_delete(const char *direct, int bid, int recno, aidu_t aidu)
{
    if (recno <= 0)
        return -1;
    search_hint_req_t req;
    memset(&req, 0, sizeof(req));
    req.magic = SEARCH_HINT_MAGIC;
    req.type = HINT_TYPE_DELETE;
    req.bid = bid;
    req.recno = recno;
    req.aidu = aidu_raw(aidu);
    if (direct)
        strlcpy(req.direct, direct, sizeof(req.direct));
    return search_svc_hint_send(&req);
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
    if (limit < 0)
        limit = 0;

    if (offset < 0) {
        /* Pass 1: count total matching records to resolve negative offset from tail */
        while ((len = read(fd, fhs, sizeof(fhs))) > 0) {
            int n = len / (int)sizeof(fileheader_t);
            if (NEED_STORAGE_CONV)
                fileheader_storage_to_mem(fhs, n);
            for (int i = 0; i < n; i++) {
                if (!fhs[i].filename[0])
                    continue;
                int matched = 1;
                for (int p = 0; p < num_preds; p++) {
                    if (!match_fileheader_predicate(&fhs[i], (void *)&preds[p])) {
                        matched = 0;
                        break;
                    }
                }
                if (matched)
                    total++;
            }
        }
        offset += total;
        if (offset < 0)
            offset = 0;
        if (lseek(fd, 0, SEEK_SET) < 0) {
            close(fd);
            return -1;
        }
        if (out_total)
            *out_total = total;
        if (limit == 0 || !out_indices) {
            close(fd);
            return 0;
        }
        total = 0;
    }

    while ((len = read(fd, fhs, sizeof(fhs))) > 0) {
        int n = len / (int)sizeof(fileheader_t);
        if (NEED_STORAGE_CONV)
            fileheader_storage_to_mem(fhs, n);
        for (int i = 0; i < n; i++) {
            recno++;
            /* Match all valid records including soft-deleted corpses */
            if (!fhs[i].filename[0])
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
                          int32_t *out_indices, int *out_total,
                          int source)
{
    if (!direct || num_preds <= 0 || num_preds > MAX_SEARCH_PREDICATES)
        return -1;

    int sfd = search_svc_connect();
    if (sfd < 0)
        return -1;

    search_svc_req_hdr_t req;
    memset(&req, 0, sizeof(req));
    req.magic = SEARCH_SVC_MAGIC_V2;
    req.bid = bid;
    req.offset = offset;
    req.limit = out_indices ? limit : 0;
    req.num_preds = num_preds;
    req.source = source;
    strlcpy(req.direct, direct, sizeof(req.direct));

    size_t preds_bytes = (size_t)num_preds * sizeof(fileheader_predicate_t);
    if (search_svc_io(sfd, &req, sizeof(req), 1) != (int)sizeof(req) ||
        search_svc_io(sfd, (void *)preds, preds_bytes, 1) != (int)preds_bytes) {
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
                         int32_t *out_indices, int *out_total,
                         int source)
{
    int ret = search_predicates_via_svc(direct, bid, preds, num_preds,
                                        offset, limit, out_indices, out_total, source);
    if (ret >= 0)
        return ret;
    return search_predicates_local(direct, preds, num_preds,
                                   offset, limit, out_indices, out_total);
}

int
search_predicates_window_legacy(const char *direct, int bid,
                                const fileheader_predicate_t *preds, int num_preds,
                                int offset, int limit,
                                int32_t *out_indices, int *out_total)
{
    return search_predicates_window(direct, bid, preds, num_preds,
                                    offset, limit, out_indices, out_total,
                                    SEARCH_SRC_UNKNOWN);
}

