
#include "bbs.h"
#include "daemons.h"

static
int regmail_transact(const void *in, size_t inlen, void *out, size_t outlen)
{
    int fd = toconnect(REGMAILD_ADDR);
    if (fd < 0)
        return -1;

    if (towrite(fd, in, inlen) != inlen ||
        toread(fd, out, outlen) != outlen)
    {
        close(fd);
        return -1;
    }

    return 0;
}

#ifdef USE_EMAILDB

int emaildb_check_email(const char * userid, const char * email)
{
    int count = -1;
    regmaildb_req req = {0};

    // regmaild rejects empty userid, use a dummy value if there isn't one yet.
    if (!userid)
        userid = "-";

    // initialize request
    req.cb = sizeof(req);
    req.operation = REGMAILDB_REQ_COUNT;
    strlcpy(req.userid, userid, sizeof(req.userid));
    strlcpy(req.email,  email,  sizeof(req.email));

    if (regmail_transact(&req, sizeof(req), &count, sizeof(count)) < 0)
        return -1;

    return count;
}

int emaildb_update_email(const char * userid, const char * email)
{
    int result = -1;
    regmaildb_req req = {0};

    // initialize request
    req.cb = sizeof(req);
    req.operation = REGMAILDB_REQ_SET;
    strlcpy(req.userid, userid, sizeof(req.userid));
    strlcpy(req.email,  email,  sizeof(req.email));

    if (regmail_transact(&req, sizeof(req), &result, sizeof(result)) < 0)
        return -1;

    return result;
}

#endif

#ifdef USE_REGCHECKD

// XXX move to regcheck someday
int regcheck_ambiguous_userid_exist(const char *userid)
{
    int result = -1;
    regmaildb_req req = {0};

    // initialize request
    req.cb = sizeof(req);
    req.operation = REGCHECK_REQ_AMBIGUOUS;
    strlcpy(req.userid, userid, sizeof(req.userid));
    strlcpy(req.email,  "ambiguous@check.non-exist",  sizeof(req.email));

    if (regmail_transact(&req, sizeof(req), &result, sizeof(result)) < 0)
        return -1;

    return result;
}

#endif

#ifdef USE_VERIFYDB

int verifydb_count(const char *userid, int64_t generation,
        int32_t vmethod, const char *vkey, void *rep)
{
    verifydb_req req = {};
    req.cb = sizeof(req);
    req.operation = VERIFYDB_REQ_COUNT;
    if (userid)
        strlcpy(req.userid, userid, sizeof(req.userid));
    req.generation = generation;
    req.vmethod = vmethod;
    if (vkey)
        strlcpy(req.vkey, vkey, sizeof(req.vkey));

    return regmail_transact(&req, sizeof(req), rep, sizeof(verifydb_count_rep));
}

int verifydb_set(const char *userid, int64_t generation,
        int32_t vmethod, const char *vkey)
{
    verifydb_req req = {};
    req.cb = sizeof(req);
    req.operation = VERIFYDB_REQ_SET;
    if (userid)
        strlcpy(req.userid, userid, sizeof(req.userid));
    req.generation = generation;
    req.vmethod = vmethod;
    if (vkey)
        strlcpy(req.vkey, vkey, sizeof(req.vkey));

    int status;
    int r = regmail_transact(&req, sizeof(req), &status, sizeof(status));
    if (r < 0)
        return r;
    return status;
}

#endif

// vim:et
