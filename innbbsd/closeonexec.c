/*
 * $Revision: 1.1 $ *
 * 
 */
/* #include "configdata.h" */
#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "clibrary.h"

#ifndef CLX_FCNTL
#define CLX_FCNTL
#endif





#if	defined(CLX_FCNTL)
#include <fcntl.h>


/*
 * *  Mark a file close-on-exec so that it doesn't get shared with our *
 * children.  Ignore any error codes.
 */
void
CloseOnExec(fd, flag)
    int             fd;
    int             flag;
{
    int             oerrno;

    oerrno = errno;
    (void)fcntl(fd, F_SETFD, flag ? FD_CLOEXEC : 0);
    errno = oerrno;
}
#endif				/* defined(CLX_FCNTL) */
