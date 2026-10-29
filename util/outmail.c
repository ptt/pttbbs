#include <stdbool.h>

#include "bbs.h"

#define SPOOL BBSHOME "/out"
#define INDEX SPOOL "/.DIR"
#define NEWINDEX_PATTERN SPOOL "/.DIR.sending.%d"
#define FROM ".bbs@" MYHOSTNAME
#define SMTPPORT 25
#define MAX_RETRY 3 /* give up a mail that has to be sent again this often */
char    *smtpname;
int     smtpport;
char	disclaimer[1024];
bool	one_shot;

/* Returns the reply code (e.g. 250), 0 if the reply doesn't start with
 * three digits and -1 if the connection is lost */
int waitReply(int sock) {
    char buf[256];
    ssize_t n;

    if ((n = read(sock, buf, sizeof(buf))) <= 0)
	return -1;
    if (n < 3 || !isdigit((unsigned char)buf[0]) ||
	    !isdigit((unsigned char)buf[1]) || !isdigit((unsigned char)buf[2]))
	return 0;
    return (buf[0] - '0') * 100 + (buf[1] - '0') * 10 + (buf[2] - '0');
}

int writeAll(int sock, const char *p, size_t n) {
    ssize_t w;

    while (n > 0) {
	if ((w = write(sock, p, n)) <= 0) {
	    if (w < 0 && errno == EINTR)
		continue;
	    return -1;
	}
	p += w;
	n -= w;
    }
    return 0;
}

int sendRequest(int sock, char *request) {
    return writeAll(sock, request, strlen(request));
}

/* Sends one SMTP command. Returns 1 if the reply is of the wanted class
 * (the first digit), -1 if the connection is lost or the relay server is
 * closing it (421), and 0 for any other reply. */
int smtpCommand(int sock, char *request, int want) {
    int r;

    if (sendRequest(sock, request) || (r = waitReply(sock)) < 0 || r == 421)
	return -1;
    return r / 100 == want;
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
    
    if(waitReply(sock) / 100 != 2) {
	close(sock);
	return -1;
    }
    
    if(sendRequest(sock, "helo " MYHOSTNAME "\r\n") || waitReply(sock) / 100 != 2) {
	close(sock);
	return -1;
    } else
	return sock;
}

void disconnectMailServer(int sock) {
    sendRequest(sock, "quit\r\n");
    /* drop the reply :p */
    close(sock);
}

int need_qp(const char *_s)
{
    const unsigned char *s = (const unsigned char*)_s;

    while (*s && *s++ < 0x80);
    if (*s)
	return 1;
    return 0;
}

int doSendBody(int sock, FILE *fp, char *from, char *to, char *subject) {
    size_t n;
    int c;
    char buf[2048];
    char subject_qp[STRLEN*3+100];
    char subject_safe[STRLEN];
    bool bol = true, cr = false;
    static  int     starttime = -1, msgid = 0;
    if( starttime == -1 ){
	starttime = (int)time(NULL);
	msgid = arc4random();
    }
    /* Keep the subject on its own header line */
    STRLCPY(subject_safe, subject);
    mbs_sanitize(subject_safe, sizeof(subject_safe));
    subject = subject_safe;
    n = SNPRINTF(buf, "From: %s <%s>\r\n"
		 "To: %s\r\n"
		 "Subject: %s\r\n"
		 "X-Sender: outmail of pttbbs " __DATE__ "\r\n"
		 "Mime-Version: 1.0\r\n"
		 "Content-Type: text/plain; charset=\"big5\"\r\n"
		 "Content-Transfer-Encoding: 8bit\r\n"
		 "Message-Id: <%d.%x.outmail@" MYHOSTNAME ">\r\n"
		 "X-Disclaimer: %s\r\n\r\n",
		 from, from, to,
		 need_qp(subject) ?  
		    qp_encode(subject_qp, sizeof(subject_qp), subject, "big5") : subject,
		 starttime,
		 (msgid += arc4random_uniform(0xff) + 1),
		 disclaimer);
    assert(n < sizeof(buf));
    if (n > sizeof(buf))
	n = sizeof(buf);
    if (writeAll(sock, buf, n))
	return -1;

    /* End every line (LF, CR or CRLF) with CRLF and dot-stuff every line
     * starting with '.' (RFC 5321 4.5.2), so the body can't end DATA */
    n = 0;
    while ((c = getc(fp)) != EOF) {
	if (n + 2 > sizeof(buf)) {
	    if (writeAll(sock, buf, n))
		return -1;
	    n = 0;
	}
	if (cr && c == '\n') {
	    cr = false;
	    continue;
	}
	if (c == '\0')
	    continue;
	cr = (c == '\r');
	if (c == '\r' || c == '\n') {
	    buf[n++] = '\r';
	    buf[n++] = '\n';
	    bol = true;
	    continue;
	}
	if (bol && c == '.')
	    buf[n++] = '.';
	buf[n++] = c;
	bol = false;
    }
    return writeAll(sock, buf, n);
}

/* Returns 1 if the mail is sent, 0 if the relay server refuses it and -1
 * if it should be sent again later (see smtpCommand). */
int doSendMail(int sock, FILE *fp, char *from, char *to, char *subject) {
    char buf[256];
    int r;

    if ((r = smtpCommand(sock, "rset\r\n", 2)) <= 0)
	return r;

    SNPRINTF(buf, "mail from: %s\r\n", from);
    if ((r = smtpCommand(sock, buf, 2)) <= 0)
	return r;

    SNPRINTF(buf, "rcpt to: %s\r\n", to);
    if ((r = smtpCommand(sock, buf, 2)) <= 0)
	return r;

    if ((r = smtpCommand(sock, "data\r\n", 3)) <= 0)
	return r;

    if (doSendBody(sock, fp, from, to, subject))
	return -1;

    return smtpCommand(sock, "\r\n.\r\n", 2);
}

/* Records in the spool aren't trusted to be NUL-terminated */
void terminateMailQueue(MailQueue *mq) {
    mq->filepath[sizeof(mq->filepath) - 1] = '\0';
    mq->subject[sizeof(mq->subject) - 1] = '\0';
    mq->sender[sizeof(mq->sender) - 1] = '\0';
    mq->username[sizeof(mq->username) - 1] = '\0';
    mq->rcpt[sizeof(mq->rcpt) - 1] = '\0';
}

void sendMail() {
    int fd, smtpsock, counter = 0;
    bool retry = false;
    long int total_count;
    struct stat st;
    MailQueue mq;
    off_t pos;
    /* Where to go on in the kept spool_path: the record of the mail to send
     * again, and how many times it was tried. The mails before it are done;
     * their files may be gone, and bsmtp may reuse the names. */
    static off_t resume_at;
    static int retry_count;

    AUTO_STR(spool_path, NEWINDEX_PATTERN, getpid());

    if(access(spool_path, R_OK | W_OK)) {
	if(link(INDEX, spool_path) || unlink(INDEX))
	    /* nothing to do */
	    return;
	/* a new spool_path, even if the kept one was removed */
	resume_at = 0;
	retry_count = 0;
    }

    smtpsock = connectMailServer(smtpname, smtpport);

    if( smtpsock < 0) {
	fprintf(stderr, "connecting to relay server failure...\n");
	return;
    }
    
    fd = open(spool_path, O_RDONLY);
    flock(fd, LOCK_EX);
    fstat(fd, &st);
    total_count = st.st_size / sizeof(mq);
    lseek(fd, resume_at, SEEK_SET);
    counter = resume_at / sizeof(mq);

    while ((pos = lseek(fd, 0, SEEK_CUR)) >= 0 &&
	    read(fd, &mq, sizeof(mq)) > 0) {
	FILE *fp;
	char buf[256];
	
	counter++;
	terminateMailQueue(&mq);
	SNPRINTF(buf, "%s%s", mq.sender, FROM);
	/* CR/LF would end the SMTP command and start a new one */
	if (strpbrk(buf, "\r\n") || strpbrk(mq.rcpt, "\r\n")) {
	    fprintf(stderr, "%s: CR/LF in sender or recipient, dropped\n",
		    mq.filepath);
	    unlink(mq.filepath);
	    continue;
	}
	if((fp = fopen(mq.filepath, "r"))) {
	    setproctitle("outmail: sending %d/%ld %s", counter, total_count, mq.filepath);
	    printf("mailto: %s, %d of %ld, relay server: %s:%d\n", mq.rcpt, counter, total_count,
		   smtpname, smtpport);
	    retry = doSendMail(smtpsock, fp, buf, mq.rcpt, mq.subject) < 0;
	    fclose(fp);
	    if (retry) {
		if (pos != resume_at) {
		    resume_at = pos;
		    retry_count = 0;
		}
		if (++retry_count >= MAX_RETRY) {
		    fprintf(stderr, "%s: not sent after %d tries, dropped\n",
			    mq.filepath, retry_count);
		    unlink(mq.filepath);
		    resume_at = pos + sizeof(mq);
		    retry_count = 0;
		    /* done if it was the last one */
		    retry = resume_at < st.st_size;
		}
		break;
	    }
	    unlink(mq.filepath);
	} else {
	    perror(mq.filepath);
	}
    }
    flock(fd, LOCK_UN);
    close(fd);

    if (retry) {
	/* Keep this mail and the rest: the next round goes on from
	 * resume_at */
	fprintf(stderr, "relay server connection lost, retry later\n");
    } else {
	unlink(spool_path);
	resume_at = 0;
	retry_count = 0;
    }
    disconnectMailServer(smtpsock);
}

void listQueue() {
    int fd;
    
    if((fd = open(INDEX, O_RDONLY)) >= 0) {
	int counter = 0;
	MailQueue mq;
	
	flock(fd, LOCK_EX);
	while(read(fd, &mq, sizeof(mq)) > 0) {
	    terminateMailQueue(&mq);
	    printf("%s:%s -> %s:%s\n", mq.filepath, mq.sender, mq.rcpt,
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

void wakeup(int s GCC_UNUSED) {
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
    /* A lost relay connection is handled where a write fails */
    Signal(SIGPIPE, SIG_IGN);
    initsetproctitle(argc, argv, envp);
    
    if(chdir(BBSHOME))
	return 1;
    while((ch = getopt(argc, argv, "qhs:o:")) != -1) {
	switch(ch) {
	case 's':
	    parseserver(optarg, &smtpname, &smtpport);
	    break;
	case 'o':
	    one_shot = true;
	    break;
	case 'q':
	    listQueue();
	    return 0;
	default:
	    printf("usage:\toutmail [-qoh] -s host[:port]\n"
		   "\t-q\tlistqueue\n"
		   "\t-o\texit when current spool is fully processed (One-shot)\n"
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
    do {
	sendMail();
	setproctitle("outmail: sleeping");
	sleep(60); /* send mail every minute */
    } while (!one_shot);

    return 0;
}
