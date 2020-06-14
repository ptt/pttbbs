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

static bool
read_verifydb_count_reply(int fd)
{
    verifydb_count_rep rep;
    if (toread(fd, &rep, sizeof(rep)) != sizeof(rep)) {
	perror("toread");
	return false;
    }
    if (rep.cb != sizeof(rep)) {
	fprintf(stderr, "unable to read reply: cb != sizeof(rep): %lu != %lu\n",
		rep.cb, sizeof(rep));
	return false;
    }
    printf("status: %d\n", rep.status);
    printf("count_self: %d\n", rep.count_self);
    printf("count_other: %d\n", rep.count_other);
    return true;
}

static bool
read_verifydb_get_reply(int fd)
{
    verifydb_get_rep rep;
    if (toread(fd, &rep, sizeof(rep)) != sizeof(rep)) {
	perror("toread");
	return false;
    }
    if (rep.cb != sizeof(rep)) {
	fprintf(stderr, "unable to read reply: cb != sizeof(rep): %lu != %lu\n",
		rep.cb, sizeof(rep));
	return false;
    }
    printf("status: %d\n", rep.status);
    printf("num_entries: %lu\n", rep.num_entries);
    printf("entry_size: %lu\n", rep.entry_size);

    verifydb_entry ent;
    if (rep.entry_size != sizeof(ent)) {
	fprintf(stderr, "unable to read entry: entry_size != sizeof(ent): %lu != %lu\n",
		rep.entry_size, sizeof(ent));
	return false;
    }
    for (size_t i = 0; i < rep.num_entries; i++) {
	if (toread(fd, &ent, sizeof(ent)) != sizeof(ent)) {
	    perror("toread");
	    return false;
	}
	printf("record [%lu]\n", i);
	printf("  timestamp: %ld\n", ent.timestamp);
	printf("  vmethod: %d\n", ent.vmethod);
	printf("  vkey: %s\n", ent.vkey);
    }
    return true;
}

static int
usage(const char *prog)
{
    fprintf(stderr, "Usage: %s table operation [args ...]\n\n", prog);
    fprintf(stderr, "\t%s regmaildb <count|set|amb> userid email\n", prog);
    fprintf(stderr, "\t%s verifydb count userid generation vmethod vkey\n", prog);
    fprintf(stderr, "\t%s verifydb set userid generation vmethod vkey\n", prog);
    fprintf(stderr, "\t%s verifydb get userid generation [vmethod]\n", prog);
    return 1;
}

int main(int argc, char **argv)
{
    const char *prog = argv[0];
    int fd;
    regmaildb_req_storage storage = {};
    bool (*read_reply)(int fd) = read_int_reply;

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
    else if (strcmp(table, "verifydb") == 0)
    {
	if (argc < 5)
	    return usage(prog);
	const char *userid = argv[3];
	const int64_t generation = atoll(argv[4]);
	const int32_t vmethod = argc >= 6 ? atoi(argv[5]) : VMETHOD_UNSET;
	const char *vkey = argc >= 7 ? argv[6] : NULL;
	verifydb_req *req = &storage.verifydb;
	req->cb = sizeof(*req);

	// Common fields.
	strlcpy(req->userid, userid, sizeof(req->userid));
	req->generation = generation;
	req->vmethod = vmethod;
	if (vkey)
	    strlcpy(req->vkey, vkey, sizeof(req->vkey));

	if (strcmp(operation, "count") == 0)
	{
	    if (!vkey)
		return usage(prog);
	    req->operation = VERIFYDB_REQ_COUNT;
	    read_reply = read_verifydb_count_reply;
	}
	else if (strcmp(operation, "set") == 0)
	{
	    if (!vkey)
		return usage(prog);
	    req->operation = VERIFYDB_REQ_SET;
	}
	else if (strcmp(operation, "get") == 0)
	{
	    req->operation = VERIFYDB_REQ_GET;
	    read_reply = read_verifydb_get_reply;
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

    if (!read_reply(fd)) {
	fprintf(stderr, "failed to read reply\n");
	return 1;
    }
    return 0;
}


