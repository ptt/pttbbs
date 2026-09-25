#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cmsys.h"
#include "cmbbs.h"
#include "common.h"
#include "config.h"

void
fileheader_storage_to_mem(fileheader_t *fh, size_t count)
{
    if (!NEED_STORAGE_CONV || !fh || count == 0)
        return;
    for (size_t i = 0; i < count; i++) {
        storage_to_mb(fh[i].owner, fh[i].owner, sizeof(fh[i].owner));
        storage_to_mb(fh[i].date, fh[i].date, sizeof(fh[i].date));
        storage_to_mb(fh[i].title, fh[i].title, sizeof(fh[i].title));
    }
}

void
fileheader_mem_to_storage(fileheader_t *fh)
{
    if (!NEED_STORAGE_CONV || !fh)
        return;
    mb_to_storage(fh->owner, fh->owner, sizeof(fh->owner));
    mb_to_storage(fh->date, fh->date, sizeof(fh->date));
    mb_to_storage(fh->title, fh->title, sizeof(fh->title));
}

int
read_fileheader(int fd, fileheader_t *fh)
{
    ssize_t ret = read(fd, fh, sizeof(fileheader_t));
    if (ret < 0)
        return -1;
    if (ret != (ssize_t)sizeof(fileheader_t))
        return 0;
    fileheader_storage_to_mem(fh, 1);
    return 1;
}

int
write_fileheader(int fd, const fileheader_t *fh)
{
    ssize_t ret;
    if (!NEED_STORAGE_CONV) {
        ret = write(fd, fh, sizeof(fileheader_t));
    } else {
        fileheader_t fh_tmp = *fh;
        fileheader_mem_to_storage(&fh_tmp);
        ret = write(fd, &fh_tmp, sizeof(fileheader_t));
    }
    return (ret == (ssize_t)sizeof(fileheader_t)) ? 1 : -1;
}

int
get_fileheaders_keep(const char *fpath, fileheader_t *rptr, int id, size_t number, int *fd)
{
    int res = get_records_keep(fpath, rptr, sizeof(fileheader_t), id, number, fd);
    if (res > 0)
        fileheader_storage_to_mem(rptr, (size_t)res);
    return res;
}

int
get_fileheaders(const char *fpath, fileheader_t *rptr, int id, size_t number)
{
    int res, fd = -1;
    res = get_fileheaders_keep(fpath, rptr, id, number, &fd);
    if (fd >= 0)
        close(fd);
    return res;
}

int
append_fileheader(const char *fpath, const fileheader_t *record)
{
    if (!NEED_STORAGE_CONV)
        return append_record(fpath, record, sizeof(fileheader_t));
    fileheader_t fh_tmp = *record;
    fileheader_mem_to_storage(&fh_tmp);
    return append_record(fpath, &fh_tmp, sizeof(fileheader_t));
}

int
modify_fileheader(const char *fpath, const fileheader_t *rptr, int id)
{
    if (!NEED_STORAGE_CONV)
        return substitute_record(fpath, rptr, sizeof(fileheader_t), id);
    fileheader_t fh_tmp = *rptr;
    fileheader_mem_to_storage(&fh_tmp);
    return substitute_record(fpath, &fh_tmp, sizeof(fileheader_t), id);
}

int
substitute_fileheaders(const char *fpath, const fileheader_t *rptr, size_t count, int id)
{
    int fd;
    size_t total_sz = sizeof(fileheader_t) * count;
    off_t offset = (off_t)sizeof(fileheader_t) * (id - 1);

    if (count == 0)
        return 0;
    if (id < 1 || (fd = OpenCreate(fpath, O_WRONLY)) == -1)
        return -1;

    lseek(fd, offset, SEEK_SET);
    PttLock(fd, offset, total_sz, F_WRLCK);
    if (!NEED_STORAGE_CONV) {
        write(fd, rptr, total_sz);
    } else {
        for (size_t i = 0; i < count; i++)
            write_fileheader(fd, &rptr[i]);
    }
    PttLock(fd, offset, total_sz, F_UNLCK);
    close(fd);
    return 0;
}

struct apply_fhdr_ctx {
    int (*fptr)(void *item, void *optarg);
    void *arg;
};

static int
_apply_fhdr_cb(void *item, void *optarg)
{
    struct apply_fhdr_ctx *ctx = (struct apply_fhdr_ctx *)optarg;
    fileheader_storage_to_mem((fileheader_t *)item, 1);
    return ctx->fptr(item, ctx->arg);
}

int
apply_fileheader(const char *fpath, int (*fptr)(void *item, void *optarg), void *arg)
{
    if (!NEED_STORAGE_CONV)
        return apply_record(fpath, fptr, sizeof(fileheader_t), arg);
    struct apply_fhdr_ctx ctx = { fptr, arg };
    return apply_record(fpath, _apply_fhdr_cb, sizeof(fileheader_t), &ctx);
}

int
getindex_m(const char *direct, fileheader_t *fhdr, int end, int isloadmoney)
{
    int fd = -1, begin = 1, i, s, times;
    fileheader_t fh;
    int stamp;
    int n = get_num_records(direct, sizeof(fileheader_t));
    if (end > n || end <= 0)
        end = n;
    stamp = get_fhdr_stamp_ts(fhdr->filename);
    for (i = (begin + end) / 2, times = 0;
         end >= begin && times < 20;
         i = (begin + end) / 2, ++times) {
        if (get_fileheader_keep(direct, &fh, i, &fd) == -1 ||
            !fh.filename[0])
            break;
        s = get_fhdr_stamp_ts(fh.filename);
        if (time4_gt(s, stamp))
            end = i - 1;
        else if (s == stamp) {
            close(fd);
            if (isloadmoney)
                fhdr->multi.money = fh.multi.money;
            return i;
        } else
            begin = i + 1;
    }

    if (times < 20) {
        if (fd != -1)
            close(fd);
        return -i;
    }
    if (fd != -1)
        close(fd);
    return 0;
}

int
getindex(const char *direct, fileheader_t *fhdr, int end)
{
    return getindex_m(direct, fhdr, end, 0);
}

int
substitute_ref_record(const char *direct, fileheader_t *fhdr, int ent)
{
    /* ent may be stale (e.g. remapped index in select mode); only write
     * if the record at ent is still the same file. */
    return substitute_fileheader(direct, fhdr, fhdr, ent);
}

static int
_is_same_fhdr_filename(const void *ptr1, const void *ptr2) {
    return strcmp(((const fileheader_t*)ptr1)->filename,
                  ((const fileheader_t*)ptr2)->filename) == 0;
}

int
substitute_fileheader(const char *dir_path,
                       const void *srcptr, const void *destptr, int id)
{
    if (!NEED_STORAGE_CONV)
        return substitute_record2(dir_path, srcptr, destptr, sizeof(fileheader_t),
                                  id, _is_same_fhdr_filename);
    fileheader_t fh_tmp = *(const fileheader_t *)destptr;
    fileheader_mem_to_storage(&fh_tmp);
    return substitute_record2(dir_path, srcptr, &fh_tmp, sizeof(fileheader_t),
                              id, _is_same_fhdr_filename);
}

int
delete_fileheader(const char *dir_path, const void *rptr, int id)
{
    return delete_record2(dir_path, rptr, sizeof(fileheader_t),
                          id, _is_same_fhdr_filename);
}

int
is_valid_fileheader(const fileheader_t *fhdr)
{
    int i, size = sizeof(fhdr->filename);
    for (i = 0; i < size && fhdr->filename[i]; i++)
	if (fhdr->filename[i] == '/')
	    return 0;
    return i < size ? 1 : 0;
}
