#include "bbs.h"
#include "var.h"
#include "testutil.h"

static void
shm_check_error()
{
    fprintf(stderr, "Please use the source code version corresponding to SHM,\n"
	    "or use ipcrm(1) command to clean share memory.\n");
    exit(1);
}

void check_SHM() {
    if (SHM->version != SHM_VERSION) {
	fprintf(stderr, "Error: SHM->version(%d) != SHM_VERSION(%d)\n",
		SHM->version, SHM_VERSION);
	shm_check_error();
    }
    if (SHM->size    != sizeof(SHM_t)) {
	fprintf(stderr, "Error: SHM->size(%d) != sizeof(SHM_t)(%zd)\n",
		SHM->size, sizeof(SHM_t));
	shm_check_error();
    }

    if (!SHM->loaded)		/* (uhash) assume fresh shared memory is
				 * zeroed */
	exit(1);
    if (SHM->Btouchtime == 0)
	SHM->Btouchtime = 1;
    bcache = SHM->bcache;

    if (SHM->Ptouchtime == 0)
	SHM->Ptouchtime = 1;

    if (SHM->Ftouchtime == 0)
	SHM->Ftouchtime = 1;
}
