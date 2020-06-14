// $Id$
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bbs.h"
#include "daemons.h"

// standalone client to test fromd

static bool
read_int_reply(int fd)
{
    int ret;
    if (toread(fd, &ret, sizeof(ret)) != sizeof(ret)) {
	perror("toread");
	return false;
    }
    printf("result: %d\n", ret);
    return true;
}

static int
usage(const char *prog)
{
    fprintf(stderr, "Usage: %s table operation [args ...]\n\n", prog);
    fprintf(stderr, "\t%s regmaildb <count|set|amb> userid email\n", prog);
    return 1;
}

int main(int argc, char **argv)
{
    const char *prog = argv[0];
    int fd;
    regmaildb_req_storage storage = {};

    if (argc < 3)
	return usage(prog);
    const char *table = argv[1];
    const char *operation = argv[2];

    if (strcmp(table, "regmaildb") == 0)
    {
	if (argc < 5)
	    return usage(prog);
	const char *userid = argv[3];
	const char *email = argv[4];
	regmaildb_req *req = &storage.regmaildb;
	req->cb = sizeof(*req);

	if (strcmp(operation, "count") == 0)
	{
	    req->operation = REGMAILDB_REQ_COUNT;
	    strlcpy(req->userid, userid, sizeof(req->userid));
	    strlcpy(req->email,  email,  sizeof(req->email));
	}
	else if (strcmp(operation, "set") == 0)
	{
	    req->operation = REGMAILDB_REQ_SET;
	    strlcpy(req->userid, userid, sizeof(req->userid));
	    strlcpy(req->email,  email,  sizeof(req->email));
	}
	else if (strcmp(operation, "amb") == 0)
	{
	    req->operation = REGCHECK_REQ_AMBIGUOUS;
	    strlcpy(req->userid, userid, sizeof(req->userid));
	    strlcpy(req->email,  "ambiguous@check.nonexist", sizeof(req->email));
	}
	else
	    return usage(prog);
    }
    else
    {
	return usage(prog);
    }

    if ( (fd = toconnect(REGMAILD_ADDR)) < 0 ) {
	perror("toconnect");
	return 1;
    }

    if (towrite(fd, &storage, storage.header.cb) != storage.header.cb) {
	perror("towrite");
	return 1;
    }

    if (!read_int_reply(fd)) {
	fprintf(stderr, "failed to read reply\n");
	return 1;
    }
    return 0;
}


