/* $Id$ */
#include "bbs.h"

SHM_t   *SHM;

int main(int argc, char **argv) {
    int i, shm, counter;
    
    shm = shmget(SHM_KEY, SHMSIZE,
#ifdef USE_HUGETLB
                SHM_HUGETLB |
#endif
	    SHM_R | SHM_W);
    if(shm == -1) {
	perror("shmget");
	exit(0);
    }
    
    if( (SHM = shmat(shm, NULL, 0)) < 0 ){
	perror("shmat");
	exit(0);
    }
    
    if(argc > 1) {
	for(i = 1; i < argc; i++)
	    SHM->uinfo[atoi(argv[i])].pid = 0;
    } else {
	for(i = counter = 0; i < USHM_SIZE; i++)
	    if(SHM->uinfo[i].pid) {
		userinfo_t *f;
		
		f = &SHM->uinfo[i];
		printf(
		    "%4d(%d) p[%d] i[%d] u[%s] n[%s] f[%s] m[%d] t[%d]\n",
		    ++counter, i, f->pager, f->invisible, f->userid,
		    f->username, f->from, f->mode, f->lastact);
	    }
	printf("\nTotal: %d(%d)\n", counter, SHM->number);
	if(counter != SHM->number) {
	    SHM->number = counter;
	    printf("adjust user number!\n");
	}
    }
    return 0;
}
