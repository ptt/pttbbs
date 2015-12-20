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
	if( gr->gr_gid == gids[i] )
    	    return 1;

    return 0;
}

enum {
    BINDPORTS_MODE_NONE  = 0,
    BINDPORTS_MODE_MBBSD,
    BINDPORTS_MODE_LOGIND,
    BINDPORTS_MODE_MULTI,
    BINDPORTS_MODE_ERROR,
};

int parse_bindports_mode(const char *fn)
{
    int mode = BINDPORTS_MODE_NONE;
    char buf[PATHLEN], vprogram[STRLEN], vservice[STRLEN], vopt[STRLEN];
    FILE *fp = fopen(fn, "rt");
    if (!fp) {
	printf("unable to open file: %s\n", fn);
	return BINDPORTS_MODE_ERROR;
    }

    while(fgets(buf, sizeof(buf), fp))
    {
	if (sscanf(buf, "%s%s%s", vprogram, vservice, vopt) != 3 ||
	    strcmp(vprogram, "bbsctl") != 0 ||
	    strcmp(vservice, "mode") != 0)
	    continue;

	if (strcmp(vopt, "mbbsd") == 0)
	{
	    mode = BINDPORTS_MODE_MBBSD;
	    break;
	}
	else if (strcmp(vopt, "logind") == 0)
	{
	    mode = BINDPORTS_MODE_LOGIND;
	    break;
	}
	else if (strcmp(vopt, "multi") == 0)
	{
	    mode = BINDPORTS_MODE_MULTI;
	    break;
	}
    }

    fclose(fp);
    return mode;
}

static int startbbs_defaults()
{
    // default: start listening ports with mbbsd.
    printf("starting mbbsd by at 23, 443, 3000-3010\n");
    execl(BBSHOME "/bin/mbbsd", "mbbsd", "-d", "-p23", "-p443",
	    "-p3000", "-p3001", "-p3002", "-p3003", "-p3004", "-p3005",
	    "-p3006", "-p3007", "-p3008", "-p3009", "-p3010", NULL);

    printf("starting daemons failed\n");
    return 1;
}

static int startbbs_with_multi(const char *bindports_fn);

static int startbbs_with_bindports(const char *bindports_fn, int already_in_multi)
{
    printf("starting bbs by %s\n", bindports_fn);

    // load the conf and determine how should we start the services.
    switch(parse_bindports_mode(bindports_fn))
    {
	case BINDPORTS_MODE_NONE:
	    printf("mode is not assigned. fallback to default ports.\n");
	    return startbbs_defaults();

	case BINDPORTS_MODE_MBBSD:
	    execl(BBSHOME "/bin/mbbsd", "mbbsd",   "-d", "-f", bindports_fn, NULL);
	    printf("mbbsd startup failed...\n");
	    break;

	case BINDPORTS_MODE_LOGIND:
	    execl(BBSHOME "/bin/logind", "logind", "-d", "-f", bindports_fn, NULL);
	    printf("logind startup failed...\n");
	    break;

	case BINDPORTS_MODE_MULTI:
	    if (already_in_multi) {
		printf("Only one level of multi is supported. This prevents fork bomb.\n");
		return 1;
	    }
	    return startbbs_with_multi(bindports_fn);

	case BINDPORTS_MODE_ERROR:
	    printf("failed to parse config: %s\n", bindports_fn);
	    break;
    }

    // error
    return 1;
}

static int startbbs_with_multi(const char *bindports_fn)
{
    char buf[PATHLEN];
    char vprogram[STRLEN], vservice[STRLEN], vpath[PATHLEN];
    FILE *fp = fopen(bindports_fn, "r");
    if (!fp)
	return 1;

    while(fgets(buf, sizeof(buf), fp))
    {
	if (sscanf(buf, "%s%s%s", vprogram, vservice, vpath) != 3 ||
	    strcmp(vprogram, "bbsctl") != 0 ||
	    strcmp(vservice, "multi") != 0)
	    continue;

	pid_t pid = fork();
	if (pid < 0) {
	    perror("fork");
	    fclose(fp);
	    return 1;
	} else if (pid == 0) {
	    // child
	    fclose(fp);
	    startbbs_with_bindports(vpath, 1);
	    exit(1);
	}
    }

    fclose(fp);
    return 0;
}

int startbbs(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    const char *bindports_fn = BBSHOME "/" FN_CONF_BINDPORTS;
    if( setuid(0) < 0 ){
	perror("setuid(0)");
	exit(1);
    }

    // if there's bindports.conf, use it.
    if (dashs(bindports_fn) > 0)
    {
	// rule 2, if bindports.conf exists, load it.
	return startbbs_with_bindports(bindports_fn, 0);
    }
    else
    {
	return startbbs_defaults();
    }
}

int stopbbs(int argc GCC_UNUSED, char **argv GCC_UNUSED)
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
	if (!strstr(de->d_name, ".pid"))
	    continue;
	// TODO use "mbbsd." and "logind." ?
	if( strstr(de->d_name, "mbbsd") || strstr(de->d_name, "logind")) {
	    sprintf(fn, BBSHOME "/run/%s", de->d_name);
	    if( (fp = fopen(fn, "r")) != NULL ){
		if( fgets(buf, sizeof(buf), fp) != NULL ){
		    int pid = atoi(buf);
		    if (pid < 2)
		    {
			printf("invalid pid record: %s\n", fn);
		    } else {
			char *sdot = NULL;
			strlcpy(buf, de->d_name, sizeof(buf));
			sdot = strchr(buf, '.');
			if (sdot) *sdot = 0;
			printf("stopping listening-%s at pid %5d\n", buf, pid);
			kill(pid, SIGKILL);
		    }
		}
		fclose(fp);
		unlink(fn);
	    }
	}
    }

    closedir(dirp);
    return 0;
}

int nonstopSTOP(int argc GCC_UNUSED, char **argv GCC_UNUSED)
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

int fakekill(pid_t pid, int sig GCC_UNUSED)
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

int bbsadm(int argc GCC_UNUSED, char **argv GCC_UNUSED)
{
    if( setuid(0) < 0 ){
	perror("setuid(0)");
	return 1;
    }
    puts("permission granted");
    execl(SU, "su", "bbsadm", NULL);
    return 0;
}

int bbstest(int argc GCC_UNUSED, char **argv GCC_UNUSED)
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
    execl("./testmbbsd", "testmbbsd", "-d", "-p9000", NULL);
    perror("execl()");
    return 0;
}

int Xipcrm(int argc GCC_UNUSED, char **argv GCC_UNUSED)
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

struct {
    int     (*func)(int, char **);
    char    *cmd, *descript;
} cmds[] =
    { {startbbs,   "start",      "start mbbsd/logind daemons "},
      {stopbbs,    "stop",       "killall listening daemons (mbbsd+logind)"},
      {restartbbs, "restart",    "stop and then start"},
      {bbsadm,     "bbsadm",     "switch to user: bbsadm"},
      {bbstest,    "test",       "run ./mbbsd as bbsadm"},
      {Xipcrm,     "ipcrm",      "ipcrm all msg, shm, sem"},
      {nonstopSTOP,"nonstopSTOP","killall ALL mbbsd (nonstop)"},
      {STOP,       "STOP",       "killall ALL mbbsd"},
      {fakeSTOP,   "fakeSTOP",   "fake killall ALL mbbsd"},
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
