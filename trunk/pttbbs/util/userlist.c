/* $id:$ */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "config.h"
#include "pttstruct.h"

struct utmpfile_t *u;

int main(int argc, char **argv) {
    int i, shm, counter;
    
    shm = shmget(UTMPSHM_KEY, USHM_SIZE, SHM_R | SHM_W);
    if(shm == -1) {
	perror("shmget");
	exit(0);
    }
    
    u = shmat(shm, NULL, 0);
    if(u == (struct utmpfile_t *)-1) {
	perror("shmat");
	exit(0);
    }
    
    if(argc > 1) {
	for(i = 1; i < argc; i++)
	    u->uinfo[atoi(argv[i])].pid = 0;
    } else {
	for(i = counter = 0; i < USHM_SIZE; i++)
	    if(u->uinfo[i].pid) {
		userinfo_t *f;
		
		f = &u->uinfo[i];
		printf(
		    "%4d(%d) p[%d] i[%d] u[%s] n[%s] f[%s] m[%d] d[%d] t[%ld]\n",
		    ++counter, i, f->pager, f->invisible, f->userid,
		    f->username, f->from, f->mode, f->mind, f->lastact);
	    }
	printf("\nTotal: %d(%d)\n", counter, u->number);
	if(counter != u->number) {
	    u->number = counter;
	    printf("adjust user number!\n");
	}
    }
    return 0;
}
