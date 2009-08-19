// $Id$
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bbs.h"
#include "daemons.h"

// standalone client to test fromd

int main(int argc, char *argv[])
{
    int fd, ret = 0;
    int op = 0;
    regmaildb_req req = {0};

    if (argc < 4) {
	fprintf(stderr, "Usage: %s operation userid email\n", argv[0]);
	return 0;
    }

    if ( (fd = toconnect(REGMAILD_ADDR)) < 0 ) {
	perror("toconnect");
	return 1;
    }

    if (strcmp(argv[1], "count") == 0)
    {
	op = REGMAILDB_REQ_COUNT;
	strlcpy(req.userid, argv[2], sizeof(req.userid));
	strlcpy(req.email,  argv[3], sizeof(req.email));
    }
    else if (strcmp(argv[1], "set") == 0)
    {
	op = REGMAILDB_REQ_SET;
	strlcpy(req.userid, argv[2], sizeof(req.userid));
	strlcpy(req.email,  argv[3], sizeof(req.email));
    }
    else if (strcmp(argv[1], "amb") == 0)
    {
	op = REGCHECK_REQ_AMBIGUOUS;
	strlcpy(req.userid, argv[2], sizeof(req.userid));
	strlcpy(req.email,  "ambiguous@check.nonexist", sizeof(req.email));
    }
    else
	return 0;

    req.cb = sizeof(req);
    req.operation = op;

    if (towrite(fd, &req, sizeof(req)) != sizeof(req)) {
	perror("towrite");
	return 1;
    }

    if (toread(fd, &ret, sizeof(ret)) != sizeof(ret)) {
	perror("toread");
	return 1;
    }

    printf("result: %d\n", ret);
    return 0;
}


