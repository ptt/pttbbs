#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <grp.h>
#include <dirent.h>

#ifdef FreeBSD
   #include <sys/syslimits.h>
   #define  SU      "/usr/bin/su" 
   #define  CP      "/bin/cp"
   #define  KILLALL "/usr/bin/killall"
#endif
#ifdef Linux
   #include <linux/limits.h>
   #define  SU      "/bin/su"
   #define  CP      "/bin/cp"
   #define  KILLALL "/usr/bin/killall"
#endif

void usage(void)
{
    printf("usage:  bbsctl [start|stop|restart|bbsadm|test]\n"
	   "        bbsctl start      start mbbsd at 23,3000-3010\n"
	   "        bbsctl stop       killall listening mbbsd\n"
	   "        bbsctl restart    stop + start\n"
	   "        bbsctl bbsadm     su to bbsadm\n"
	   "        bbsctl test       use bbsadm permission to exec ./mbbsd 9000\n");
    exit(0);
}

int HaveBBSADM(void)
{
    gid_t   gids[NGROUPS_MAX];
    int     i, ngids;
    struct  group *gr; 
    ngids = getgroups(NGROUPS_MAX, gids);
    if( (gr = getgrnam("bbsadm")) == NULL ){
	puts("group bbsadm not found");
	return 0;
    }

    for( i = 0 ; i < ngids ; ++i )
	if( gr->gr_gid == gids[i] )
	    break;

    if( i == ngids ){
	puts("permission denied");
	return 0;
    }

    return 1;
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
    DIR     *dirp;
    struct  dirent *de;    
    FILE    *fp;
    char    buf[512];
    if( !(dirp = opendir("/proc")) ){
	perror("open /proc");
	exit(0);
    }

    while( (de = readdir(dirp)) ){
	if( de->d_type & DT_DIR ){
	    sprintf(buf, "/proc/%s/cmdline", de->d_name);
	    if( (fp = fopen(buf, "r")) ){
		if( fgets(buf, sizeof(buf), fp) != NULL ){
		    if( strstr(buf, "mbbsd") && strstr(buf, "listening") ){
			kill(atoi(de->d_name), 1);
			printf("stopping mbbsd at pid %5d\n", atoi(de->d_name));
		    }
		}
		fclose(fp);
	    }
	}
    }

    closedir(dirp);
}

void restartbbs(void)
{
    stopbbs();
    sleep(1);
    startbbs();
}

void bbsadm(void)
{
    if( setuid(0) < 0 ){
	perror("setuid(0)");
	return;
    }
    puts("permission granted");
    execl(SU, "su", "bbsadm", NULL);
}

void bbstest(void)
{
    if( access("mbbsd", 0) < 0 ){
	perror("./mbbsd");
	return;
    }
    system(CP " -f mbbsd testmbbsd");
    if( setuid(0) < 0 ){
	perror("setuid(0)");
	return;
    }
    if( setuid(9999) < 0 ){
	perror("setuid(9999)");
	return;
    }
    system(KILLALL " testmbbsd");
    execl("./testmbbsd", "testmbbsd", "9000", NULL);
    perror("execl()");
}

struct {
    char    *cmd;
    void    (*func)();
}cmds[] = { {"start",   startbbs},
	    {"stop",    stopbbs},
	    {"restart", restartbbs},
	    {"bbsadm",  bbsadm},
	    {"test",    bbstest},
	    {NULL, NULL} };

int main(int argc, char **argv)
{
    int     i;
    if( argc == 1 )
	usage();
    if( !HaveBBSADM() )
	return 1;
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
