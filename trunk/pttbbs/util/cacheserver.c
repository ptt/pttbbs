#include "bbs.h"
#include <err.h>
#include <avltree.h>

int toaccept(int port);
void usage(void);
ssize_t Read(int fd, void *buf, size_t nbytes);
ssize_t Write(int fd, void *BUF, size_t nbytes)
{
    char    *buf = (char *)BUF;
    size_t  thisgot, totalgot = nbytes;
    while( nbytes > 0 ){
        if( (thisgot = write(fd, buf, nbytes)) <= 0 )
            err(1, "read from socket: ");
        nbytes -= thisgot;
        buf += thisgot;
    }
    return totalgot;
}


int main(int argc, char **argv)
{
    int     sfd, len;
    int     port = -1;
    char    ch;
    OCbuf_t OCbuf;

    AVL_IX_DESC        ix;
    AVL_IX_REC         pe;
    OCstore_t          *store;
    //avl_create_index(&ix, AVL_COUNT_DUPS, OC_KEYLEN);
    avl_create_index(&ix, AVL_COUNT_DUPS, 5);

    while( (ch = getopt(argc, argv, "p:")) != -1 )
	switch( ch ){
	case 'p':
	    port = atoi(optarg);
	    break;
	}
    if( port == -1 )
	usage();

    while( 1 ){
	sfd = toaccept(port);
	Read(sfd, &len, sizeof(len));
	printf("reading %d bytes\n", len);
	Read(sfd, &OCbuf, len);
	printf("read! pid: %d\n", OCbuf.key.pid);

	memset(&pe, 0, sizeof(pe));
	if( OCbuf.key.pid <= 1 ){
	    /* garbage collection */
	}
	else if( OCbuf.key.pid < OC_pidadd ){
	    /* store */
	    puts("swapin");

	    len = OC_HEADERLEN + OCbuf.length;
	    store = (OCstore_t *)malloc(sizeof(time_t) + len);
	    store->mtime = time(NULL);
	    memcpy(&(store->data), &OCbuf, len);

	    pe.recptr = (void *)store;
	    memcpy(pe.key, &OCbuf.key, OC_KEYLEN);
	    if( avl_add_key(&pe, &ix) != AVL_IX_OK )
		puts("add key error");
	}
	else {
	    OCbuf.key.pid -= OC_pidadd;
	    memcpy(pe.key, &OCbuf.key, OC_KEYLEN);
	    if( avl_find_key(&pe, &ix) == AVL_IX_OK ){
		store = (OCstore_t *)pe.recptr;

		len = store->data.length + OC_HEADERLEN;
		Write(sfd, &len, sizeof(len));
		Write(sfd, &(store->data), len);
		free(store);
		if( avl_delete_key(&pe, &ix) != AVL_IX_OK )
		    puts("delete key error");
	    }
	    else
		puts("error");
	}
	close(sfd);
    }
    return 0;
}

int toaccept(int port)
{
    static  int     sockfd = 0, len;
    int     cfd, val;
    struct  sockaddr_in     servaddr, clientaddr;
    
    if( sockfd == 0 ){
	if( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
	    err(1, NULL);
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&val, sizeof(val));
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (char *)&val, sizeof(val));
	
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(port);
	if( bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0 )
	    err(1, NULL);
	if( listen(sockfd, 5) < 0 )
	    err(1, NULL);

	len = sizeof(struct sockaddr_in);
    }

    while( (cfd = accept(sockfd, (struct sockaddr *)&clientaddr, &len)) <= 0 )
	;

    return cfd;
}

void usage(void)
{
    fprintf(stderr,
	    "usage:\tcacheserver [options]\n"
	    "-p port\tlistenport\n");
    exit(0);
}

ssize_t Read(int fd, void *BUF, size_t nbytes)
{
    char    *buf = (char *)BUF;
    ssize_t  thisgot, totalgot = nbytes;
    while( nbytes > 0 ){
	if( (thisgot = read(fd, buf, nbytes)) <= 0 )
	    err(1, "read from socket: ");
	nbytes -= thisgot;
	buf += thisgot;
    }
    return totalgot;
}
