// $Id$
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bbs.h"
#include "logind.h"

// standalone client to test logind

int main(int argc, char *argv[])
{
    int fd;
    char buf[64];

    memset(buf, 0, sizeof(buf));

    if (argc < 2) {
	fprintf(stderr, "Usage: %s tunnel_path\n", argv[0]);
	return 0;
    }

    if ( (fd = toconnect(argv[1])) < 0 ) {
	perror("toconnect");
	return 1;
    }

    puts("start waiting!\n");
    while (1)
    {
	int xfd = 0, ok = 1, i;
	const char *encoding = "";
	login_data dat = {0};

	if ((xfd = recv_remote_fd(fd)) < 0) 
	{
	    fprintf(stderr, "recv_remote_fd error. abort.\r\n");
	    break;
	}
	puts("got recv_remote_fd");
	if (read(fd, &dat, sizeof(dat)) <= 0)
	{
	    fprintf(stderr, "recv error. abort.\r\n");
	    break;
	}
#ifdef CONVERT
	switch (dat.encoding)
	{
	    case CONV_GB:
		encoding = "[GB] ";
		break;
	    case CONV_UTF8:
		encoding = "[UTF-8] ";
		break;
	}
#endif
	fprintf(stderr, "got login data: userid=%s, (%dx%d) %sfrom: %s\r\n", 
		dat.userid, dat.t_cols, dat.t_lines, 
		encoding, dat.hostip);
	write(fd, &ok, sizeof(ok));

	dup2(xfd, 0);
	dup2(xfd, 1);

	// write something to user!
	printf("\r\nwelcome, %s from %s! greetings from loginc!\r\n", dat.userid, dat.hostip);
	printf("please give me 3 key hits to test i/o!\r\n");

	for (i = 0; i < 3; i++)
	{
	    char c;
	    read(0, &c, sizeof(c));
	    printf("you hit %02X\r\n", c);
	}
	printf("\r\ntest complete. connection closed.\r\n");
	close(xfd);
    }
    return 0;
}


