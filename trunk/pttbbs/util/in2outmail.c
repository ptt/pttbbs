/* $Id: in2outmail.c,v 1.1 2002/03/07 15:13:46 in2 Exp $ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/file.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "config.h"
#include "pttstruct.h"


#ifdef HAVE_SETPROCTITLE

#include <sys/types.h>
#include <libutil.h>

void initsetproctitle(int argc, char **argv, char **envp) {
}

#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

char **Argv = NULL;          /* pointer to argument vector */
char *LastArgv = NULL;       /* end of argv */
extern char **environ;

void initsetproctitle(int argc, char **argv, char **envp) {
    register int i;
    
    /* Move the environment so setproctitle can use the space at
       the top of memory. */
    for(i = 0; envp[i]; i++);
    environ = malloc(sizeof(char *) * (i + 1));
    for(i = 0; envp[i]; i++)
	environ[i] = strdup(envp[i]);
    environ[i] = NULL;
    
    /* Save start and extent of argv for setproctitle. */
    Argv = argv;
    if(i > 0)
	LastArgv = envp[i - 1] + strlen(envp[i - 1]);
    else
	LastArgv = argv[argc - 1] + strlen(argv[argc - 1]);
}

static void do_setproctitle(const char *cmdline) {
    char buf[256], *p;
    int i;
    
    strncpy(buf, cmdline, 256);
    buf[255] = '\0';
    i = strlen(buf);
    if(i > LastArgv - Argv[0] - 2) {
	i = LastArgv - Argv[0] - 2;
    }
    strcpy(Argv[0], buf);
    p = &Argv[0][i];
    while(p < LastArgv)
	*p++='\0';
    Argv[1] = NULL;
}

void setproctitle(const char* format, ...) {
    char buf[256];
    
    va_list args;
    va_start(args, format);
    vsprintf(buf, format,args);
    do_setproctitle(buf);
    va_end(args);
}
#endif





#define SPOOL BBSHOME "/out"
#define INDEX SPOOL "/.DIR"
#define NEWINDEX SPOOL "/.DIR.sending"
#define FROM ".bbs@" MYHOSTNAME
#define SMTPPORT 25

int waitReply(int sock) {
    char buf[256];
    
    if(read(sock, buf, sizeof(buf)) <= 0)
	return -1;
    else
	return buf[0] - '0';
}

int sendRequest(int sock, char *request) {
    return write(sock, request, strlen(request)) < 0 ? -1 : 0;
}

int connectMailServer(char *host) {
    int sock;
    struct sockaddr_in addr;
    
    if((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
	perror("socket");
	return -1;
    }
    
    memset(&addr, 0, sizeof(addr));
#ifdef FreeBSD
    addr.sin_len = sizeof(addr);
#endif
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SMTPPORT);
    addr.sin_addr.s_addr = inet_addr(host);
    
    if(connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	perror(RELAY_SERVER_IP);
	close(sock);
	return -1;
    }
    
    if(waitReply(sock) != 2) {
	close(sock);
	return -1;
    }
    
    if(sendRequest(sock, "helo " MYHOSTNAME "\n") || waitReply(sock) != 2) {
	close(sock);
	return -1;
    } else
	return sock;
}

void disconnectMailServer(int sock) {
    sendRequest(sock, "quit\n");
    /* drop the reply :p */
    close(sock);
}

void doSendBody(int sock, FILE *fp, char *from, char *to, char *subject) {
    int n;
    char buf[2048];
    
    n = snprintf(buf, sizeof(buf), "From: %s\nTo: %s\nSubject: %s\n\n",
		 from, to, subject);
    write(sock, buf, n);
    
    while(fgets(buf, sizeof(buf), fp)) {
	if(buf[0] == '.' && buf[1] == '\n')
	    strcpy(buf, "..\n");
	write(sock, buf, strlen(buf));
    }
}

void doSendMail(int sock, FILE *fp, char *from, char *to, char *subject) {
    char buf[256];
    
    snprintf(buf, sizeof(buf), "mail from: %s\n", from);
    if(sendRequest(sock, buf) || waitReply(sock) != 2)
	return;
    
    snprintf(buf, sizeof(buf), "rcpt to: %s\n", to);
    if(sendRequest(sock, buf) || waitReply(sock) != 2)
	return;
    
    if(sendRequest(sock, "data\n") || waitReply(sock) != 3)
	return;
    
    doSendBody(sock, fp, from, to, subject);

    if(sendRequest(sock, "\n.\n") || waitReply(sock) != 2)
	return;
}

void sendMail() {
    int fd, sockPTT2, sockHinet;
    MailQueue mq;
    
    if(access(NEWINDEX, R_OK | W_OK)) {
	if(link(INDEX, NEWINDEX) || unlink(INDEX))
	    /* nothing to do */
	    return;
    }
	
    if( (sockPTT2 = connectMailServer("140.112.30.143")) < 0 ){
	fprintf(stderr, "connect server failed...\n");
	return;
    }
    sockHinet = connectMailServer("61.218.59.183");
    
    fd = open(NEWINDEX, O_RDONLY);
    flock(fd, LOCK_EX);
    while(read(fd, &mq, sizeof(mq)) > 0) {
	FILE *fp;
	char buf[256];
	
	snprintf(buf, sizeof(buf), "%s%s", mq.sender, FROM);
	if((fp = fopen(mq.filepath, "r"))) {
	    setproctitle("outmail: sending %s", mq.filepath);
	    if( strstr(mq.rcpt, ".edu.tw")    ||
		strstr(mq.rcpt, ".twbbs.org") ||
		strstr(mq.rcpt, "ptt.cc")    ||
		strstr(mq.rcpt, "ptt2.cc")      ){
		printf("relay server: ptt2, to %s\n", mq.rcpt);
		doSendMail(sockPTT2, fp, buf, mq.rcpt, mq.subject);
	    }
	    else{
		printf("relay server: ezmain, to %s\n", mq.rcpt);
		doSendMail( (sockHinet > 0) ? sockHinet : sockPTT2,
			   fp, buf, mq.rcpt, mq.subject);
	    }
	    fclose(fp);
	    unlink(mq.filepath);
	} else {
	    perror(mq.filepath);
	}
    }
    flock(fd, LOCK_UN);
    close(fd);
    unlink(NEWINDEX);
    
    if( sockHinet > 0 )
	disconnectMailServer(sockHinet);
    disconnectMailServer(sockPTT2);
}

void listQueue() {
    int fd;
    
    if((fd = open(INDEX, O_RDONLY)) >= 0) {
	int counter = 0;
	MailQueue mq;
	
	flock(fd, LOCK_EX);
	while(read(fd, &mq, sizeof(mq)) > 0) {
	    printf("%s:%s -> %s:%s\n", mq.filepath, mq.username, mq.rcpt,
		   mq.subject);
	    counter++;
	}
	flock(fd, LOCK_UN);
	close(fd);
	printf("\nTotal: %d mails in queue\n", counter);
    } else {
	perror(INDEX);
    }
}

void usage() {
    fprintf(stderr, "usage: outmail [-qh]\n");
}

void wakeup(int s) {
}

int main(int argc, char **argv, char **envp) {
    int ch;
 
    signal(SIGHUP, wakeup);
    initsetproctitle(argc, argv, envp);
    
    if(chdir(BBSHOME))
	return 1;
    while((ch = getopt(argc, argv, "qh")) != -1) {
	switch(ch) {
	case 'q':
	    listQueue();
	    return 0;
	default:
	    usage();
	    return 0;
	}
    }
    for(;;) {
	sendMail();
	setproctitle("outmail: sleeping");
	sleep(60 * 3); /* send mail every 3 minute */
    }
    return 0;
}
