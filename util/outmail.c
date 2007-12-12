/* $Id$ */
#include "bbs.h"

#define SPOOL BBSHOME "/out"
#define INDEX SPOOL "/.DIR"
#define NEWINDEX SPOOL "/.DIR.sending"
#define FROM ".bbs@" MYHOSTNAME
#define SMTPPORT 25
char    *smtpname;
int     smtpport;
char	disclaimer[1024];

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

int connectMailServer(char *servername, int serverport)
{
    int sock;
    struct sockaddr_in addr;
    
    if((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
	perror("socket");
	return -1;
    }
    
    memset(&addr, 0, sizeof(addr));
#ifdef __FreeBSD__
    addr.sin_len = sizeof(addr);
#endif
    addr.sin_family = AF_INET;
    addr.sin_port = htons(serverport);
    addr.sin_addr.s_addr = inet_addr(servername);
    
    if(connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	printf("servername: %s\n", servername);
	perror(servername);
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
    char subject_qp[STRLEN*3+100];
    static  int     starttime = -1, msgid = 0;
    if( starttime == -1 ){
	srandom(starttime = (int)time(NULL));
	msgid = random();
    }
    n = snprintf(buf, sizeof(buf),
		 "From: %s <%s>\r\n"
		 "To: %s\r\n"
		 "Subject: %s\r\n"
		 "X-Sender: outmail of pttbbs\r\n"
		 "Mime-Version: 1.0\r\n"
		 "Content-Type: text/plain; charset=\"big5\"\r\n"
		 "Content-Transfer-Encoding: 8bit\r\n"
		 "Message-Id: <%d.%x.outmail@" MYHOSTNAME ">\r\n"
		 "X-Disclaimer: %s\r\n\r\n",
		 from, from, to,
		 qp_encode(subject_qp, sizeof(subject_qp), subject, "big5"),
		 starttime,
		 (msgid += (int)(random() >> 24)),
		 disclaimer);
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
    int fd, smtpsock;
    MailQueue mq;
    
    if(access(NEWINDEX, R_OK | W_OK)) {
	if(link(INDEX, NEWINDEX) || unlink(INDEX))
	    /* nothing to do */
	    return;
    }

    smtpsock = connectMailServer(smtpname, smtpport);

    if( smtpsock < 0) {
	fprintf(stderr, "connecting to relay server failure...\n");
	return;
    }
    
    fd = open(NEWINDEX, O_RDONLY);
    flock(fd, LOCK_EX);
    while(read(fd, &mq, sizeof(mq)) > 0) {
	FILE *fp;
	char buf[256];
	
	snprintf(buf, sizeof(buf), "%s%s", mq.sender, FROM);
	if((fp = fopen(mq.filepath, "r"))) {
	    setproctitle("outmail: sending %s", mq.filepath);
	    printf("mailto: %s, relay server: %s:%d\n", mq.rcpt, smtpname, smtpport);
	    doSendMail(smtpsock, fp, buf, mq.rcpt, mq.subject);
	    fclose(fp);
	    unlink(mq.filepath);
	} else {
	    perror(mq.filepath);
	}
    }
    flock(fd, LOCK_UN);
    close(fd);
    unlink(NEWINDEX);
    
    disconnectMailServer(smtpsock);
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

void wakeup(int s) {
}

void parseserver(char *sx, char **name, int *port)
{
    char    *save = strdup(sx);
    char    *ptr;
    if( (ptr = strstr(save, ":")) == NULL ){
	*name = strdup(save);
	*port = 25;
    }
    else{
	*ptr = 0;
	*name = strdup(save);
	*port = atoi(ptr + 1);
    }
    free(save);
}

int main(int argc, char **argv, char **envp) {
    int ch;
 
    Signal(SIGHUP, wakeup);
    initsetproctitle(argc, argv, envp);
    
    if(chdir(BBSHOME))
	return 1;
    while((ch = getopt(argc, argv, "qhs:o:")) != -1) {
	switch(ch) {
	case 's':
	    parseserver(optarg, &smtpname, &smtpport);
	    break;
	case 'q':
	    listQueue();
	    return 0;
	default:
	    printf("usage:\toutmail [-qh] -s host[:port]\n"
		   "\t-q\tlistqueue\n"
		   "\t-h\thelp\n"
		   "\t-s\tset default smtp server to host[:port]\n");
	    return 0;
	}
    }

    if( smtpname == NULL ){
#ifdef RELAY_SERVER_IP
	smtpname = RELAY_SERVER_IP;
#else
	smtpname = "127.0.0.1";
#endif
	smtpport = 25;
    }

    qp_encode(disclaimer, sizeof(disclaimer), "[" BBSNAME "]對本信內容恕不負責", "big5");
    for(;;) {
	sendMail();
	setproctitle("outmail: sleeping");
	sleep(60); /* send mail every minute */
    }
    return 0;
}
