/* $Id$ */
#include "bbs.h"

#ifdef __FreeBSD__
   #include <sys/syslimits.h>
   #include <sys/types.h>
   #include <signal.h>
   #include <grp.h>
   #define  SU      "/usr/bin/su" 
   #define  CP      "/bin/cp"
   #define  KILLALL "/usr/bin/killall"
   #define  IPCS    "/usr/bin/ipcs"
   #define  IPCRM   "/usr/bin/ipcrm"
   #define  AWK     "/usr/bin/awk"
#endif
#ifdef __linux__
   #include <linux/limits.h>
   #include <sys/types.h>
   #include <signal.h>
   #include <grp.h>
   #define  SU      "/bin/su"
   #define  CP      "/bin/cp"
   #define  KILLALL "/usr/bin/killall"
   #define  IPCS    "/usr/bin/ipcs"
   #define  IPCRM   "/usr/bin/ipcrm"
   #define  AWK     "/usr/bin/awk"
#endif

int HaveBBSADM(void)
{
    gid_t   gids[NGROUPS_MAX];
    int     i, ngids;
    struct  group *gr; 

    if( getuid() == 0 || getuid() == BBSUID )
	return 1;

    ngids = getgroups(NGROUPS_MAX, gids);
    if( (gr = getgrnam("bbsadm")) == NULL ){
	puts("group bbsadm not found");
	return 0;
    }

    for( i = 0 ; i < ngids ; ++i )
	if( gr->gr_gid == (int)gids[i] )
    	    return 1;

    return 0;
}

int startbbs(int argc, char **argv)
{
    if( setuid(0) < 0 ){
	perror("setuid(0)");
	exit(1);
    }
    printf("starting mbbsd at 23, 443, 3000-3010\n");
    execl(BBSHOME "/bin/mbbsd", "mbbsd", "23", "443",
	  "3000", "3001", "3002", "3003", "3004", "3005",
	  "3006", "3007", "3008", "3009", "3010", NULL);
    printf("starting mbbsd failed\n");
    return 1;
}

int stopbbs(int argc, char **argv)
{
    DIR     *dirp;
    struct  dirent *de;    
    FILE    *fp;
    char    buf[512], fn[512];
    if( !(dirp = opendir("run")) ){
	perror("open " BBSHOME "/run");
	exit(0);
    }

    while( (de = readdir(dirp)) ){
	if( strstr(de->d_name, "mbbsd") && strstr(de->d_name, "pid")){
	    sprintf(fn, BBSHOME "/run/%s", de->d_name);
	    if( (fp = fopen(fn, "r")) != NULL ){
		if( fgets(buf, sizeof(buf), fp) != NULL ){
		    printf("stopping listening-mbbsd at pid %5d\n", atoi(buf));
		    kill(atoi(buf), SIGKILL);
		}
		fclose(fp);
		unlink(fn);
	    }
	}
    }

    closedir(dirp);
    return 0;
}

int nonstopSTOP(int argc, char **argv)
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
		    if( strstr(buf, "mbbsd") ){
			kill(atoi(de->d_name), SIGHUP);
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

int fakekill(pid_t pid, int sig)
{
  kill(pid, 0 /* dummy */);
  return 0;
}

int STOP(int argc, char **argv)
{
    DIR     *dirp;
    struct  dirent *de;    
    FILE    *fp;
    char    buf[512];
    int     num_kill_per_sec = 100;
    int     num_load_threshold = 200;
    int     num_wait_sec = 10;
    int     count = 0;
    int     i;

    if(argc > 0 && (i = atoi(argv[0])) > 0)
    {
      argc --, argv ++;
      num_kill_per_sec = i;
    }
    if(argc > 0 && (i = atoi(argv[0])) > 0)
    {
      argc --, argv ++;
      num_load_threshold = i;
    }
    if(argc > 0 && (i = atoi(argv[0])) > 0)
    {
      argc --, argv ++;
      num_wait_sec = i;
    }

    if( !(dirp = opendir("/proc")) ){
	perror("open /proc");
	exit(0);
    }

    while( (de = readdir(dirp)) ){
	if( de->d_type & DT_DIR ){
	    int load;

	    while((load = cpuload(NULL)) > num_load_threshold)
	    {
	      printf("Current load: %d, wait for %d sec...\n", load, num_wait_sec);
	      sleep(num_wait_sec);
	    }
	    sprintf(buf, "/proc/%s/cmdline", de->d_name);
	    if( (fp = fopen(buf, "r")) ){
		if( fgets(buf, sizeof(buf), fp) != NULL ){
		    if( strstr(buf, "mbbsd") ){
		        count ++;
			kill(atoi(de->d_name), SIGHUP);
			printf("stopping mbbsd at pid %5d\n",
			       atoi(de->d_name));
		    }
		}
		fclose(fp);
	    }
	}
	if(count >= num_kill_per_sec)
	{
	  sleep(1);
	  count = 0;
	}
    }

    closedir(dirp);
    return 0;
}

#define FAKESTOP_PID_LIMIT (100000)
int fakeSTOP(int argc, char **argv)
{
    DIR     *dirp;
    struct  dirent *de;    
    FILE    *fp;
    char    buf[512];
    int num_per_sec = 100;
    int num_load_threshold = 200;
    pid_t pids[FAKESTOP_PID_LIMIT];
    int num_pid = 0;
    int i;

    if(argc > 0)
    {
      int n;
      
      if((n = atoi(argv[0])) > 0)
        num_per_sec = n;

      if(argc > 1)
      {
        if((n = atoi(argv[1])) > 0)
          num_load_threshold = n;
      }
    }

    if( !(dirp = opendir("/proc")) ){
	perror("open /proc");
	exit(0);
    }

    printf("Now halting all mbbsd processes...\n");
    while( (de = readdir(dirp)) ){
	if( de->d_type & DT_DIR ){
	    sprintf(buf, "/proc/%s/cmdline", de->d_name);
	    if( (fp = fopen(buf, "r")) ){
		if( fgets(buf, sizeof(buf), fp) != NULL ){
		    if( strstr(buf, "mbbsd") ){
		        pid_t pid = atoi(de->d_name);

		        if(num_pid < FAKESTOP_PID_LIMIT)
		        {
		          pids[num_pid ++] = pid;
		        }
			fakekill(pid, SIGQUIT);
			printf("halting(SIGQUIT) mbbsd at pid %5d\n",
			       pid);
		    }
		}
		fclose(fp);
	    }
	}
    }
    closedir(dirp);

    printf("Really killing them...\n");
    for(i = 0; i < num_pid; )
    {
      int c;
      int load;

      load = cpuload(NULL);
      if(load > num_load_threshold)
      {
        printf("current load: %d, waiting...\n", load);
        sleep(5);
        continue;
      }
      for(c = 0; c < num_per_sec && i < num_pid; c ++, i ++)
      {
        pid_t pid = pids[i];

        fakekill(pid, SIGHUP);
        printf("stopping(SIGHUP) mbbsd at pid %5d\n", pid);
      }
      sleep(1);
    }

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
    if( setuid(BBSUID) < 0 ){
	perror("setuid(BBSUID)");
	return 1;
    }
    system(KILLALL " testmbbsd");
    execl("./testmbbsd", "testmbbsd", "9000", NULL);
    perror("execl()");
    return 0;
}

int Xipcrm(int argc, char **argv)
{
#ifdef __FreeBSD__
    char    buf[256], cmd[256];
    FILE    *fp;
    setuid(BBSUID); /* drop privileges so we don't remove other users' IPC */
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
#elif defined(__linux__)
    char    buf[256], cmd[256], *type = "ms";
    FILE    *fp;
    setuid(BBSUID); /* drop privileges so we don't remove other users' IPC */
    for( ;*type != '\0';type++ ){
        sprintf(buf, IPCS " -%c | " AWK " '{print $2}'", *type);
        if( !(fp = popen(buf, "r")) ){
            perror(buf);
            return 1;
        }
        while( fgets(buf, sizeof(buf), fp) != NULL ){
            if( isdigit(buf[0]) ){
                buf[strlen(buf) - 1] = 0;
                sprintf(cmd, IPCRM " -%c %s\n", *type, buf);
                system(cmd);
            }
        }
        pclose(fp);
    }
    system(IPCS);
    return 0;
#else
    puts("not implement!");
    return 1;
#endif
}

int permreport(int argc, char **argv)
{
    int     fd, i, count;
    userec_t usr;
    struct {
	int     perm;
	char    *desc;
    } check[] = {
	{PERM_BBSADM,   "PERM_BBSADM"},
	{PERM_SYSOP,    "PERM_SYSOP"},
	{PERM_ACCOUNTS, "PERM_ACCOUNTS  帳號總管"},
	{PERM_CHATROOM,	"PERM_CHATROOM  聊天室總管"},
	{PERM_BOARD,	"PERM_BOARD     看板總管"},
	{PERM_PRG,	"PERM_PRG       程式組"},
	{PERM_VIEWSYSOP,"PERM_VIEWSYSOP 視覺站長"},
	{PERM_POLICE_MAN,"PERM_POLICE_MAN 警察總管"},
	{PERM_SYSSUPERSUBOP,"PERM_SYSSUPERSUBOP	群組長"},
	//{PERM_SYSSUBOP, "PERM_SYSSUBOP    小組長"},
	{PERM_ACCTREG,  "PERM_ACCTREG   帳號審核組"},
#if 0
		 {PERM_RELATION, "PERM_RELATION"},
                 {PERM_PRG,      "PERM_PRG"},
                 {PERM_ACTION,   "PERM_ACTION"},
                 {PERM_PAINT,    "PERM_PAINT"},
                 {PERM_POLICE_MAN, "PERM_POLICE_MAN"},
                 {PERM_MSYSOP,   "PERM_MSYSOP"},
                 {PERM_PTT,      "PERM_PTT"},
#endif
		 {0, NULL}};

    if( (fd = open(".PASSWDS", O_RDONLY)) < 0 ){
	perror(".PASSWDS");
	return 1;
    }
    for( count = i = 0 ; check[i].perm != 0 ; ++i ){
	count = 0;
	lseek(fd, 0, SEEK_SET);
	printf("%s\n", check[i].desc);
	while( read(fd, &usr, sizeof(usr)) > 0 ){
	    if( usr.userid[0] != 0 && isalpha(usr.userid[0]) &&
		usr.userlevel & check[i].perm ){
		++count;
		printf("%-20s%-10s\n", usr.userid, usr.realname);
	    }
	}
	printf("total %d users\n\n", count);
    }
    return 0;
}

struct {
    int     (*func)(int, char **);
    char    *cmd, *descript;
} cmds[] =
    { {startbbs,   "start",      "start mbbsd at port 23, 3000~3010"},
      {stopbbs,    "stop",       "killall listening mbbsd"},
      {restartbbs, "restart",    "stop and then start"},
      {bbsadm,     "bbsadm",     "switch to user: bbsadm"},
      {bbstest,    "test",       "run ./mbbsd as bbsadm"},
      {Xipcrm,     "ipcrm",      "ipcrm all msg, shm, sem"},
      {nonstopSTOP,"nonstopSTOP","killall ALL mbbsd (nonstop)"},
      {STOP,       "STOP",       "killall ALL mbbsd"},
      {fakeSTOP,   "fakeSTOP",   "fake killall ALL mbbsd"},
      {permreport, "permreport", "permission report"},
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
	if ( geteuid() != 0 )
	    printf("Warning: bbsctl should be SUID\n");
    }
    return 0;
}
