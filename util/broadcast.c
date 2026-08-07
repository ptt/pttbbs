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
    int *sorted, UTMPnumber; // SHM snapshot

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
    sorted = (int *)malloc(sizeof(int) * USHM_SIZE);
    memcpy(sorted, SHM->sorted[SHM->currsorted][0], sizeof(int) * USHM_SIZE);
    UTMPnumber = SHM->UTMPnumber;

    char msgbuf[PATHLEN];
    SNPRINTF(msgbuf, "[廣播]%s", argv[optind]);

    for (i = 0, j = 0; i < UTMPnumber; ++i, ++j) {
	// XXX why use sorted list?
	//     can we just scan uinfo with proper checking?
	int uip = sorted[i];
	uentp = &SHM->uinfo[uip];
	if (uentp->pid && kill(uentp->pid, 0) != -1){
	    write_message(uip, uentp->pid, getpid(), owner, msgbuf, MSGMODE_WRITE);
	}

	if (j == num_per_loop) {
	    fprintf(stderr, "%5d/%5d\n", i + 1, UTMPnumber);
	    j = 0;
	    sleep(sleep_time);
	}
    }
    return 0;
}
