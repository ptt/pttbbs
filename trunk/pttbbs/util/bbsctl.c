#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <grp.h>
#ifdef FreeBSD
   #include <sys/syslimits.h>
   #define  PS     "/bin/ps"
   #define  GREP   "/usr/bin/grep"
   #define  SU     "/usr/bin/su" 
#endif
#ifdef Linux
   #include <linux/limits.h>
   #define  PS     "/bin/ps"
   #define  GREP   "/bin/grep"
   #define  SU     "/bin/su"
#endif

void usage(void)
{
    printf("usage:  bbsctl [start|stop|restart|bbsadm]\n");
    exit(0);
}

void startbbs(void)
{
    if( setuid(0) < 0 ){
	perror("setuid(0)");
	exit(1);
    }
    puts("starting mbbsd:  23"); system("/home/bbs/bin/mbbsd   23");
    puts("starting mbbsd:3000"); system("/home/bbs/bin/mbbsd 3000");
    puts("starting mbbsd:3001"); system("/home/bbs/bin/mbbsd 3001");
    puts("starting mbbsd:3002"); system("/home/bbs/bin/mbbsd 3002");
    puts("starting mbbsd:3003"); system("/home/bbs/bin/mbbsd 3003");
    puts("starting mbbsd:3004"); system("/home/bbs/bin/mbbsd 3004");
    puts("starting mbbsd:3005"); system("/home/bbs/bin/mbbsd 3005");
    puts("starting mbbsd:3006"); system("/home/bbs/bin/mbbsd 3006");
    puts("starting mbbsd:3007"); system("/home/bbs/bin/mbbsd 3007");
    puts("starting mbbsd:3008"); system("/home/bbs/bin/mbbsd 3008");
    puts("starting mbbsd:3009"); system("/home/bbs/bin/mbbsd 3009");
    puts("starting mbbsd:3010"); system("/home/bbs/bin/mbbsd 3010");
}

void stopbbs(void)
{
    char    buf[1024];
    int     pid;
    FILE    *fp = popen(PS " -ax | " GREP " mbbsd | "
			GREP " listen", "r");
    while( fgets(buf, sizeof(buf), fp) != NULL ){
	sscanf(buf, "%d", &pid);
	printf("stopping %d\n", pid);
	kill(pid, 1);
    }
}

void restartbbs(void)
{
    stopbbs();
    startbbs();
}

void bbsadm(void)
{
    gid_t   gids[NGROUPS_MAX];
    int     i, ngids;
    struct  group *gr; 
    ngids = getgroups(NGROUPS_MAX, gids);
    if( (gr = getgrnam("bbsadm")) == NULL ){
	puts("group bbsadm not found");
	return;
    }

    for( i = 0 ; i < ngids ; ++i )
	if( gr->gr_gid == gids[i] )
	    break;

    if( i == ngids ){
	puts("permission denied");
	return;
    }

    if( setuid(0) < 0 ){
	perror("setuid(0)");
	return;
    }
    puts("permission granted");
    execl(SU, "su", "bbsadm", NULL);
}

struct {
    char    *cmd;
    void    (*func)();
}cmds[] = { {"start",   startbbs},
	    {"stop",    stopbbs},
	    {"restart", restartbbs},
	    {"bbsadm",  bbsadm},
	    {NULL, NULL} };

int main(int argc, char **argv)
{
    int     i;
    if( argc == 1 )
	usage();
    for( i = 0 ; cmds[i].cmd != NULL ; ++i )
	if( strcmp(cmds[i].cmd, argv[1]) == 0 ){
	    cmds[i].func();
	    break;
	}
    if( cmds[i].cmd == NULL ){
	printf("command %s not found\n", argv[1]);
	usage();
    }
    return 0;
}
