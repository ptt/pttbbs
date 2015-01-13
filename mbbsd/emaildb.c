
#include "bbs.h"
#include "daemons.h"

#ifdef USE_EMAILDB

int emaildb_check_email(const char * email)
{
    int count = -1;
    int fd = -1;
    regmaildb_req req = {0};

    // initialize request
    req.cb = sizeof(req);
    req.operation = REGMAILDB_REQ_COUNT;
    strlcpy(req.userid, cuser.userid, sizeof(req.userid));
    strlcpy(req.email,  email,        sizeof(req.email));

    if ( (fd = toconnect(REGMAILD_ADDR)) < 0 )
    {
        // perror("toconnect");
        return -1;
    }

    if (towrite(fd, &req, sizeof(req)) < 0) {
        // perror("towrite");
        close(fd);
        return -1;
    }

    if (toread(fd, &count, sizeof(count)) < 0) {
        // perror("toread");
        close(fd);
        return -1;
    }

    return count;
}

int emaildb_update_email(const char * userid, const char * email)
{
    int result = -1;
    int fd = -1;
    regmaildb_req req = {0};

    // initialize request
    req.cb = sizeof(req);
    req.operation = REGMAILDB_REQ_SET;
    strlcpy(req.userid, userid, sizeof(req.userid));
    strlcpy(req.email,  email,  sizeof(req.email));

    if ( (fd = toconnect(REGMAILD_ADDR)) < 0 )
    {
        // perror("toconnect");
        return -1;
    }

    if (towrite(fd, &req, sizeof(req)) < 0) {
        // perror("towrite");
        close(fd);
        return -1;
    }

    if (toread(fd, &result, sizeof(result)) < 0) {
        // perror("toread");
        close(fd);
        return -1;
    }

    return result;
}

#endif

#ifdef USE_REGCHECKD

// XXX move to regcheck someday
int regcheck_ambiguous_userid_exist(const char *userid)
{
    int result = -1;
    int fd = -1;
    regmaildb_req req = {0};

    // initialize request
    req.cb = sizeof(req);
    req.operation = REGCHECK_REQ_AMBIGUOUS;
    strlcpy(req.userid, userid, sizeof(req.userid));
    strlcpy(req.email,  "ambiguous@check.non-exist",  sizeof(req.email));

    if ( (fd = toconnect(REGMAILD_ADDR)) < 0 )
    {
        // perror("toconnect");
        return -1;
    }

    if (towrite(fd, &req, sizeof(req)) < 0) {
        // perror("towrite");
        close(fd);
        return -1;
    }

    if (toread(fd, &result, sizeof(result)) < 0) {
        // perror("toread");
        close(fd);
        return -1;
    }

    return result;
}

#endif

// vim:et
