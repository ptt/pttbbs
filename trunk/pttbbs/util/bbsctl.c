#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
void usage(void)
{
    printf("usage:  bbsctl [start|stop|restart]\n");
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
    FILE    *fp = popen("/bin/ps -ax | /usr/bin/grep mbbsd | "
			"/usr/bin/grep listen", "r");
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

int main(int argc, char **argv)
{
    if( argc == 1 )
	usage();
    if( strcmp(argv[1], "start") == 0 )
	startbbs();
    else if( strcmp(argv[1], "stop") == 0 )
	stopbbs();
    else if( strcmp(argv[1], "restart") == 0 )
	restartbbs();
    return 0;
}
