package daemon

/*
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cmbbs.h"
#include "cmsys.h"
#include "modes.h"

extern SHM_t *SHM;

static int c_shm_ready(void) {
    return SHM != NULL;
}

// Forces NUL termination of every predicate keyword received from clients.
static void c_sanitize_preds(void *buf, int n) {
    fileheader_predicate_t *p = (fileheader_predicate_t *)buf;
    for (int i = 0; i < n; i++)
        p[i].keyword[sizeof(p[i].keyword) - 1] = '\0';
}

static void c_format_preds(const void *buf, int n, char *out, int outlen) {
    const fileheader_predicate_t *p = (const fileheader_predicate_t *)buf;
    out[0] = '\0';
    for (int i = 0; i < n; i++) {
        char item[128];
        const char *mstr = "kw";
        if (p[i].mode & RS_AUTHOR) mstr = "author";
        else if (p[i].mode & RS_TITLE) mstr = "title";
        else if (p[i].mode & RS_RECOMMEND) mstr = "push";
        else if (p[i].mode & RS_MARK) mstr = "mark";
        else if (p[i].mode & RS_MONEY) mstr = "money";
        snprintf(item, sizeof(item), "%s%s=%s", (i > 0 ? " " : ""), mstr, p[i].keyword);
        strlcat(out, item, outlen);
    }
}

static int c_pred_size(void) {
    return (int)sizeof(fileheader_predicate_t);
}

static int c_fhdr_size(void) {
    return (int)sizeof(fileheader_t);
}

enum {
    FHDR_OFF_RECOMMEND = offsetof(fileheader_t, recommend),
    FHDR_OFF_OWNER     = offsetof(fileheader_t, owner),
    FHDR_OFF_FILEMODE  = offsetof(fileheader_t, filemode),
};

static int64_t c_get_board_srexpire(int bid) {
    if (SHM == NULL || bid < 1 || bid > MAX_BOARD)
        return 0;
    return (int64_t)SHM->bcache[bid - 1].SRexpire;
}

static const char *c_get_board_name(int bid) {
    if (SHM == NULL || bid < 1 || bid > MAX_BOARD)
        return "";
    return SHM->bcache[bid - 1].brdname;
}

static int c_get_board_bid(const char *name) {
    if (SHM == NULL || !name || !*name)
        return 0;
    return getbnum(name);
}

// Reads the filename of 1-based record recno; used as a tail anchor to detect
// records shifted by physical deletion.
static int c_read_filename_at(const char *direct, int recno, char *out, int outlen) {
    fileheader_t fh;
    if (recno < 1 || outlen <= 0)
        return -1;
    int fd = open(direct, O_RDONLY);
    if (fd < 0)
        return -1;
    ssize_t n = pread(fd, &fh, sizeof(fh), (off_t)(recno - 1) * sizeof(fh));
    close(fd);
    if (n != (ssize_t)sizeof(fh))
        return -1;
    strlcpy(out, fh.filename, outlen);
    return 0;
}

static void c_fill_predicate(void *buf, int mode, const char *kw, int recommend, int money) {
    fileheader_predicate_t *p = (fileheader_predicate_t *)buf;
    memset(p, 0, sizeof(*p));
    p->mode = mode;
    if (kw)
        strlcpy(p->keyword, kw, sizeof(p->keyword));
    p->recommend = recommend;
    p->money = money;
}

static int c_append_fileheader(const char *direct, const char *filename,
                               const char *owner, const char *title,
                               int filemode, int recommend, int money)
{
    fileheader_t fh;
    memset(&fh, 0, sizeof(fh));
    if (filename)
        strlcpy(fh.filename, filename, sizeof(fh.filename));
    if (owner)
        strlcpy(fh.owner, owner, sizeof(fh.owner));
    if (title)
        strlcpy(fh.title, title, sizeof(fh.title));
    fh.filemode = (uint8_t)filemode;
    fh.recommend = (int8_t)recommend;
    fh.multi.money = money;
    return append_fileheader(direct, &fh);
}

static int c_delete_fileheader(const char *direct, const char *filename, int id)
{
    fileheader_t fh;
    memset(&fh, 0, sizeof(fh));
    if (filename)
        strlcpy(fh.filename, filename, sizeof(fh.filename));
    return delete_fileheader(direct, &fh, id);
}

static int c_scan_dir_range(const char *direct,
                            const void *preds_buf, int num_preds,
                            int start_rec,
                            int32_t *out_indices, int max_out,
                            int *out_total_recs)
{
    int fd = open(direct, O_RDONLY);
    if (fd < 0)
        return -1;

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return -1;
    }

#ifdef POSIX_FADV_SEQUENTIAL
    posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif

    int total_recs = (int)(st.st_size / sizeof(fileheader_t));

    if (start_rec < 1)
        start_rec = 1;
    int max_end_rec = start_rec + max_out - 1;
    if (total_recs > max_end_rec)
        total_recs = max_end_rec;
    if (out_total_recs)
        *out_total_recs = total_recs;

    if (start_rec > total_recs) {
        close(fd);
        return 0;
    }

    if (lseek(fd, (off_t)(start_rec - 1) * sizeof(fileheader_t), SEEK_SET) < 0) {
        close(fd);
        return -1;
    }

    const fileheader_predicate_t *preds = (const fileheader_predicate_t *)preds_buf;
    enum { SCAN_BATCH = 1024 };
    fileheader_t fhs[SCAN_BATCH];
    int recno = start_rec - 1;
    int matched_count = 0;
    ssize_t len;

    while (recno < total_recs && (len = read(fd, fhs, sizeof(fhs))) > 0) {
        int n = (int)(len / sizeof(fileheader_t));
        if (NEED_STORAGE_CONV)
            fileheader_storage_to_mem(fhs, n);
        for (int i = 0; i < n && recno < total_recs; i++) {
            recno++;
            if (!fhs[i].filename[0] || fhs[i].filename[0] == '.' || fhs[i].owner[0] == '-')
                continue;
            int ok = 1;
            for (int p = 0; p < num_preds; p++) {
                if (!match_fileheader_predicate(&fhs[i], (void *)&preds[p])) {
                    ok = 0;
                    break;
                }
            }
            if (ok) {
                if (matched_count < max_out)
                    out_indices[matched_count] = recno;
                matched_count++;
            }
        }
    }

    close(fd);
    return matched_count;
}

static int c_filter_candidates(const char *direct,
                               const void *preds_buf, int num_preds,
                               const int32_t *cand_indices, int num_cands,
                               int32_t *out_indices)
{
    if (num_cands <= 0)
        return 0;

    int fd = open(direct, O_RDONLY);
    if (fd < 0)
        return -1;

    const fileheader_predicate_t *preds = (const fileheader_predicate_t *)preds_buf;
    fileheader_t fh;
    int matched_count = 0;

    for (int i = 0; i < num_cands; i++) {
        int32_t recno = cand_indices[i];
        if (recno < 1)
            continue;
        if (pread(fd, &fh, sizeof(fh), (off_t)(recno - 1) * sizeof(fh)) != (ssize_t)sizeof(fh))
            continue;
        if (NEED_STORAGE_CONV)
            fileheader_storage_to_mem(&fh, 1);
        if (!fh.filename[0] || fh.filename[0] == '.' || fh.owner[0] == '-')
            continue;
        int ok = 1;
        for (int p = 0; p < num_preds; p++) {
            if (!match_fileheader_predicate(&fh, (void *)&preds[p])) {
                ok = 0;
                break;
            }
        }
        if (ok)
            out_indices[matched_count++] = recno;
    }

    close(fd);
    return matched_count;
}

static const char *c_get_userid_by_pid(pid_t pid) {
    if (SHM == NULL || pid <= 0)
        return "";
    for (int i = 0; i < USHM_SIZE; i++) {
        if (SHM->uinfo[i].pid == pid && SHM->uinfo[i].userid[0] != '\0')
            return SHM->uinfo[i].userid;
    }
    return "";
}

static int c_compute_dir_backtrack(const char *direct, int *out_total_recs, int64_t *out_max_time_diff) {
    if (out_total_recs)
        *out_total_recs = 0;
    if (out_max_time_diff)
        *out_max_time_diff = 0;

    int fd = open(direct, O_RDONLY);
    if (fd < 0)
        return -1;

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return -1;
    }

    int total = (int)(st.st_size / sizeof(fileheader_t));
    if (out_total_recs)
        *out_total_recs = total;
    if (total <= 1) {
        close(fd);
        return 0;
    }

#ifdef POSIX_FADV_SEQUENTIAL
    posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif

    time4_t *ts_buf = (time4_t *)malloc((size_t)total * sizeof(time4_t));
    if (!ts_buf) {
        close(fd);
        return -1;
    }

    enum { BATCH = 1024 };
    fileheader_t fhs[BATCH];
    int valid_cnt = 0;
    ssize_t len;
    while ((len = read(fd, fhs, sizeof(fhs))) > 0) {
        int n = (int)(len / sizeof(fileheader_t));
        for (int i = 0; i < n && valid_cnt < total; i++) {
            if ((fhs[i].filename[0] == 'M' || fhs[i].filename[0] == 'G') && fhs[i].owner[0] != '-') {
                ts_buf[valid_cnt++] = get_fhdr_stamp_ts(fhs[i].filename);
            } else {
                ts_buf[valid_cnt++] = 0;
            }
        }
    }
    close(fd);

    if (valid_cnt <= 1) {
        free(ts_buf);
        return 0;
    }

    int *cand_idx = (int *)malloc((size_t)valid_cnt * sizeof(int));
    if (!cand_idx) {
        free(ts_buf);
        return -1;
    }

    int cand_cnt = 0;
    time4_t max_seen = 0;
    for (int i = 0; i < valid_cnt; i++) {
        time4_t t = ts_buf[i];
        if (t <= 0)
            continue;
        if (cand_cnt == 0 || t > max_seen) {
            cand_idx[cand_cnt++] = i;
            max_seen = t;
        }
    }

    int max_backtrack = 0;
    int64_t max_time_diff = 0;

    for (int j = valid_cnt - 1; j >= 0; j--) {
        time4_t tj = ts_buf[j];
        if (tj <= 0)
            continue;
        if (cand_cnt > 0 && tj < ts_buf[cand_idx[cand_cnt - 1]]) {
            int low = 0, high = cand_cnt - 1, best = cand_cnt - 1;
            while (low <= high) {
                int mid = low + (high - low) / 2;
                if (ts_buf[cand_idx[mid]] > tj) {
                    best = mid;
                    high = mid - 1;
                } else {
                    low = mid + 1;
                }
            }
            int i = cand_idx[best];
            int dist = j - i;
            if (dist > max_backtrack)
                max_backtrack = dist;
            int64_t tdiff = (int64_t)(ts_buf[cand_idx[cand_cnt - 1]] - tj);
            if (tdiff > max_time_diff)
                max_time_diff = tdiff;
        }
    }

    free(cand_idx);
    free(ts_buf);

    if (out_max_time_diff)
        *out_max_time_diff = max_time_diff;
    return max_backtrack;
}

static inline int match_fhdr_stamp_hex(const fileheader_t *fh, char prefix_ch,
                                         time4_t target_ts, unsigned int target_hex,
                                         int allow_prefix, int required_mode)
{
    if (!fh->filename[0] || fh->filename[0] == '.')
        return 0;
    if (prefix_ch && fh->filename[0] != prefix_ch &&
        !(prefix_ch == 'M' && fh->filename[0] == 'L' && (required_mode & FILE_BOTTOM)))
        return 0;
    if (required_mode) {
        if (fh->owner[0] == '-')
            return 0;
        if ((fh->filemode & required_mode) != required_mode)
            return 0;
    }
    if (get_fhdr_stamp_ts(fh->filename) != target_ts)
        return 0;
    unsigned int hex = get_fhdr_stamp_hex(fh->filename);
    if (hex == target_hex) {
        if (target_hex != 0 || allow_prefix)
            return 1;
        const char *dot = strrchr(fh->filename, '.');
        return (dot && (dot[1] == '0' || (dot[1] == 'A' && dot[2] == '\0')));
    }
    return 0;
}

static int c_search_aidu_and_compute_backtrack(
    int fd, int total, uint64_t aidu, int required_mode,
    char prefix_ch, time4_t target_ts, unsigned int target_hex, int allow_prefix,
    fileheader_t *out_fh, int *out_backtrack, int64_t *out_max_time_diff)
{
    if (out_backtrack)
        *out_backtrack = 0;
    if (out_max_time_diff)
        *out_max_time_diff = 0;

    if (total <= 0)
        return 0;

#ifdef POSIX_FADV_SEQUENTIAL
    posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif

    time4_t *ts_buf = (time4_t *)malloc((size_t)total * sizeof(time4_t));
    if (!ts_buf)
        return -1;

    enum { SCAN_BATCH = 1024 };
    fileheader_t fhs[SCAN_BATCH];
    int valid_cnt = 0;
    int found_rec = 0;
    int recno = 0;
    ssize_t len;

    if (lseek(fd, 0, SEEK_SET) < 0) {
        free(ts_buf);
        return -1;
    }

    while (recno < total && (len = read(fd, fhs, sizeof(fhs))) > 0) {
        int n = (int)(len / sizeof(fileheader_t));
        if (NEED_STORAGE_CONV)
            fileheader_storage_to_mem(fhs, n);

        for (int i = 0; i < n && recno < total; i++) {
            recno++;
            fileheader_t *cur = &fhs[i];
            if (!cur->filename[0] || cur->filename[0] == '.' || cur->owner[0] == '-') {
                ts_buf[valid_cnt++] = 0;
                continue;
            }

            if (!found_rec) {
                if (match_fhdr_stamp_hex(cur, prefix_ch, target_ts, target_hex, allow_prefix, required_mode)) {
                    found_rec = recno;
                    if (out_fh)
                        *out_fh = *cur;
                }
            }

            if (cur->filename[0] == 'M' || cur->filename[0] == 'G') {
                ts_buf[valid_cnt++] = get_fhdr_stamp_ts(cur->filename);
            } else {
                ts_buf[valid_cnt++] = 0;
            }
        }
    }

    int max_btrack = 0;
    int64_t max_tdiff = 0;

    if (valid_cnt > 1) {
        int *cand_idx = (int *)malloc((size_t)valid_cnt * sizeof(int));
        if (cand_idx) {
            int cand_cnt = 0;
            time4_t max_seen = 0;
            for (int i = 0; i < valid_cnt; i++) {
                time4_t t = ts_buf[i];
                if (t <= 0)
                    continue;
                if (cand_cnt == 0 || t > max_seen) {
                    cand_idx[cand_cnt++] = i;
                    max_seen = t;
                }
            }

            for (int j = valid_cnt - 1; j >= 0; j--) {
                time4_t tj = ts_buf[j];
                if (tj <= 0)
                    continue;
                if (cand_cnt > 0 && tj < ts_buf[cand_idx[cand_cnt - 1]]) {
                    int low = 0, high = cand_cnt - 1, best = cand_cnt - 1;
                    while (low <= high) {
                        int mid = low + (high - low) / 2;
                        if (ts_buf[cand_idx[mid]] > tj) {
                            best = mid;
                            high = mid - 1;
                        } else {
                            low = mid + 1;
                        }
                    }
                    int i = cand_idx[best];
                    int dist = j - i;
                    if (dist > max_btrack)
                        max_btrack = dist;
                    int64_t tdiff = (int64_t)(ts_buf[cand_idx[cand_cnt - 1]] - tj);
                    if (tdiff > max_tdiff)
                        max_tdiff = tdiff;
                }
            }
            free(cand_idx);
        }
    }

    free(ts_buf);

    if (out_backtrack)
        *out_backtrack = max_btrack;
    if (out_max_time_diff)
        *out_max_time_diff = max_tdiff;

    return found_rec;
}

static int c_read_fh_at(const char *direct, int rec, void *out_fh) {
    if (rec < 1 || !out_fh)
        return -1;
    int fd = open(direct, O_RDONLY);
    if (fd < 0)
        return -1;
    fileheader_t fh;
    ssize_t n = pread(fd, &fh, sizeof(fh), (off_t)(rec - 1) * sizeof(fh));
    close(fd);
    if (n != (ssize_t)sizeof(fh))
        return -1;
    if (NEED_STORAGE_CONV)
        fileheader_storage_to_mem(&fh, 1);
    memcpy(out_fh, &fh, sizeof(fh));
    return 0;
}

static int c_load_dir_aids(const char *direct, uint64_t *out_aids, int max_recs) {
    int fd = open(direct, O_RDONLY);
    if (fd < 0)
        return -1;
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return -1;
    }
    int total = (int)(st.st_size / sizeof(fileheader_t));
    if (total > max_recs)
        total = max_recs;
    if (total <= 0) {
        close(fd);
        return 0;
    }
#ifdef POSIX_FADV_SEQUENTIAL
    posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
    enum { BATCH = 1024 };
    fileheader_t fhs[BATCH];
    int recno = 0;
    ssize_t len;
    while (recno < total && (len = read(fd, fhs, sizeof(fhs))) > 0) {
        int n = (int)(len / sizeof(fileheader_t));
        for (int i = 0; i < n && recno < total; i++) {
            if ((fhs[i].filename[0] == 'M' || fhs[i].filename[0] == 'G') && fhs[i].owner[0] != '-') {
                out_aids[recno] = (uint64_t)fn2aidu(fhs[i].filename) & 0x00001FFFFFFFFFFFULL;
            } else {
                out_aids[recno] = 0;
            }
            recno++;
        }
    }
    close(fd);
    return recno;
}

static int c_search_aidu_in_dir(const char *direct, uint64_t aidu, int required_mode,
                                int start_rec, void *out_fh, int *out_total_recs,
                                int max_backtrack, int *out_computed_backtrack,
                                int64_t *out_computed_time_diff)
{
    if (out_computed_backtrack)
        *out_computed_backtrack = -1;
    if (out_computed_time_diff)
        *out_computed_time_diff = 0;

    int fd = open(direct, O_RDONLY);
    if (fd < 0)
        return -1;

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return -1;
    }

    int total_recs = (int)(st.st_size / sizeof(fileheader_t));
    if (out_total_recs)
        *out_total_recs = total_recs;
    if (total_recs <= 0) {
        close(fd);
        return 0;
    }

    fileheader_t fh;
    memset(&fh, 0, sizeof(fh));

    if (start_rec > 1) {
        // Tail scan only [start_rec .. total_recs] (used when negative cache entry exists and .DIR grew)
        char prefix_ch = aidu_type((aidu_t)aidu) ? 'G' : 'M';
        time4_t target_ts = aidu_stamp(aidu);
        unsigned int target_hex = aidu_hex(aidu);
        int allow_prefix = (target_hex == 0 && !(required_mode & FILE_BOTTOM));
        for (int rec = total_recs; rec >= start_rec; rec--) {
            if (pread(fd, &fh, sizeof(fh), (off_t)(rec - 1) * sizeof(fh)) != (ssize_t)sizeof(fh))
                continue;
            if (!fh.filename[0] || fh.filename[0] == '.' || fh.filename[0] != prefix_ch ||
                (required_mode && fh.owner[0] == '-'))
                continue;
            if (required_mode && (fh.filemode & required_mode) != required_mode)
                continue;
            if (get_fhdr_stamp_ts(fh.filename) == target_ts &&
                get_fhdr_stamp_hex(fh.filename) == target_hex) {
                if (target_hex == 0 && !allow_prefix) {
                    const char *dot = strrchr(fh.filename, '.');
                    if (!dot || (dot[1] != '0' && !(dot[1] == 'A' && dot[2] == '\0')))
                        continue;
                }
                if (out_fh) {
                    if (NEED_STORAGE_CONV)
                        fileheader_storage_to_mem(&fh, 1);
                    memcpy(out_fh, &fh, sizeof(fh));
                }
                close(fd);
                return rec;
            }
        }
        close(fd);
        return 0;
    }

    char prefix_ch = aidu_type((aidu_t)aidu) ? 'G' : 'M';
    time4_t target_ts = aidu_stamp(aidu);
    unsigned int target_hex = aidu_hex(aidu);
    int allow_prefix = (target_hex == 0 && !(required_mode & FILE_BOTTOM));

    // Fast path:
    // If max_backtrack >= 0: bounded binary search with window (max_backtrack * 2 + BSEARCH_WIN).
    // If max_backtrack < 0: try binary search with default window (BSEARCH_WIN = 256).
    int btrack_for_bsearch = (max_backtrack >= 0) ? max_backtrack : 0;
    int found = search_dir_by_aidu_fd_bounded(fd, total_recs, (aidu_t)aidu, required_mode, &fh, btrack_for_bsearch);

    if (found > 0) {
        // Fast path HIT! Article found by index, tail, or binary search window!
        // No need to touch or compute backtrack!
        if (out_fh)
            memcpy(out_fh, &fh, sizeof(fh));
        close(fd);
        return found;
    }

    // Binary search window missed!
    if (max_backtrack >= 0) {
        // We ALREADY had a known backtrack window, and it was not found in [mid - win, mid + win].
        // Article definitely does not exist on this board. FAIL immediately!
        close(fd);
        return 0;
    }

    // Binary search missed AND we do NOT have a backtrack window (max_backtrack < 0).
    // Fall back to linear scan once, find the AID, AND compute backtrack window!
    int computed_bt = 0;
    int64_t computed_tdiff = 0;
    found = c_search_aidu_and_compute_backtrack(
        fd, total_recs, aidu, required_mode,
        prefix_ch, target_ts, target_hex, allow_prefix,
        &fh, &computed_bt, &computed_tdiff);

    if (found > 0 && out_fh)
        memcpy(out_fh, &fh, sizeof(fh));

    if (out_computed_backtrack)
        *out_computed_backtrack = computed_bt;
    if (out_computed_time_diff)
        *out_computed_time_diff = computed_tdiff;

    close(fd);
    return found;
}
*/
import "C"

import (
	"encoding/binary"
	"fmt"
	"os"
	"unsafe"
)

const (
	RS_AUTHOR          = int(C.RS_AUTHOR)
	RS_TITLE           = int(C.RS_TITLE)
	RS_KEYWORD         = int(C.RS_KEYWORD)
	RS_NEWPOST         = int(C.RS_NEWPOST)
	RS_MARK            = int(C.RS_MARK)
	RS_RECOMMEND       = int(C.RS_RECOMMEND)
	RS_SOLVED          = int(C.RS_SOLVED)
	RS_MONEY           = int(C.RS_MONEY)
	RS_KEYWORD_EXCLUDE = int(C.RS_KEYWORD_EXCLUDE)
	FILE_MARKED        = int(C.FILE_MARKED)
	FILE_BOTTOM        = int(C.FILE_BOTTOM)

	FHDR_OFF_RECOMMEND = int(C.FHDR_OFF_RECOMMEND)
	FHDR_OFF_OWNER     = int(C.FHDR_OFF_OWNER)
	FHDR_OFF_FILEMODE  = int(C.FHDR_OFF_FILEMODE)
)

func PredSize() int {
	return int(C.c_pred_size())
}

func FileheaderSize() int {
	return int(C.c_fhdr_size())
}

// SHMReady reports whether SHM (and thus bcache[].SRexpire) is available.
func SHMReady() bool {
	return C.c_shm_ready() != 0
}

// SanitizePreds forces NUL termination of every predicate keyword in place.
func SanitizePreds(predsRaw []byte, numPreds int) {
	if numPreds > 0 && len(predsRaw) >= numPreds*PredSize() {
		C.c_sanitize_preds(unsafe.Pointer(&predsRaw[0]), C.int(numPreds))
	}
}

// FormatPreds returns a human-readable representation of search predicates.
func FormatPreds(predsRaw []byte, numPreds int) string {
	if numPreds <= 0 || len(predsRaw) < numPreds*PredSize() {
		return ""
	}
	var buf [256]C.char
	C.c_format_preds(unsafe.Pointer(&predsRaw[0]), C.int(numPreds), &buf[0], C.int(len(buf)))
	return C.GoString(&buf[0])
}

func GetBoardSRExpire(bid int32) int64 {
	return int64(C.c_get_board_srexpire(C.int(bid)))
}

func BoardName(bid int32) string {
	cstr := C.c_get_board_name(C.int(bid))
	return C.GoString(cstr)
}

func BoardBID(name string) int32 {
	if name == "" {
		return 0
	}
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))
	return int32(C.c_get_board_bid(cName))
}

// ReadFilenameAt returns the filename of 1-based record rec.
func ReadFilenameAt(directPath string, rec int32) (string, bool) {
	var buf [64]C.char
	cPath := C.CString(directPath)
	defer C.free(unsafe.Pointer(cPath))
	if C.c_read_filename_at(cPath, C.int(rec), &buf[0], C.int(len(buf))) != 0 {
		return "", false
	}
	return C.GoString(&buf[0]), true
}

func SetFileheaderRecommend(fh *[128]byte, recommend int) {
	if fh != nil {
		fh[FHDR_OFF_RECOMMEND] = byte(recommend)
	}
}

func FileheaderFilemode(fh *[128]byte) int {
	if fh == nil {
		return 0
	}
	return int(binary.NativeEndian.Uint16(fh[FHDR_OFF_FILEMODE:]))
}

func MatchFileheaderMode(fh *[128]byte, requiredMode int32) bool {
	if fh == nil {
		return false
	}
	if fh[0] == 0 || fh[0] == '.' {
		return false
	}
	if requiredMode != 0 && fh[FHDR_OFF_OWNER] == '-' {
		return false
	}
	if requiredMode != 0 && (FileheaderFilemode(fh)&int(requiredMode)) != int(requiredMode) {
		return false
	}
	return true
}

func FNToAIDU(fn string) uint64 {
	cFn := C.CString(fn)
	defer C.free(unsafe.Pointer(cFn))
	return uint64(C.fn2aidu(cFn))
}

func ReadFileheaderAt(directPath string, rec int32) ([128]byte, bool) {
	var buf [128]byte
	cPath := C.CString(directPath)
	defer C.free(unsafe.Pointer(cPath))
	if C.c_read_fh_at(cPath, C.int(rec), unsafe.Pointer(&buf[0])) != 0 {
		return buf, false
	}
	return buf, true
}

func LoadDirAIDs(directPath string) ([]uint64, error) {
	st, err := os.Stat(directPath)
	if err != nil {
		return nil, err
	}
	fhSize := int64(FileheaderSize())
	totalRecs := int(st.Size() / fhSize)
	if totalRecs <= 0 {
		return []uint64{}, nil
	}
	aids := make([]uint64, totalRecs)
	cPath := C.CString(directPath)
	defer C.free(unsafe.Pointer(cPath))
	n := C.c_load_dir_aids(cPath, (*C.uint64_t)(unsafe.Pointer(&aids[0])), C.int(totalRecs))
	if n < 0 {
		return nil, fmt.Errorf("failed to load AIDs from %s", directPath)
	}
	return aids[:n], nil
}

func MakePredBytes(mode int, keyword string, recommend int, money int) []byte {
	buf := make([]byte, PredSize())
	cKw := C.CString(keyword)
	defer C.free(unsafe.Pointer(cKw))
	C.c_fill_predicate(unsafe.Pointer(&buf[0]), C.int(mode), cKw, C.int(recommend), C.int(money))
	return buf
}

func AppendTestFileheader(directPath, filename, owner, title string, filemode int, recommend int, money int) error {
	cDir := C.CString(directPath)
	cFn := C.CString(filename)
	cOwn := C.CString(owner)
	cTit := C.CString(title)
	defer func() {
		C.free(unsafe.Pointer(cDir))
		C.free(unsafe.Pointer(cFn))
		C.free(unsafe.Pointer(cOwn))
		C.free(unsafe.Pointer(cTit))
	}()
	rc := C.c_append_fileheader(cDir, cFn, cOwn, cTit, C.int(filemode), C.int(recommend), C.int(money))
	if rc < 0 {
		return fmt.Errorf("append_fileheader failed on %s", directPath)
	}
	return nil
}

func DeleteTestFileheader(directPath, filename string, id int) error {
	cDir := C.CString(directPath)
	cFn := C.CString(filename)
	defer func() {
		C.free(unsafe.Pointer(cDir))
		C.free(unsafe.Pointer(cFn))
	}()
	rc := C.c_delete_fileheader(cDir, cFn, C.int(id))
	if rc != 0 {
		return fmt.Errorf("delete_fileheader failed on %s: %d", directPath, rc)
	}
	return nil
}

func UserIDByPID(pid int32) string {
	cstr := C.c_get_userid_by_pid(C.pid_t(pid))
	return C.GoString(cstr)
}

func ComputeDirBacktrack(directPath string) (int32, int32, int64) {
	cPath := C.CString(directPath)
	defer C.free(unsafe.Pointer(cPath))
	var cTotal C.int
	var cTimeDiff C.int64_t
	btrack := C.c_compute_dir_backtrack(cPath, &cTotal, &cTimeDiff)
	return int32(cTotal), int32(btrack), int64(cTimeDiff)
}

func SearchAIDInDir(directPath string, aidu uint64, requiredMode int, startRec int32, maxBacktrack int) (int32, [128]byte, int32, int32, int64, error) {
	var fhBuf [128]byte
	var totalRecs C.int
	var cComputedBacktrack C.int = -1
	var cComputedTimeDiff C.int64_t = 0
	cPath := C.CString(directPath)
	defer C.free(unsafe.Pointer(cPath))

	found := int(C.c_search_aidu_in_dir(
		cPath,
		C.uint64_t(aidu),
		C.int(requiredMode),
		C.int(startRec),
		unsafe.Pointer(&fhBuf[0]),
		&totalRecs,
		C.int(maxBacktrack),
		&cComputedBacktrack,
		&cComputedTimeDiff,
	))
	if found < 0 {
		return 0, fhBuf, 0, -1, 0, fmt.Errorf("failed to search AID in %s", directPath)
	}
	return int32(found), fhBuf, int32(totalRecs), int32(cComputedBacktrack), int64(cComputedTimeDiff), nil
}

// ScanDirRange scans directPath starting from 1-based startRec through the end of the file,
// returning all matching 1-based record indices and the total record count in the file.
func ScanDirRange(directPath string, predsRaw []byte, numPreds int, startRec int32) ([]int32, int32, error) {
	if numPreds <= 0 || len(predsRaw) < numPreds*PredSize() {
		return nil, 0, fmt.Errorf("invalid predicates buffer")
	}

	st, err := os.Stat(directPath)
	if err != nil {
		return nil, 0, err
	}
	fhSize := int64(FileheaderSize())
	totalEst := int(st.Size() / fhSize)
	if totalEst <= 0 || int(startRec) > totalEst {
		return []int32{}, int32(totalEst), nil
	}

	maxOut := totalEst - int(startRec) + 1
	if maxOut < 1 {
		maxOut = 1
	}
	outBuf := make([]int32, maxOut)

	cPath := C.CString(directPath)
	defer C.free(unsafe.Pointer(cPath))

	var totalRecs C.int
	matched := int(C.c_scan_dir_range(
		cPath,
		unsafe.Pointer(&predsRaw[0]),
		C.int(numPreds),
		C.int(startRec),
		(*C.int32_t)(unsafe.Pointer(&outBuf[0])),
		C.int(maxOut),
		&totalRecs,
	))
	if matched < 0 {
		return nil, 0, fmt.Errorf("failed to scan %s", directPath)
	}
	if matched > maxOut {
		matched = maxOut
	}
	res := make([]int32, matched)
	copy(res, outBuf[:matched])
	return res, int32(totalRecs), nil
}

// FilterCandidates filters an existing sorted slice of 1-based candidate indices against additional predicates.
func FilterCandidates(directPath string, predsRaw []byte, numPreds int, candidates []int32) ([]int32, error) {
	if len(candidates) == 0 {
		return []int32{}, nil
	}
	if numPreds <= 0 || len(predsRaw) < numPreds*PredSize() {
		return nil, fmt.Errorf("invalid predicates buffer")
	}

	outBuf := make([]int32, len(candidates))
	cPath := C.CString(directPath)
	defer C.free(unsafe.Pointer(cPath))

	matched := int(C.c_filter_candidates(
		cPath,
		unsafe.Pointer(&predsRaw[0]),
		C.int(numPreds),
		(*C.int32_t)(unsafe.Pointer(&candidates[0])),
		C.int(len(candidates)),
		(*C.int32_t)(unsafe.Pointer(&outBuf[0])),
	))
	if matched < 0 {
		return nil, fmt.Errorf("failed to filter candidates on %s", directPath)
	}
	res := make([]int32, matched)
	copy(res, outBuf[:matched])
	return res, nil
}
