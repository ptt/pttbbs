#include "bbs.h"
#include <getopt.h>

extern SHM_t   *SHM;

void print_help(int argc GCC_UNUSED, char *argv[])
{
    fprintf(stderr, "Usage: %s [-t sleep_time] [-n users_per_round] [-o broadcast_name] broadcast content\n\n", argv[0]);
}

int main(int argc, char *argv[])
{
    int sleep_time = 5;
    int num_per_loop = 500;
    char * owner = "系統廣播";

    int i, j;
    userinfo_t *uentp;
    
    while ((i = getopt(argc, argv, "t:n:o:h")) != -1)
	switch (i) {
	    case 'h':
		print_help(argc, argv);
		return 0;
		break;
	    case 't':
		sleep_time = atoi(optarg);
		break;
	    case 'n':
		num_per_loop = atoi(optarg);
		break;
	    case 'o':
		owner = optarg;
		break;
	}

    if (optind == argc || strlen(argv[optind]) == 0) {
	fprintf(stderr, "no message to broadcast\n\n");
	return 1;
    }

    printf("broadcast \"%s\" ? [y/N]\n", argv[optind]);
    if (tolower(getchar()) != 'y')
	return 0;

    attach_SHM();

    char msgbuf[PATHLEN];
    SNPRINTF(msgbuf, "[廣播]%s", argv[optind]);

    for (i = 0, j = 0; i < USHM_SIZE; ++i) {
	int uslot = i;
	uentp = &SHM->uinfo[uslot];
	if (uentp->pid && kill(uentp->pid, 0) != -1){
	    write_message(uslot, uentp->pid, getpid(), owner, msgbuf, MSGMODE_WRITE);
	    ++j;
	    if (j == num_per_loop) {
	        fprintf(stderr, "processed: %d/%d\n", i + 1, USHM_SIZE);
	        j = 0;
	        sleep(sleep_time);
	    }
	}
    }
    return 0;
}
