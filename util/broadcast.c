/* $Id$ */
#include "bbs.h"
#include <getopt.h>

extern SHM_t   *SHM;

void print_help(int argc, char *argv[])
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
    msgque_t msg;
    time_t now;
    int *sorted, UTMPnumber; // SHM snapshot

    while ((i = getopt(argc, argv, "t:n:o:h")) != -1)
	switch (i) {
	    case 'h':
		print_help();
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

    msg.pid = getpid();
    strlcpy(msg.userid, owner, sizeof(msg.userid));
    snprintf(msg.last_call_in, sizeof(msg.last_call_in), "[廣播]%s", argv[optind]);

    now = time(NULL);

    for (i = 0, j = 0; i < UTMPnumber; ++i, ++j) {
	if (j == num_per_loop) {
	    fprintf(stderr, "%5d/%5d\n", i + 1, UTMPnumber);
	    j = 0;
	    now = time(NULL);
	    sleep(sleep_time);
	}

	// XXX why use sorted list?
	//     can we just scan uinfo with proper checking?
	uentp = &SHM->uinfo[sorted[i]];
	if (uentp->pid && kill(uentp->pid, 0) != -1){
	    int write_pos = uentp->msgcount;
	    if (write_pos < (MAX_MSGS - 1)){
		uentp->msgcount = write_pos + 1;
		memcpy(&uentp->msgs[write_pos], &msg, sizeof(msg));
#ifdef NOKILLWATERBALL
		uentp->wbtime = (time4_t)now;
#else
		kill(uentp->pid, SIGUSR2);
#endif
	    }
	}
    }
    return 0;
}
