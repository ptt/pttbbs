// $Id$
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bbs.h"
#include "daemons.h"

// forward declarations

bool verifydb_client_getuser(const char *userid);
bool verifydb_client_getverify(int32_t vmethod, const char *vkey);
bool verifydb_client_set(const char *userid, int64_t generation,
	int32_t vmethod, const char *vkey, int64_t timestamp);

// standalone client to test fromd

static int
usage(const char *prog)
{
    fprintf(stderr, "Usage: %s table operation [args ...]\n\n", prog);
    fprintf(stderr, "\t%s regmaildb <count|set|amb> userid email\n", prog);
    fprintf(stderr, "\t%s verifydb getuser userid\n", prog);
    fprintf(stderr, "\t%s verifydb getverify vmethod vkey\n", prog);
    fprintf(stderr, "\t%s verifydb set userid generation vmethod vkey\n", prog);
    return 1;
}

static int verifydb_main(const char *prog, int argc, char **argv)
{
    bool ok = false;

    if (argc < 1)
	return usage(prog);
    const char *operation = argv[0];

    if (strcmp(operation, "getuser") == 0 && argc == 2)
    {
	const char *userid = argv[1];
	ok = verifydb_client_getuser(userid);
    }
    else if (strcmp(operation, "getverify") == 0 && argc == 3)
    {
	const int32_t vmethod = atoi(argv[1]);
	const char *vkey = argv[2];
	ok = verifydb_client_getverify(vmethod, vkey);
    }
    else if (strcmp(operation, "set") == 0 && argc == 6)
    {
	const char *userid = argv[1];
	const int64_t generation = atoi(argv[2]);
	const int32_t vmethod = atoi(argv[3]);
	const char *vkey = argv[4];
	const int64_t timestamp = atoi(argv[5]);
	ok = verifydb_client_set(userid, generation, vmethod, vkey, timestamp);
    }
    else
	return usage(prog);

    return ok ? 0 : 1;
}

static int regmaildb_main(const char *prog, int argc, char **argv)
{
    if (argc != 3)
	return usage(prog);
    const char *operation = argv[0];
    const char *userid = argv[1];
    const char *email = argv[2];

    regmaildb_req req = {};
    req.cb = sizeof(req);

    if (strcmp(operation, "count") == 0)
    {
	req.operation = REGMAILDB_REQ_COUNT;
	strlcpy(req.userid, userid, sizeof(req.userid));
	strlcpy(req.email,  email,  sizeof(req.email));
    }
    else if (strcmp(operation, "set") == 0)
    {
	req.operation = REGMAILDB_REQ_SET;
	strlcpy(req.userid, userid, sizeof(req.userid));
	strlcpy(req.email,  email,  sizeof(req.email));
    }
    else if (strcmp(operation, "amb") == 0)
    {
	req.operation = REGCHECK_REQ_AMBIGUOUS;
	strlcpy(req.userid, userid, sizeof(req.userid));
	strlcpy(req.email,  "ambiguous@check.nonexist", sizeof(req.email));
    }
    else
	return usage(prog);

    int fd = toconnect(REGMAILD_ADDR);
    if (fd < 0) {
	perror("toconnect");
	return 1;
    }
    if (towrite(fd, &req, req.cb) != req.cb) {
	perror("towrite");
	return 1;
    }
    int ret;
    if (toread(fd, &ret, sizeof(ret)) != sizeof(ret)) {
	perror("toread");
	return 1;
    }
    printf("result: %d\n", ret);
    return 0;
}

int main(int argc, char **argv)
{
    const char *prog = argv[0];

    if (argc < 2)
	return usage(prog);

    const char *table = argv[1];

    if (strcmp(table, "regmaildb") == 0)
	return regmaildb_main(prog, argc - 2, argv + 2);
    else if (strcmp(table, "verifydb") == 0)
	return verifydb_main(prog, argc - 2, argv + 2);

    return usage(prog);
}


