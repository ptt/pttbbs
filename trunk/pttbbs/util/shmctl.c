#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "config.h"
#include "pttstruct.h"
#include "util.h"

extern struct utmpfile_t *utmpshm;

int fixutmp(int argc, char **argv)
{
    int     i;
    char    buf[1024], *clean;
    struct  stat    st;
    if( utmpshm->busystate ){
	puts("utmpshm is busy");
	return 0;
    }
    utmpshm->busystate = 1;
    for( i = 0 ; i < USHM_SIZE ; ++i )
	if( utmpshm->uinfo[i].pid ){
	    clean = NULL;
	    if( !isalpha(utmpshm->uinfo[i].userid[0]) )
		clean = "userid error";
	    else{
		sprintf(buf, "/proc/%d", utmpshm->uinfo[i].pid);
		if( stat(buf, &st) < 0 ) 
		    clean = "process not exist";
	    }
	    
	    if( clean ){
		printf("clean %06d(%s), userid: %s\n",
		       i, clean, utmpshm->uinfo[i].userid);
		memset(&utmpshm->uinfo[i], 0, sizeof(userinfo_t));
	    }
	}
    utmpshm->busystate = 0;
    return 0;
}

struct {
    int     (*func)(int, char **);
    char    *cmd, *descript;
} cmd[] =
    { {fixutmp, "fixutmp", "clear dead userlist entry"},
      {NULL, NULL, NULL} };

int main(int argc, char **argv)
{
    int     i = 0;
	
    if( argc >= 2 ){
	resolve_utmp();
	resolve_boards();
	//resolve_garbage();
	resolve_fcache();
	for( i = 0 ; cmd[i].func != NULL ; ++i )
	    if( strcmp(cmd[i].cmd, argv[1]) == 0 ){
		cmd[i].func(argc - 2, &argv[2]);
		break;
	    }
    }
    if( argc == 1 || cmd[i].func == NULL ){
	/* usage */
	printf("usage: bbsctl [command] [options]\n");
	printf("commands:\n");
	for( i = 0 ; cmd[i].func != NULL ; ++i )
	    printf("\t%-15s%s\n", cmd[i].cmd, cmd[i].descript);
    }
    return 0;
}
