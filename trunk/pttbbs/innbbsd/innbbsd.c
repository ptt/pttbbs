#include "innbbsconf.h"
#include "daemon.h"
#include "innbbsd.h"
#include <dirent.h>
#include "bbslib.h"
#include "inntobbs.h"
#include "nntp.h"

#ifdef GETRUSAGE
#include <sys/time.h>
#include <sys/resource.h>
#endif

#ifdef STDC
# ifndef ARG
#   define ARG(x) (x)
# else
#   define ARG(x) ()
# endif
#endif

/*<  add <mid> <recno> ...
>  200 OK
<  quit
 500 BYE

>  300 DBZ Server ...
<  query <mid>
>  250 <recno> ...
>  450 NOT FOUND!
*/

static int CMDhelp ARG((ClientType*));
static int CMDquit ARG((ClientType*));
static int CMDihave ARG((ClientType*));
static int CMDstat ARG((ClientType*));
static int CMDaddhist ARG((ClientType*));
static int CMDgrephist ARG((ClientType*));
static int CMDmidcheck ARG((ClientType*));
static int CMDshutdown ARG((ClientType*));
static int CMDmode ARG((ClientType*));
static int CMDreload ARG((ClientType*));
static int CMDhismaint ARG((ClientType*));
static int CMDverboselog ARG((ClientType*));
static int CMDlistnodelist ARG((ClientType*));
static int CMDlistnewsfeeds ARG((ClientType*));

#ifdef GETRUSAGE
static int CMDgetrusage ARG((ClientType*));
static int CMDmallocmap ARG((ClientType*));
#endif

static daemoncmd_t cmds[]=
/*  cmd-name, cmd-usage, min-argc, max-argc, errorcode, normalcode, cmd-func */
{ {"help","help [cmd]",1,2,99,100,CMDhelp},
  {"quit","quit",1,0,99,100,CMDquit},
#ifndef DBZSERVER
  {"ihave","ihave mid",2,2,435,335,CMDihave},
#endif
  {"stat","stat mid",2,2,223,430,CMDstat},
  {"addhist","addhist <mid> <path>",3,3, NNTP_ADDHIST_BAD, NNTP_ADDHIST_OK,CMDaddhist},
  {"grephist","grephist <mid>",2,2, NNTP_GREPHIST_BAD, NNTP_GREPHIST_OK, CMDgrephist},
  {"midcheck","midcheck [on|off]",1,2, NNTP_MIDCHECK_BAD, NNTP_MIDCHECK_OK, CMDmidcheck},
  {"shutdown","shutdown (local)",1,1, NNTP_SHUTDOWN_BAD, NNTP_SHUTDOWN_OK, CMDshutdown},
  {"mode","mode (local)",1,1, NNTP_MODE_BAD, NNTP_MODE_OK, CMDmode},
  {"listnodelist","listnodelist (local)",1,1, NNTP_MODE_BAD, NNTP_MODE_OK, CMDlistnodelist},
  {"listnewsfeeds","listnewsfeeds (local)",1,1, NNTP_MODE_BAD, NNTP_MODE_OK, CMDlistnewsfeeds},
  {"reload","reload (local)",1,1, NNTP_RELOAD_BAD, NNTP_RELOAD_OK, CMDreload},
  {"hismaint","hismaint (local)",1,1, NNTP_RELOAD_BAD, NNTP_RELOAD_OK, CMDhismaint},
  {"verboselog","verboselog [on|off](local)",1,2, NNTP_VERBOSELOG_BAD, NNTP_VERBOSELOG_OK, CMDverboselog},
#ifdef GETRUSAGE
  {"getrusage","getrusage (local)",1,1, NNTP_MODE_BAD, NNTP_MODE_OK, CMDgetrusage},
#endif
#ifdef MALLOCMAP
  {"mallocmap","mallocmap (local)",1,1, NNTP_MODE_BAD, NNTP_MODE_OK, CMDmallocmap},
#endif
  {NULL,NULL,0,0,99,100,NULL}
};

installinnbbsd()
{
	installdaemon(cmds,100,NULL);
}

#ifdef OLDLIBINBBSINND
testandmkdir(dir)
char *dir;
{
        if (!isdir(dir)) {
               char path[MAXPATHLEN+12];
               sprintf(path,"mkdir -p %s",dir);
               system(path);
        }
}
                                                
static char splitbuf[2048]; 
static char joinbuf[1024]; 
#define MAXTOK 50 
static char* Splitptr[MAXTOK];
char **split(line,pat)
char *line,*pat;
{
	char *p;
	int i;

	for (i=0;i<MAXTOK;++i) Splitptr[i] = NULL;
	strncpy(splitbuf,line,sizeof splitbuf - 1 );
	/*printf("%d %d\n",strlen(line),strlen(splitbuf));*/
	splitbuf[sizeof splitbuf - 1] = '\0';
	for (i=0,p=splitbuf;*p && i< MAXTOK -1 ;){
	   for (Splitptr[i++]=p;*p && !strchr(pat,*p);p++);
	   if (*p=='\0') break;
	   for (*p++='\0'; *p && strchr(pat,*p);p++);
	}
	return Splitptr;
}

char **BNGsplit(line) 
char *line;
{
   char **ptr = split(line,",");
   newsfeeds_t *nf1, *nf2;
   char *n11, *n12, *n21, *n22;
   int i,j;
   for (i=0; ptr[i] != NULL; i++) {
      nf1 = (newsfeeds_t*)search_group(ptr[i]);
      for (j=i+1; ptr[j] != NULL; j++) {
	 if (strcmp(ptr[i],ptr[j])==0) {
	    *ptr[j] = '\0';    
	    continue;
         }
	 nf2 = (newsfeeds_t*)search_group(ptr[j]);
	 if (nf1 && nf2) {
	   if (strcmp(nf1->board,nf2->board)==0) {
	      *ptr[j] = '\0';    
	      continue;
	   }
	   for (n11 = nf1->board, n12 = (char*)strchr(n11,',');
		n11 && *n11 ; n12 = (char*) strchr(n11,',')) {
		if (n12) *n12 = '\0';
	       for (n21 = nf2->board, n22 = (char*)strchr(n21,',');
	  	    n21 && *n21 ; n22 = (char*) strchr(n21,',')) {
		    if (n22) *n22 = '\0';
	            if (strcmp(n11,n21)==0) {
		      *n21 = '\t';
                    }
		    if (n22) {
		      *n22 = ',';
		      n21 = n22 + 1;
                    } else  
		      break;
                }
		if (n12) {
		      *n12 = ',';
		      n11 = n12 +1;
                } else  
		  break;
	   }
	 }
      }
   }
   return ptr;
}

char **ssplit(line,pat)
char *line,*pat;
{
	char *p;
	int i;
	for (i=0;i<MAXTOK;++i) Splitptr[i] = NULL;
	strncpy(splitbuf,line,1024);
	for (i=0,p=splitbuf;*p && i< MAXTOK;){
	   for (Splitptr[i++]=p;*p && !strchr(pat,*p);p++);
	   if (*p=='\0') break;
	   *p=0;p++;
/*	   for (*p='\0'; strchr(pat,*p);p++);*/
	}
	return Splitptr;
}

char *join(lineptr,pat,num)
char **lineptr,*pat;
int num;
{
	int i;
	joinbuf[0] = '\0';
	if (lineptr[0] != NULL)
		strncpy(joinbuf,lineptr[0],1024);
	else  {
		joinbuf[0]='\0';
		return joinbuf; 
	}
	for (i=1;i<num;i++) {
		strcat(joinbuf,pat);
		if (lineptr[i] != NULL)
		   strcat(joinbuf,lineptr[i]);
		else 
		   break; 
	}
	return joinbuf;
}

#endif

static int CMDtnrpd(client) 
ClientType *client;
{
        argv_t *argv = &client->Argv;
	fprintf(argv->out,"%d %s\n",argv->dc->usage);
	return 0;
}

islocalconnect(client)
ClientType *client;
{
	if (strcmp(client->username,"localuser") != 0 ||
	    strcmp(client->hostname,"localhost") != 0) 
           return 0;
       return 1;
}

static shutdownflag = 0;
INNBBSDhalt()
{
   shutdownflag = 1;
}

int INNBBSDshutdown()
{
   return shutdownflag;
}

static int CMDshutdown(client)
ClientType *client;
{
        argv_t *argv = &client->Argv;
	buffer_t *in = &client->in;
	daemoncmd_t *p = argv->dc;
	if (!islocalconnect(client)) {
		fprintf(argv->out,"%d shutdown access denied\r\n", p->errorcode); 
		fflush(argv->out);
		verboselog("Shutdown Put: %d shutdown access denied\n", p->errorcode);
		return 1;
	}
	shutdownflag = 1;
	fprintf(argv->out,"%d shutdown starting\r\n", p->normalcode); 
	fflush(argv->out);
	verboselog("Shutdown Put: %d shutdown starting\n", p->normalcode);
	return 1;
}

static int CMDmode(client)
ClientType *client;
{
        /*char cwdpath[MAXPATHLEN+1];*/
        argv_t *argv = &client->Argv;
	extern ClientType INNBBSD_STAT;
	buffer_t *in = &client->in;
	daemoncmd_t *p = argv->dc;
	time_t uptime, now;
	int i,j;
	time_t lasthist;
	ClientType *client1 = &INNBBSD_STAT;
        
	if (!islocalconnect(client)) {
		fprintf(argv->out,"%d mode access denied\r\n", p->errorcode); 
		fflush(argv->out);
		verboselog("Mode Put: %d mode access denied\n", p->errorcode);
		return 1;
	}
	fprintf(argv->out,"%d mode\r\n", p->normalcode); 
	fflush(argv->out);
	verboselog("Mode Put: %d mode\n", p->normalcode);
	uptime = innbbsdstartup();
	time(&now);
	fprintf(argv->out,"up since %salive %.2f days\r\n", ctime(&uptime), (double)(now - innbbsdstartup())/86400);
	fprintf(argv->out,"BBSHOME %s\r\n", BBSHOME);
	fprintf(argv->out,"MYBBSID %s\r\n", MYBBSID);
	fprintf(argv->out,"ECHOMAIL %s\r\n", ECHOMAIL);
	fprintf(argv->out,"INNDHOME %s\r\n", INNDHOME);
	fprintf(argv->out,"HISTORY %s\r\n", HISTORY);
	fprintf(argv->out,"LOGFILE %s\r\n", LOGFILE);
	fprintf(argv->out,"INNBBSCONF %s\r\n", INNBBSCONF);
	fprintf(argv->out,"BBSFEEDS %s\r\n", BBSFEEDS);
	fprintf(argv->out,"Verbose log: %s\r\n", isverboselog() ?"ON":"OFF");
	fprintf(argv->out,"History Expire Days %d\r\n", Expiredays);
	fprintf(argv->out,"History Expire Time %d:%d\r\n", His_Maint_Hour, His_Maint_Min);
	lasthist = gethisinfo();
	if (lasthist > 0) {
	   time_t keep = lasthist, keep1;
	   time(&now);
	   fprintf(argv->out,"Oldest history entry created: %s",(char*)ctime(&keep));
	   keep = Expiredays * 86400 * 2 + lasthist;
	   keep1 = keep - now ;
	   fprintf(argv->out,"Next time to maintain history: (%.2f days later) %s",(double)keep1/86400, (char*)ctime(&keep));
	}
	fprintf(argv->out,"PID is %d\r\n", getpid());
	fprintf(argv->out,"LOCAL ONLY %d\r\n", LOCALNODELIST);
	fprintf(argv->out,"NONE NEWSFEEDS %d\r\n", NONENEWSFEEDS);
	fprintf(argv->out,"Max connections %d\r\n", Maxclient);
#ifdef DEBUGCWD
	getwd(cwdpath);
	fprintf(argv->out,"Working directory %s\r\n", cwdpath);
#endif
	if (Channel)
	for (i=0, j=0; i< Maxclient; ++i) {
	  if (Channel[i].fd == -1) continue;
	  if (Channel+i == client) continue;
	  j++;
	  fprintf(argv->out," %d) in->used %d, in->left %d %s@%s\r\n",i,
		  Channel[i].in.used, Channel[i].in.left, 
		  Channel[i].username,Channel[i].hostname);
	}
	fprintf(argv->out,"Total connections %d\r\n", j);
	fprintf(argv->out,"Total rec: %d dup: %d fail: %d size: %d, stat rec: %d fail: %d\n", client1->ihavecount, client1->ihaveduplicate, client1->ihavefail, client1->ihavesize, client1->statcount, client1->statfail);
	fprintf(argv->out,".\r\n");
	fflush(argv->out);
	return 1;
}

static int 
CMDlistnodelist(client)
ClientType *client;
{
        int nlcount;
        argv_t *argv = &client->Argv;
	buffer_t *in = &client->in;
	daemoncmd_t *p = argv->dc;
	if (!islocalconnect(client)) {
		fprintf(argv->out,"%d listnodelist access denied\r\n", p->errorcode); 
		fflush(argv->out);
		verboselog("Mallocmap Put: %d listnodelist access denied\n", p->errorcode);
		return 1;
	}
	fprintf(argv->out,"%d listnodelist\r\n", p->normalcode); 
	for (nlcount =0; nlcount < NLCOUNT; nlcount++) {
	  nodelist_t *nl = NODELIST+nlcount;
	  fprintf(argv->out,"%2d %s /\\/\\ %s\r\n", nlcount+1, nl->node==NULL?"":nl->node, nl->exclusion==NULL?"":nl->exclusion); 
	  fprintf(argv->out,"   %s:%s:%s\r\n",nl->host==NULL?"":nl->host, nl->protocol==NULL?"":nl->protocol, nl->comments == NULL ? "": nl->comments);
	}
	fprintf(argv->out,".\r\n");
	fflush(argv->out);
	verboselog("Listnodelist Put: %d listnodelist complete\n", p->normalcode);
	return 1;
}

static int 
CMDlistnewsfeeds(client)
ClientType *client;
{
        argv_t *argv = &client->Argv;
	buffer_t *in = &client->in;
	daemoncmd_t *p = argv->dc;
	int nfcount;
	if (!islocalconnect(client)) {
		fprintf(argv->out,"%d listnewsfeeds access denied\r\n", p->errorcode); 
		fflush(argv->out);
		verboselog("Mallocmap Put: %d listnewsfeeds access denied\n", p->errorcode);
		return 1;
	}
	fprintf(argv->out,"%d listnewsfeeds\r\n", p->normalcode); 
	for (nfcount =0; nfcount < NFCOUNT; nfcount++) {
	  newsfeeds_t *nf = NEWSFEEDS + nfcount;
	  fprintf(argv->out,"%3d %s<=>%s\r\n",nfcount+1, nf->newsgroups, nf->board);
	  fprintf(argv->out,"    %s\r\n",nf->path==NULL?"(Null)":nf->path);
	}
	fprintf(argv->out,".\r\n");
	fflush(argv->out);
	verboselog("Listnewsfeeds Put: %d listnewsfeeds complete\n", p->normalcode);
	return 1;
}

#ifdef MALLOCMAP
static int CMDmallocmap(client)
ClientType *client;
{
        argv_t *argv = &client->Argv;
	buffer_t *in = &client->in;
	daemoncmd_t *p = argv->dc;
	struct rusage ru;
	int savefd ;
	if (!islocalconnect(client)) {
		fprintf(argv->out,"%d mallocmap access denied\r\n", p->errorcode); 
		fflush(argv->out);
		verboselog("Mallocmap Put: %d mallocmap access denied\n", p->errorcode);
		return 1;
	}
	fprintf(argv->out,"%d mallocmap\r\n", p->normalcode); 
	savefd = dup(1);
	dup2(client->fd, 1);
	mallocmap();
	dup2(savefd, 1);
	close(savefd);
	fprintf(argv->out,".\r\n");
	fflush(argv->out);
	verboselog("Mallocmap Put: %d mallocmap complete\n", p->normalcode);
	return 1;
}
#endif

#ifdef GETRUSAGE
static int CMDgetrusage(client)
ClientType *client;
{
        argv_t *argv = &client->Argv;
	buffer_t *in = &client->in;
	daemoncmd_t *p = argv->dc;
	struct rusage ru;
	if (!islocalconnect(client)) {
		fprintf(argv->out,"%d getrusage access denied\r\n", p->errorcode); 
		fflush(argv->out);
		verboselog("Getrusage Put: %d getrusage access denied\n", p->errorcode);
		return 1;
	}
	fprintf(argv->out,"%d getrusage\r\n", p->normalcode); 
	if (getrusage(RUSAGE_SELF,&ru) == 0) {
	  fprintf(argv->out,"user time used: %.6f\r\n",(double)ru.ru_utime.tv_sec + (double)ru.ru_utime.tv_usec/1000000.0); 
	  fprintf(argv->out,"system time used: %.6f\r\n",(double)ru.ru_stime.tv_sec + (double)ru.ru_stime.tv_usec/1000000.0); 
	  fprintf(argv->out,"maximum resident set size: %lu\r\n",ru.ru_maxrss * getpagesize()); 
	  fprintf(argv->out,"integral resident set size: %lu\r\n",ru.ru_idrss * getpagesize()); 
	  fprintf(argv->out,"page faults not requiring physical I/O: %d\r\n",ru.ru_minflt); 
	  fprintf(argv->out,"page faults requiring physical I/O: %d\r\n",ru.ru_majflt); 
	  fprintf(argv->out,"swaps: %d\r\n",ru.ru_nswap); 
	  fprintf(argv->out,"block input operations: %d\r\n",ru.ru_inblock); 
	  fprintf(argv->out,"block output operations: %d\r\n",ru.ru_oublock); 
	  fprintf(argv->out,"messages sent: %d\r\n",ru.ru_msgsnd); 
	  fprintf(argv->out,"messages received: %d\r\n",ru.ru_msgrcv); 
	  fprintf(argv->out,"signals received: %d\r\n",ru.ru_nsignals); 
	  fprintf(argv->out,"voluntary context switches: %d\r\n",ru.ru_nvcsw); 
	  fprintf(argv->out,"involuntary context switches: %d\r\n",ru.ru_nivcsw); 
	}
	fprintf(argv->out,".\r\n");
	fflush(argv->out);
	verboselog("Getrusage Put: %d getrusage complete\n", p->normalcode);
	return 1;
}

#endif

static int CMDhismaint(client)
ClientType *client;
{
        argv_t *argv = &client->Argv;
	buffer_t *in = &client->in;
	daemoncmd_t *p = argv->dc;
	if (!islocalconnect(client)) {
		fprintf(argv->out,"%d hismaint access denied\r\n", p->errorcode); 
		fflush(argv->out);
		verboselog("Hismaint Put: %d hismaint access denied\n", p->errorcode);
		return 1;
	}
	verboselog("Hismaint Put: %d hismaint start\n", p->normalcode);
	HISmaint();
	fprintf(argv->out,"%d hismaint complete\r\n", p->normalcode); 
	fflush(argv->out);
	verboselog("Hismaint Put: %d hismaint complete\n", p->normalcode);
	return 1;
}

static int CMDreload(client)
ClientType *client;
{
        argv_t *argv = &client->Argv;
	buffer_t *in = &client->in;
	daemoncmd_t *p = argv->dc;
	if (!islocalconnect(client)) {
		fprintf(argv->out,"%d reload access denied\r\n", p->errorcode); 
		fflush(argv->out);
		verboselog("Reload Put: %d reload access denied\n", p->errorcode);
		return 1;
	}
	initial_bbs("feed");
	fprintf(argv->out,"%d reload complete\r\n", p->normalcode); 
	fflush(argv->out);
	verboselog("Reload Put: %d reload complete\n", p->normalcode);
	return 1;
}

static int CMDverboselog(client)
ClientType *client;
{
        argv_t *argv = &client->Argv;
	buffer_t *in = &client->in;
	daemoncmd_t *p = argv->dc;
	if (!islocalconnect(client)) {
		fprintf(argv->out,"%d verboselog access denied\r\n", p->errorcode); 
		fflush(argv->out);
		verboselog("Reload Put: %d verboselog access denied\n", p->errorcode);
		return 1;
	}
	if (client->mode == 0) {
	   if (argv->argc > 1) {
	     if (strcasecmp(argv->argv[1],"off")==0) {
		setverboseoff();
	     } else {
		setverboseon();
	     }
	   } 
	}
       fprintf(argv->out,"%d verboselog %s\r\n",p->normalcode, 
	     isverboselog() ?"ON":"OFF");
       fflush(argv->out);
       verboselog("%d verboselog %s\r\n",p->normalcode, 
		 isverboselog()?"ON":"OFF");
}

static int CMDmidcheck(client)
ClientType *client;
{
        argv_t *argv = &client->Argv;
	buffer_t *in = &client->in;
	daemoncmd_t *p = argv->dc;
	if (client->mode == 0) {
	   if (argv->argc > 1) {
	     if (strcasecmp(argv->argv[1],"off")==0) {
		client->midcheck = 0;
	     } else {
		client->midcheck = 1;
	     }
	   } 
	}
       fprintf(argv->out,"%d mid check %s\r\n",p->normalcode, 
	     client->midcheck == 1?"ON":"OFF");
       fflush(argv->out);
       verboselog("%d mid check %s\r\n",p->normalcode, 
		 client->midcheck == 1?"ON":"OFF");
}

static int CMDgrephist(client)
ClientType *client;
{
        argv_t *argv = &client->Argv;
	buffer_t *in = &client->in;
	daemoncmd_t *p = argv->dc;
	if (client->mode == 0) {
	  if (argv->argc > 1) {
	    char *ptr;
	    ptr = (char*)DBfetch(argv->argv[1]);
	    if (ptr != NULL) {
		fprintf(argv->out,"%d %s OK\r\n", p->normalcode, ptr); 
		fflush(argv->out);
		verboselog("Addhist Put: %d %s OK\n", p->normalcode, ptr);
		return 0;
	    } else {
		fprintf(argv->out,"%d %s not found\r\n", p->errorcode,argv->argv[1]); 
		fflush(argv->out);
		verboselog("Addhist Put: %d %s not found\n", p->errorcode, argv->argv[1]);
		return 1;
	    }
          }
	}
        fprintf(argv->out,"%d grephist error\r\n", p->errorcode); 
	fflush(argv->out);
	verboselog("Addhist Put: %d grephist error\n", p->errorcode);
	return 1;
}


static int CMDaddhist(client)
ClientType *client;
{
        argv_t *argv = &client->Argv;
	buffer_t *in = &client->in;
	daemoncmd_t *p = argv->dc;
	/*
	if (strcmp(client->username,"localuser") != 0 ||
	    strcmp(client->hostname,"localhost") != 0) {
		fprintf(argv->out,"%d add hist access denied\r\n", p->errorcode); 
		fflush(argv->out);
		verboselog("Addhist Put: %d add hist access denied\n", p->errorcode);
		return 1;
	}
	*/
	if (client->mode == 0) {
	  if (argv->argc > 2) {
	    char *ptr;
	    ptr = (char*)DBfetch(argv->argv[1]);
	    if (ptr == NULL) {
	       if (storeDB(argv->argv[1], argv->argv[2]) < 0) {
		fprintf(argv->out,"%d add hist store DB error\r\n", p->errorcode); 
		fflush(argv->out);
		verboselog("Addhist Put: %d add hist store DB error\n", p->errorcode);
		return 1;
	       } else {
		fprintf(argv->out,"%d add hist OK\r\n", p->normalcode); 
		fflush(argv->out);
		verboselog("Addhist Put: %d add hist OK\n", p->normalcode);
		return 0;
	       }
	    } else {
		fprintf(argv->out,"%d add hist duplicate error\r\n", p->errorcode); 
		fflush(argv->out);
		verboselog("Addhist Put: %d add hist duplicate error\n", p->errorcode);
		return 1;
	    }
	  }
	} 
        fprintf(argv->out,"%d add hist error\r\n", p->errorcode); 
	fflush(argv->out);
	verboselog("Addhist Put: %d add hist error\n", p->errorcode);
	return 1;
}

static int CMDstat(client)
ClientType *client;
{
        argv_t *argv = &client->Argv;
	char *ptr, *frontptr;
	buffer_t *in = &client->in;
	daemoncmd_t *p;
	if (client->mode == 0) {
	  client->statcount++;
	  if (argv->argc > 1) {
	   if (argv->argv[1][0] != '<') {
	     fprintf(argv->out,"430 No such article\r\n");
	     fflush(argv->out);
	     verboselog("Stat Put: 430 No such article\n");
	     client->statfail++;
	     return 0;
	   }
	   ptr = (char*)DBfetch(argv->argv[1]);
	   if (ptr != NULL) {
	      fprintf(argv->out,"223 0 status %s\r\n",argv->argv[1]);
	      fflush(argv->out);
	      client->mode = 0;
	      verboselog("Stat Put: 223 0 status %s\n",argv->argv[1]);
	      return 1;
	   } else {
	    fprintf(argv->out,"430 No such article\r\n");
	    fflush(argv->out);
	    verboselog("Stat Put: 430 No such article\n");
	    client->mode = 0;
	    client->statfail++;
	   }
          }
	} 
}

#ifndef DBZSERVER
static int CMDihave(client)
ClientType *client;
{
        argv_t *argv = &client->Argv;
	char *ptr=NULL, *frontptr;
	buffer_t *in = &client->in;
	daemoncmd_t *p;
	if (client->mode == 0) {
	  client->ihavecount++;
	  if (argv->argc > 1) {
	   if (argv->argv[1][0] != '<') {
	     fprintf(argv->out,"435 Bad Message-ID\r\n");
	     fflush(argv->out);
	     verboselog("Ihave Put: 435 Bad Message-ID\n");
	     client->ihavefail++;
	     return 0;
	   }
	   if (client->midcheck == 1)
	      ptr = (char*)DBfetch(argv->argv[1]);
	   if (ptr != NULL && client->midcheck == 1) {
	      fprintf(argv->out,"435 Duplicate\r\n");
	      fflush(argv->out);
	      client->mode = 0;
	      verboselog("Ihave Put: 435 Duplicate\n");
	      client->ihaveduplicate++;
	      client->ihavefail++;
	      return 1;
	   } else {
	    fprintf(argv->out,"335\r\n");
	    fflush(argv->out);
	    client->mode = 1;
	    verboselog("Ihave Put: 335\n");
	   }
          }
	} else {
	      client->mode = 0;
	      readlines(client);
	      if (HEADER[SUBJECT_H] && HEADER[FROM_H] && HEADER[DATE_H] &&
		  HEADER[MID_H] && HEADER[NEWSGROUPS_H] ) {
                 char *path1, *path2;
		 int rel ;
		 strcpy(HEADER[SUBJECT_H], str_decode_M3(HEADER[SUBJECT_H]));
		 strcpy(HEADER[FROM_H], str_decode_M3(HEADER[FROM_H]));
		 strcpy(HEADER[DATE_H], str_decode_M3(HEADER[DATE_H]));
		 strcpy(HEADER[MID_H], str_decode_M3(HEADER[MID_H]));
		 strcpy(HEADER[NEWSGROUPS_H], str_decode_M3(HEADER[NEWSGROUPS_H]));
		 rel = 0;
		 path1 = (char*)mymalloc(strlen(HEADER[PATH_H]) + 3);
		 path2 = (char*)mymalloc(strlen(MYBBSID) + 3);
		 sprintf(path1, "!%s!",HEADER[PATH_H]);
		 sprintf(path2, "!%s!",MYBBSID);
                 if (HEADER[CONTROL_H]) {
		   bbslog( "Control: %s\n", HEADER[CONTROL_H] );
		   if (strncasecmp(HEADER[CONTROL_H],"cancel ",7)==0) {
		      rel = cancel_article_front(HEADER[CONTROL_H]+7);
		   } else {
		      rel = receive_control();
		   }
		 } else if ( (char*)strstr(path1, path2) != NULL) {
		   bbslog( ":Warn: Loop back article: %s!%s\n",MYBBSID,HEADER[PATH_H] );
		 } else {
                   rel = receive_article();
                 }
		 free(path1);
		 free(path2);
		 if (rel == -1) {
	          fprintf(argv->out,"400 server side failed\r\n");
		  fflush(argv->out);
	          verboselog("Ihave Put: 400\n");
		  clearfdset(client->fd);
		  fclose(client->Argv.in);
		  fclose(client->Argv.out);
		  close(client->fd);
		  client->fd = -1;
		  client->mode = 0;
	          client->ihavefail++;
		  return;
		 } else {
	          fprintf(argv->out,"235\r\n");
	          verboselog("Ihave Put: 235\n");
		 }
	         fflush(argv->out);
              } else if (!HEADER[PATH_H]) {
		fprintf(argv->out,"437 No Path in \"ihave %s\" header\r\n",HEADER[MID_H]);
	        fflush(argv->out);
		verboselog("Put: 437 No Path in \"ihave %s\" header\n",HEADER[MID_H]);
	        client->ihavefail++;
	      } else {
		fprintf(argv->out,"437 No colon-space in \"ihave %s\" header\r\n",HEADER[MID_H]);
	        fflush(argv->out);
		verboselog("Ihave Put: 437 No colon-space in \"ihave %s\" header\n",HEADER[MID_H]);
	        client->ihavefail++;
	      }
#ifdef DEBUG
	      printf("subject is %s\n",HEADER[SUBJECT_H]);
	      printf("from is %s\n",HEADER[FROM_H]);
	      printf("Date is %s\n",HEADER[DATE_H]);
	      printf("Newsgroups is %s\n",HEADER[NEWSGROUPS_H]);
	      printf("mid is %s\n",HEADER[MID_H]);
	      printf("path is %s\n",HEADER[PATH_H]);
#endif
	}
	fflush(argv->out);
	return 0;
}
#endif

static int CMDhelp(client)
ClientType *client;
{
        argv_t *argv = &client->Argv;
	daemoncmd_t *p;
	if (argv->argc>=1) {
		fprintf(argv->out,"%d Available Commands\r\n",argv->dc->normalcode);
		for (p=cmds;p->name !=NULL;p++) {
			fprintf(argv->out,"  %s\r\n",p->usage);
		}
		fprintf(argv->out,"Report problems to %s\r\n",ADMINUSER);
	} 
	fputs(".\r\n",argv->out);
	fflush(argv->out);
	client->mode = 0;
	return 0;
}

static int CMDquit(client) 
ClientType *client;
{
        argv_t *argv = &client->Argv;
	fprintf(argv->out,"205 quit\r\n");	
	fflush(argv->out);
	verboselog("Quit Put: 205 quit\n");	
	clearfdset(client->fd);
	fclose(client->Argv.in);
	fclose(client->Argv.out);
	close(client->fd);
	client->fd = -1;
	client->mode = 0;
	channeldestroy(client);
	/*exit(0);*/
}
