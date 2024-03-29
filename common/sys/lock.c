#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/**
 * lock fd
 * @param mode	F_WRLCK, F_UNLCK
 */
void
PttLock(int fd, int offset, int size, int mode)
{
    static struct flock lock_it;
    int             ret;

    lock_it.l_whence = SEEK_SET;/* provided offset, we should use SEEK_SET */
    lock_it.l_start = offset;	/* -"- */
    lock_it.l_len = size;	/* length of data */
    lock_it.l_type = mode;	/* set exclusive/write lock */
    lock_it.l_pid = 0;		/* pid not actually interesting */
    while ((ret = fcntl(fd, F_SETLKW, &lock_it)) < 0 && errno == EINTR)
      sleep(1);
}

