// $Id$
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bbs.h"

// standalone client to test fromd

int main(int argc, char *argv[])
{
    int port, fd;
    char buf[64];

    memset(buf, 0, sizeof(buf));

    if (argc < 4) {
	fprintf(stderr, "Usage: %s ip port lookup_ip\n", argv[0]);
	return 0;
    }

    if ( (port = atoi(argv[2])) == 0 ) {
	fprintf(stderr, "Port given is not valid\n");
	return 1;
    }

    if ( (fd = toconnect(argv[1], port)) < 0 ) {
	perror("toconnect");
	return 1;
    }

    write(fd, argv[3], strlen(argv[3]));
    read(fd, buf, sizeof(buf));

    printf("%s\n", buf);

    return 0;
}


