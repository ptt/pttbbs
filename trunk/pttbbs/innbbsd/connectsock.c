#include "innbbsconf.h"
#include "daemon.h"
#include <signal.h>
#include <setjmp.h>

static jmp_buf timebuf;

static void
timeout(sig)
  int sig;
{
  longjmp(timebuf, sig);
}

extern int errno;
static void reapchild (s)
int s;
{
	int state;
	while (waitpid(-1,&state,WNOHANG|WUNTRACED)>0) {
	/*	printf("reaping child\n");*/
	}
}

void dokill(s)
int s;
{
   kill(0,SIGKILL);
}

static INETDstart = 0;
int startfrominetd(flag)
{
   INETDstart = flag ;
}


int standalonesetup(fd)
int fd;
{
        int on =1;
	struct linger foobar;
	if (setsockopt(fd,SOL_SOCKET, SO_REUSEADDR,(char *)&on,sizeof(on)) < 0)
   	   syslog(LOG_ERR, "setsockopt (SO_REUSEADDR): %m");
	foobar.l_onoff = 0;
	if (setsockopt(fd, SOL_SOCKET, SO_LINGER, (char*)&foobar, sizeof (foobar))<0)
	   syslog(LOG_ERR, "setsockopt (SO_LINGER): %m");
}

static char *UNIX_SERVER_PATH;
static int (*halt)();

sethaltfunction(haltfunc)
int (*haltfunc)();
{
    halt = haltfunc;
}

void docompletehalt(s)
int s;
{
	/*printf("try to remove %s\n", UNIX_SERVER_PATH);
	unlink(UNIX_SERVER_PATH);*/
	exit(0);
	/*dokill();*/
}

void doremove(s)
int s;
{
   if (halt != NULL)
      (*halt)(s);
   else 
      docompletehalt(s);
}


initunixserver(path, protocol)
char *path;
char *protocol;
{
	struct sockaddr_un s_un;   
	/* unix endpoint address */
	struct protoent *pe;   /*protocol information entry*/
	int s;
	char *ptr;

	bzero((char*)&s_un,sizeof(s_un));
	s_un.sun_family= AF_UNIX;
	strcpy(s_un.sun_path, path);
	if (protocol==NULL)
	     protocol="tcp";
        /* map protocol name to protocol number */
	pe=getprotobyname(protocol);
	if (pe==NULL) {
	 fprintf(stderr,"%s: Unknown protocol.\n",protocol);
	 return (-1);
	}

        /* Allocate a socket */
	s = socket(PF_UNIX,strcmp(protocol,"tcp")?SOCK_DGRAM:SOCK_STREAM,0);
	if (s<0) {
		printf("protocol %d\n", pe->p_proto);
		perror("socket");
		return -1;
	}
	/*standalonesetup(s);*/
        signal(SIGHUP, SIG_IGN);
        signal(SIGUSR1, SIG_IGN);
	signal(SIGCHLD,reapchild);
	UNIX_SERVER_PATH = path;
	signal(SIGINT,doremove);
	signal(SIGTERM,doremove);

	chdir("/");
	if (bind(s,(struct sockaddr*)&s_un,sizeof (struct sockaddr_un))<0){
		perror("bind");
		perror(path);
		return -1;
	}
	listen(s,10);
	return s;
}

initinetserver(service,protocol)
char *service;
char *protocol;
{
        struct servent *se;    /*service information entry*/
	struct hostent *he;    /*host information entry*/
	struct protoent *pe;   /*protocol information entry*/
	struct sockaddr_in sin;/*Internet endpoint address*/
	int port,s;
	int randomport=0;

	bzero((char*)&sin,sizeof(sin));
	sin.sin_family= AF_INET;
	if (!strcmp("0",service)) {
		randomport = 1;
		sin.sin_addr.s_addr = INADDR_ANY;
	}

	if (service==NULL) 
	     service=DEFAULTPORT;
	if (protocol==NULL)
	     protocol="tcp";
        /* map service name to port number */
	/* service ---> port */
	se = getservbyname(service,protocol);
	if (se==NULL) {
	  port = htons((u_short)atoi(service)); 
	  if (port==0 && !randomport) {
	   fprintf (stderr, "%s/%s: Unknown service.\n",service,protocol);
	   return (-1);
	  }
        } else 
	        port=se->s_port;
	sin.sin_port = port;

        /* map protocol name to protocol number */
	pe=getprotobyname(protocol);
	if (pe==NULL) {
	 fprintf(stderr,"%s: Unknown protocol.\n",protocol);
	 return (-1);
	}

        /* Allocate a socket */
	s = socket(PF_INET,strcmp(protocol,"tcp")?SOCK_DGRAM:SOCK_STREAM,pe->p_proto);
	if (s<0) {
		perror("socket");
		return -1;
	}
	standalonesetup(s);
        signal(SIGHUP, SIG_IGN);
        signal(SIGUSR1, SIG_IGN);
	signal(SIGCHLD,reapchild);
	signal(SIGINT,dokill);
	signal(SIGTERM,dokill);

	chdir("/");
	if (bind(s,(struct sockaddr*)&sin,sizeof (struct sockaddr_in))<0){
		perror("bind");
		return -1;
	}
	listen(s,10);
#ifdef DEBUG
	{ int length=sizeof(sin);
	getsockname(s,&sin,&length);
	printf("portnum alocalted %d\n",sin.sin_port);
	}
#endif
	return s;
}

int open_unix_listen(path,protocol,initfunc)
char *path;
char *protocol;
int (*initfunc) ARG((int));
{
	int s;
	s = initunixserver(path,protocol);
	if (s<0) {
		return -1;
	}
	if (initfunc != NULL) {
	   printf("in inetsingleserver before initfunc s %d\n",s);
	   if ((*initfunc)(s)<0) {
		perror("initfunc error");
		return -1;
	   }
	   printf("end inetsingleserver before initfunc \n");
        }
	return s;
}

int open_listen(service,protocol,initfunc)
char *service;
char *protocol;
int (*initfunc) ARG((int));
{
	int s;
	if (! INETDstart)
	  s = initinetserver(service,protocol);
        else
	  s = 0;
	if (s<0) {
		return -1;
	}
	if (initfunc != NULL) {
	   printf("in inetsingleserver before initfunc s %d\n",s);
	   if ((*initfunc)(s)<0) {
		perror("initfunc error");
		return -1;
	   }
	   printf("end inetsingleserver before initfunc \n");
        }
	return s;
}

int inetsingleserver(service,protocol,serverfunc,initfunc)
char *service;
char *protocol;
int (*initfunc) ARG((int));
int (*serverfunc) ARG((int));
{
	int s;
	if (!INETDstart)
	  s = initinetserver(service,protocol);
        else
	  s = 0;
	if (s<0) {
		return -1;
	}
	if (initfunc != NULL) {
	   printf("in inetsingleserver before initfunc s %d\n",s);
	   if ((*initfunc)(s)<0) {
		perror("initfunc error");
		return -1;
	   }
	   printf("end inetsingleserver before initfunc \n");
        }
	{
		int ns=tryaccept(s);
		int result=0;
		if (ns < 0 && errno != EINTR){
#ifdef DEBUGSERVER
			perror("accept");
#endif
		}
		close(s);
		if (serverfunc != NULL)
		   result = (*serverfunc)(ns);
		close(ns);
		return(result);
	}
}


int tryaccept(s)
int s;
{
	int ns,fromlen;
	struct sockaddr sockaddr;/*Internet endpoint address*/
	fromlen=sizeof (struct sockaddr_in);

#ifdef DEBUGSERVER
	fputs("Listening again\n",stdout);
#endif
	do {
	   ns = accept(s,&sockaddr,&fromlen);
	   errno = 0;
	} while ( ns < 0 && errno == EINTR );
	return ns;
}

int inetserver(service,protocol,serverfunc)
char *service;
char *protocol;
int (*serverfunc) ARG((int));
{
	int port,s;

        if (!INETDstart)
	 s = initinetserver(service,protocol);
        else
	 s = 0;
	if (s<0) {
		return -1;
	}
	for (;;) {
		int ns=tryaccept(s);
		int result=0;
		int pid;
		if (ns < 0 && errno != EINTR){
#ifdef DEBUGSERVER
			perror("accept");
#endif
			continue;
		}
#ifdef DEBUGSERVER
		fputs("Accept OK\n",stdout);
#endif
		pid = fork();
		if (pid==0) {
			close(s);
			if (serverfunc != NULL)
			   result = (*serverfunc)(ns);
			close(ns);
			exit(result);
		} else if (pid < 0) {
			perror("fork");
			return -1;
		}
		close(ns);
	}
	return 0;
}

int inetclient(server,service,protocol) 
char *server;
char *protocol;
char *service;
{
        struct servent *se;    /*service information entry*/
	struct hostent *he;    /*host information entry*/
	struct protoent *pe;   /*protocol information entry*/
	struct sockaddr_in sin;/*Internet endpoint address*/
	int port,s;

	bzero((char*)&sin,sizeof(sin));
	sin.sin_family= AF_INET;

	if (service==NULL) 
	     service=DEFAULTPORT;
	if (protocol==NULL)
	     protocol="tcp";
	if (server==NULL)
	     server=DEFAULTSERVER;
        /* map service name to port number */
	/* service ---> port */
	se = getservbyname(service,protocol);
	if (se==NULL) {
	  port = htons((u_short)atoi(service)); 
	  if (port==0) {
	   fprintf (stderr, "%s/%s: Unknown service.\n",service,protocol);
	   return (-1);
	  }
        } else 
	        port=se->s_port;
	sin.sin_port = port;

	/* map server hostname to IP address, allowing for dotted decimal */
	he=gethostbyname(server);
	if (he==NULL) {
	  sin.sin_addr.s_addr = inet_addr(server);
	  if (sin.sin_addr.s_addr==INADDR_NONE) {
	   fprintf (stderr, "%s: Unknown host.\n",server);
	   return (-1);
	  }
	} else
	  bcopy(he->h_addr,(char*)&sin.sin_addr,he->h_length);

        /* map protocol name to protocol number */
	pe=getprotobyname(protocol);
	if (pe==NULL) {
	 fprintf(stderr,"%s: Unknown protocol.\n",protocol);
	 return (-1);
	}

        /* Allocate a socket */
	s = socket(PF_INET,strcmp(protocol,"tcp")?SOCK_DGRAM:SOCK_STREAM,pe->p_proto);
	if (s<0) {
		perror("socket");
		return -1;
	}

       if (setjmp(timebuf) == 0)
       {
        signal(SIGALRM, timeout);
        alarm(5);
        if (connect(s,(struct sockaddr*)&sin,sizeof(sin))<0)
          {
                alarm(0);
                return -1;
          }
       }
       else
       {
                alarm(0);
                return -1;
       }
        alarm(0);

	return s;
}

int unixclient(path,protocol) 
char *path;
char *protocol;
{
	struct protoent *pe;    /*protocol information entry*/
	struct sockaddr_un s_un;/*unix endpoint address*/
	int s;

	bzero((char*)&s_un,sizeof(s_un));
	s_un.sun_family= AF_UNIX;

	if (path==NULL) 
	     path=DEFAULTPATH;
	if (protocol==NULL)
	     protocol="tcp";
	strcpy(s_un.sun_path , path);

        /* map protocol name to protocol number */
	pe=getprotobyname(protocol);
	if (pe==NULL) {
	 fprintf(stderr,"%s: Unknown protocol.\n",protocol);
	 return (-1);
	}
        /* Allocate a socket */
	s = socket(PF_UNIX,strcmp(protocol,"tcp")?SOCK_DGRAM:SOCK_STREAM,0);
	if (s<0) {
		perror("socket");
		return -1;
	}
	/* Connect the socket to the server */
	if (connect(s,(struct sockaddr*)&s_un,sizeof(s_un))<0) {
		/*perror("connect");*/
		return -1;
	}
	return s;
}
