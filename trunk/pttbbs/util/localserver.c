#include "bbs.h"
#include <err.h>

void usage(void);
int connectserver(char *, int);
void listennetwork(int, int);
void listenmsgqueue(int, int);

int main(int argc, char **argv)
{
    char    *host = NULL;
    int     port = 0, ch, sfd, msto, mtos;
    pid_t   pid;
    while( (ch = getopt(argc, argv, "s:p:")) != -1 )
	switch( ch ){
	case 's':
	    host = strdup(optarg);
	    break;
	case 'p':
	    port = atoi(optarg);
	    break;
	default:
	    usage();
	}

    if( host == NULL || port == 0 )
	usage();

    printf("connecting to server %s:%d\n", host, port);
    sfd = connectserver(host, port);
    puts("connected");

    puts("attaching message queue");
    if( (msto = msgget(OC_msto, 0600 | IPC_CREAT)) < 0 )
	err(1, "msgget OC_msto");
    if( (mtos = msgget(OC_mtos, 0600 | IPC_CREAT)) < 0 )
	err(1, "msgget OC_mtos");
    puts("attached");

    if( (pid = fork()) < 0 )
	err(1, "fork()");
    else if( pid == 0 )
	listennetwork(sfd, msto);
    listenmsgqueue(sfd, mtos);

    return 0;
}

void usage(void)
{
    fprintf(stderr, "usage:\tlocalserver -s host -p port\n");
    exit(0);
}

int connectserver(char *host, int port)
{
    struct  sockaddr_in     servaddr;
    int     fd;

    if( (fd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
	err(1, "socket");
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    inet_pton(AF_INET, host, &servaddr.sin_addr);
    servaddr.sin_port = htons(port);
    if( connect(fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0 )
	err(1, "connect");
    return fd;
}

void listennetwork(int netfd, int msgfd)
{
    int     len;
    char    buf[CACHE_BUFSIZE];
    printf("(%d)listening network %d => message queue %d\n",
	   getpid(), netfd, msgfd);
    while( Read(netfd, &len, 4) > 0 ){
	if( Read(netfd, buf, len) < 0 )
	    err(1, "(network)read()");
	if( msgsnd(msgfd, buf, len, 0) < 0 )
	    err(1, "(network)msgsnd()");
    }
    exit(0);
}

void listenmsgqueue(int netfd, int msgfd)
{
    char    buf[CACHE_BUFSIZE];
    OCbuf_t *ptr;
    int     len;
    printf("(%d)listening message queue %d => network %d\n",
	   getpid(), msgfd, netfd);
    while( msgrcv(msgfd, buf, CACHE_BUFSIZE, 0, 0) > 0 ){
	ptr = (OCbuf_t *)buf;
	len = ptr->length + OC_HEADERLEN;
	if( write(netfd, &len, sizeof(len)) < 0 ||
	    write(netfd, ptr, ptr->length + OC_HEADERLEN) < 0      )
	    err(1, "(msgqueue)write()");
    }
    err(1, "(msgqueue)msgrcv()");
    exit(0);
}

ssize_t Read(int fd, void *BUF, size_t nbytes)
{
    char    *buf = (char *)BUF;
    size_t  thisgot, totalgot = nbytes;
    while( nbytes > 0 ){
	if( (thisgot = read(fd, buf, nbytes)) <= 0 )
	    err(1, "read from socket: ");
	nbytes -= thisgot;
	buf += thisgot;
    }
    return totalgot;
}

