#ifndef FreeBSD
int main(int argc, char **argv)
{
    puts("this program is only for FreeBSD");
}
#else
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <kvm.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include "config.h"        // for BBSUID

int main(int argc, char **argv)
{
    kvm_t   *kd;
    struct  kinfo_proc *kp;
    char    errbuf[_POSIX2_LINE_MAX];
    int     nentries, i, npids;
    pid_t   pids[8192];
    kd = kvm_openfiles("/dev/null", "/dev/null", NULL, O_RDONLY, errbuf);
    if( kd == NULL )
	errx(1, "%s", errbuf);
    
    if ((kp = kvm_getprocs(kd, KERN_PROC_UID, BBSUID, &nentries)) == 0 ||
	nentries < 0)
	errx(1, "%s", kvm_geterr(kd));

    for( npids = 0, i = nentries ; --i >= 0 ; ++kp ){
	if( strncmp(kp->ki_comm, "mbbsd", 5) == 0 ){
	    if( kp->ki_runtime > (60 * 1000000) ){ // 60 secs
		kill(kp->ki_pid, 1);
		pids[npids++] = kp->ki_pid;
		printf("%d\n", kp->ki_pid);
	    }
	}
    }
    
    if( npids != 0 ){
	sleep(2);
	while( --npids >= 0 )
	    kill(pids[npids], 9);
    }

    kvm_close(kd);
    return 0;
}

#endif
