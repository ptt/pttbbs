#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>

#include "cmsys.h"

unsigned int
ipstr2int(const char *ip)
{
    unsigned int i, val = 0;
    char buf[32];
    char *nil, *p;

    strlcpy(buf, ip, sizeof(buf));
    p = buf;
    for (i = 0; i < 4; i++) {
	nil = strchr(p, '.');
	if (nil != NULL)
	    *nil = 0;
	val *= 256;
	val += atoi(p);
	if (nil != NULL)
	    p = nil + 1;
    }
    return val;
}

int tobind(const char * addr)
{
    int     sockfd, val = 1;

    assert(addr != NULL);

    if (!isdigit(addr[0] && addr[0] != ':')) {
	struct sockaddr_un servaddr;

	if ( (sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0 ) {
	    perror("socket()");
	    exit(1);
	}

	servaddr.sun_family = AF_UNIX;
	strlcpy(servaddr.sun_path, addr, sizeof(servaddr.sun_path));

	// remove the file first if it exists.
	unlink(servaddr.sun_path);

	if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
	    perror("bind()");
	    exit(1);
	}
    }
    else {
	char buf[64], *port;
	struct sockaddr_in servaddr;

	if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
	    perror("socket()");
	    exit(1);
	}

	strlcpy(buf, addr, sizeof(buf));
	if ( (port = strchr(buf, ':')) != NULL)
	    *port++ = '\0';

	assert(port && atoi(port) != 0);

	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
		   (char *)&val, sizeof(val));
	servaddr.sin_family = AF_INET;
	if (buf[0] == '\0')
	    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (inet_aton(buf, &servaddr.sin_addr) == 0) {
	    perror("inet_aton()");
	    exit(1);
	}
	servaddr.sin_port = htons(atoi(port));

	if( bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0 ) {
	    perror("bind()");
	    exit(1);
	}
    }

    if (listen(sockfd, 10) < 0) {
	perror("listen()");
	exit(1);
    }

    return sockfd;
}

int toconnect(const char *addr)
{
    int sock;
    
    assert(addr != NULL);

    if (!isdigit(addr[0])) {
	struct sockaddr_un serv_name;

	if ( (sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0 ) {
	    perror("socket");
	    return -1;
	}

	serv_name.sun_family = AF_UNIX;
	strlcpy(serv_name.sun_path, addr, sizeof(serv_name.sun_path));

	if (connect(sock, (struct sockaddr *)&serv_name, sizeof(serv_name)) < 0) {
	    close(sock);
	    return -1;
	}
    }
    else {
	char buf[64], *port;
	struct sockaddr_in serv_name;

	if( (sock = socket(AF_INET, SOCK_STREAM, 0)) < 0 ){
	    perror("socket");
	    return -1;
	}

	strlcpy(buf, addr, sizeof(buf));
	if ( (port = strchr(buf, ':')) != NULL)
	    *port++ = '\0';

	assert(port && atoi(port) != 0);

	serv_name.sin_family = AF_INET;
	serv_name.sin_addr.s_addr = inet_addr(buf);
	serv_name.sin_port = htons(atoi(port));
	if( connect(sock, (struct sockaddr*)&serv_name, sizeof(serv_name)) < 0 ){
	    close(sock);
	    return -1;
	}
    }

    return sock;
}

/**
 * same as read(2), but read until exactly size len 
 */
int toread(int fd, void *buf, int len)
{
    int     l;
    for( l = 0 ; len > 0 ; )
	if( (l = read(fd, buf, len)) <= 0 )
	    return -1;
	else{
	    buf += l;
	    len -= l;
	}
    return l;
}

/**
 * same as write(2), but write until exactly size len 
 */
int towrite(int fd, const void *buf, int len)
{
    int     l;
    for( l = 0 ; len > 0 ; )
	if( (l = write(fd, buf, len)) <= 0 )
	    return -1;
	else{
	    buf += l;
	    len -= l;
	}
    return l;
}
