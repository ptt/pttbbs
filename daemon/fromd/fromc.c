// $Id$
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bbs.h"

// standalone client to test fromd

int main(int argc, char *argv[])
{
    int fd;
    char buf[64];

    memset(buf, 0, sizeof(buf));

    if (argc < 4) {
	fprintf(stderr, "Usage: %s ip:port lookup_ip\n", argv[0]);
	return 0;
    }

    if ( (fd = toconnect(argv[1])) < 0 ) {
	perror("toconnect");
	return 1;
    }

    write(fd, argv[2], strlen(argv[2]));
    read(fd, buf, sizeof(buf));

    printf("%s\n", buf);

    return 0;
}


