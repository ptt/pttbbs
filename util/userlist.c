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

    if(argc == 2) {
	/* list specific id */
	for (i = 0; i < USHM_SIZE; i++)
	{
	    userinfo_t *f = &SHM->uinfo[i];
	    if(!f->pid)
		continue;
	    if(strcmp(f->userid, argv[1]) != 0)
		continue;
	    printf(
		    "id=%s money=%d\n",
		    f->userid, SHM->money[f->uid - 1]);
	}
    } 
    else 
    {
	for(i = counter = 0; i < USHM_SIZE; i++)
	    if(SHM->uinfo[i].pid) {
		userinfo_t *f;
		
		f = &SHM->uinfo[i];
		printf(
		    "%4d(%d) p[%d] i[%d] u[%s] n[%s] f[%s] m[%d] t[%d]\n",
		    ++counter, i, f->pager, f->invisible, f->userid,
		    f->nickname, f->from, f->mode, f->lastact);
	    }
	printf("\nTotal: %d(%d)\n", counter, SHM->number);
	if(counter != SHM->number) {
	    SHM->number = counter;
	    printf("adjust user number!\n");
	}
    }
    return 0;
}
