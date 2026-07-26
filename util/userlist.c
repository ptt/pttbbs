#include "bbs.h"
#include "cmbbs.h"

extern SHM_t *SHM;

int main(int argc, char **argv) {
    int i, counter;

    attach_SHM();

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
    }
    return 0;
}
