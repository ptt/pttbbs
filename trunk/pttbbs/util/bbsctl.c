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
   #define  IPCS    "/usr/bin/ipcs"
   #define  IPCRM   "/usr/bin/ipcrm"
   #define  AWK     "/usr/bin/awk"
#endif
#ifdef Linux
   #include <linux/limits.h>
   #define  SU      "/bin/su"
   #define  CP      "/bin/cp"
   #define  KILLALL "/usr/bin/killall"
#endif

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

int startbbs(int argc, char **argv)
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
    return 0;
}

int stopbbs(int argc, char **argv)
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
			kill(atoi(de->d_name), 9);
			printf("stopping mbbsd at pid %5d\n",
			       atoi(de->d_name));
		    }
		}
		fclose(fp);
	    }
	}
    }

    closedir(dirp);
    return 0;
}

int restartbbs(int argc, char **argv)
{
    stopbbs(argc, argv);
    sleep(1);
    startbbs(argc, argv);
    return 0;
}

int bbsadm(int argc, char **argv)
{
    if( setuid(0) < 0 ){
	perror("setuid(0)");
	return 1;
    }
    puts("permission granted");
    execl(SU, "su", "bbsadm", NULL);
    return 0;
}

int bbstest(int argc, char **argv)
{
    if( access("mbbsd", 0) < 0 ){
	perror("./mbbsd");
	return 1;
    }
    system(CP " -f mbbsd testmbbsd");
    if( setuid(0) < 0 ){
	perror("setuid(0)");
	return 1;
    }
    if( setuid(9999) < 0 ){
	perror("setuid(9999)");
	return 1;
    }
    system(KILLALL " testmbbsd");
    execl("./testmbbsd", "testmbbsd", "9000", NULL);
    perror("execl()");
    return 0;
}

int Xipcrm(int argc, char **argv)
{
    char    buf[256], cmd[256];
    FILE    *fp;
    sprintf(buf, IPCS " | " AWK " '{print $1 $2}'");
    if( !(fp = popen(buf, "r")) ){
	perror(buf);
	return 1;
    }
    while( fgets(buf, sizeof(buf), fp) != NULL ){
	if( buf[0] == 't' || buf[0] == 'm' || buf[0] == 's' ){
	    buf[strlen(buf) - 1] = 0;
	    sprintf(cmd, IPCRM " -%c %s\n", buf[0], &buf[1]);
	    system(cmd);
	}
    }
    pclose(fp);
    system(IPCS);
    return 0;
}

struct {
    int     (*func)(int, char **);
    char    *cmd, *descript;
} cmds[] =
    { {startbbs,   "start",    "start mbbsd at port 23, 3000~3010"},
      {stopbbs,    "stop",     "killall listening mbbsd"},
      {restartbbs, "restart",  "stop and then start"},
      {bbsadm,     "bbsadm",   "switch to user: bbsadm"},
      {bbstest,    "test",     "run ./mbbsd as bbsadm"},
      {Xipcrm,     "ipcrm",    "ipcrm all msg, shm, sem"},
      {NULL,       NULL,       NULL} };

int main(int argc, char **argv)
{
    int     i = 0;
    if( !HaveBBSADM() )
	return 1;
    if( argc >= 2 ){
	chdir(BBSHOME);
	for( i = 0 ; cmds[i].func != NULL ; ++i )
	    if( strcmp(cmds[i].cmd, argv[1]) == 0 ){
		cmds[i].func(argc - 2, &argv[2]);
		break;
	    }
    }
    if( argc == 1 || cmds[i].func == NULL ){
	/* usage */
	printf("usage: bbsctl [command] [options]\n");
	printf("commands:\n");
	for( i = 0 ; cmds[i].func != NULL ; ++i )
	    printf("\t%-15s%s\n", cmds[i].cmd, cmds[i].descript);
    }
    return 0;
}
