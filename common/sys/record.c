/* $Id$ */

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <stdlib.h>
#include "cmsys.h"

#define BUFSIZE 512

/* Functions for fixed size record operations */

int
get_num_records(const char *fpath, size_t size)
{
    struct stat st;

    if (stat(fpath, &st) == -1)
	return 0;

    return st.st_size / size;
}

int
get_records_keep(const char *fpath, void *rptr, size_t size, int id, size_t number, int *fd)
{
    assert(fpath);
    assert(fd);

    if (id < 1)
	return -1;

    if (*fd < 0 && (*fd = open(fpath, O_RDONLY, 0)) == -1)
	return -1;

    if (lseek(*fd, (off_t) (size * (id - 1)), SEEK_SET) == -1)
	return 0;

    if ((id = read(*fd, rptr, size * number)) == -1)
	return -1;

    return id / size;
}

int
get_records(const char *fpath, void *rptr, size_t size, int id, size_t number)
{
    int res, fd = -1;

    res = get_records_keep(fpath, rptr, size, id, number, &fd);
    close(fd);

    return res;
}

#ifndef get_record
int
get_record(const char *fpath, void *rptr, size_t size, int id)
{
    return get_records(fpath, rptr, size, id, 1);
}
#endif

#ifndef get_record_keep
int
get_record_keep(const char *fpath, void *rptr, size_t size, int id, int *fd)
{
    return get_records_keep(fpath, rptr, size, id, 1, fd);
}
#endif

int
substitute_record(const char *fpath, const void *rptr, size_t size, int id)
{
    int fd;
    off_t offset = size * (id - 1);

    if (id < 1 || (fd = OpenCreate(fpath, O_WRONLY)) == -1)
       return -1;

    lseek(fd, offset, SEEK_SET);
    PttLock(fd, offset, size, F_WRLCK);
    write(fd, rptr, size);
    PttLock(fd, offset, size, F_UNLCK);
    close(fd);

    return 0;
}

int
append_record(const char *fpath, const void *record, size_t size)
{
    int fd, index = -2, fsize = 0;

    if ((fd = OpenCreate(fpath, O_WRONLY)) == -1)
	return -1;

    flock(fd, LOCK_EX);

    if ((fsize = lseek(fd, 0, SEEK_END)) < 0)
	goto out;

    index = fsize / size;
    lseek(fd, index * size, SEEK_SET);  // avoid offset

    write(fd, record, size);

out:
    flock(fd, LOCK_UN);
    close(fd);

    return index + 1;
}

int
substitute_record2(const char *fpath, const void *srcptr, const void *destptr,
                   size_t size, int id, record_callback_t cb_can_substitue)
{
    int fd;
    int err = 0;
    off_t offset = size * (id - 1);
    void *p = NULL;

    if (id < 1 || (fd = OpenCreate(fpath, O_RDWR)) == -1)
	return -1;
    if (lseek(fd, offset, SEEK_SET) != offset ||
        (cb_can_substitue && !(p = malloc(size)))) {
        close(fd);
        return -1;
    }
    PttLock(fd, offset, size, F_WRLCK);
    while(cb_can_substitue) {
        err = -1;
        if (read(fd, p, size) != size)
            break;
        if (!cb_can_substitue(p, srcptr))
            break;
        if (lseek(fd, offset, SEEK_SET) != offset)
            break;
        err = 0;
        break;
    }
    if (err == 0) {
        if (write(fd, destptr, size) != size)
            err = -1;
    }
    PttLock(fd, offset, size, F_UNLCK);
    close(fd);
    if (p) free(p);

    return err;
}

int
delete_record2(const char *fpath, const void *rptr, size_t size,
               int id, record_callback_t cb_can_delete)
{
    char buf[BUFSIZE];
    int fi = -1, fo = -1;
    int locksize, readsize, c, d=0;
    struct stat st;
    off_t offset = size * (id - 1);
    int err = 0;
    void *p = NULL;
    const int num = 1;

    do {
        err = -1;
        fi = open(fpath, O_RDONLY, 0);
        if (fi < 0)
            break;
        if (fstat(fi, &st) != 0)
            break;
        locksize = st.st_size - offset;
        if (locksize < 0)
            break;
        fo = open(fpath, O_WRONLY, 0);
        if (fo < 0)
            break;
        if (cb_can_delete && (p = malloc(size)) == NULL)
            break;
        err = 0;
    } while (0);

    if (err != 0) {
        // clean up on error exit
        if (fi >= 0) close(fi);
        if (fo >= 0) close(fo);
        return err;
    }

    readsize = locksize - size*num;
    PttLock(fo, offset, locksize, F_WRLCK);

    while (cb_can_delete) {
        err = -1;
        if (lseek(fi, offset, SEEK_SET) != offset)
            break;
        if (read(fi, p, size) != size)
            break;
        if (!cb_can_delete(p, rptr))
            break;
        err = 0;
        break;
    }
    if (!cb_can_delete) {
        if (lseek(fi, offset+size*num, SEEK_SET) != offset)
            err = -1;
    }

    if (lseek(fo, offset, SEEK_SET) != offset)
        err = -1;
    while (err == 0 && d < readsize && (c = read(fi, buf, BUFSIZE)) > 0) {
        // when entering loop, never stop even if error
        write(fo, buf, c);
        d += c;
    }
    close(fi);
    if (err == 0)
        ftruncate(fo, st.st_size - size*num);
    PttLock(fo, offset, locksize, F_UNLCK);
    close(fo);
    if (p) free(p);

    return err;
}

int
delete_records(const char *fpath, size_t size, int id, size_t num)
{
    char buf[BUFSIZE];
    int fi, fo, locksize, readsize, c, d=0;
    struct stat st;
    off_t offset = size * (id - 1);

    if ((fi=open(fpath, O_RDONLY, 0)) == -1)
	return -1;

    if ((fo=open(fpath, O_WRONLY, 0)) == -1) {
	close(fi);
	return -1;
    }

    if (fstat(fi, &st)==-1) {
	close(fo);
	close(fi);
	return -1;
    }

    locksize = st.st_size - offset;
    readsize = locksize - size*num;

    if (locksize < 0 ) {
	close(fo);
	close(fi);
	return -1;
    }

    PttLock(fo, offset, locksize, F_WRLCK);

    if (readsize > 0) {
	lseek(fi, offset+size*num, SEEK_SET);  
	lseek(fo, offset, SEEK_SET);  
	while (d < readsize && (c = read(fi, buf, BUFSIZE)) > 0) {
	    write(fo, buf, c);
	    d += c;
	}
    }
    close(fi);

    ftruncate(fo, st.st_size - size*num);

    PttLock(fo, offset, locksize, F_UNLCK);

    close(fo);

    return 0;
}

#ifndef delete_record
int delete_record(const char *fpath, size_t size, int id)
{
    return delete_records(fpath, size, id, 1);
}
#endif

int
apply_record(const char *fpath, int (*fptr) (void *item, void *optarg),
             size_t size, void *arg)
{
    char buf[BUFSIZE];
    int fd;

    assert(size <= sizeof(buf));

    if ((fd = open(fpath, O_RDONLY, 0)) == -1)
	return -1;

    while (read(fd, buf, size) == size)
	if ((*fptr) (buf, arg) == 1) {
	    close(fd);
	    return 1;
	}

    close(fd);

    return 0;
}
