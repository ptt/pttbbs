/* $Id$ */

#include "bbs.h"
#include "daemons.h"

#ifdef USE_EMAILDB

int emaildb_check_email(const char * email, int email_len)
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

    if (towrite(fd, &req, sizeof(req)) != sizeof(req)) {
        // perror("towrite");
        close(fd);
        return -1;
    }

    if (toread(fd, &count, sizeof(count)) != sizeof(count)) {
        // perror("toread");
        close(fd);
        return -1;
    }

    return count;
}

int emaildb_update_email(const char * userid, int userid_len, const char * email, int email_len)
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

    if (towrite(fd, &req, sizeof(req)) != sizeof(req)) {
        // perror("towrite");
        close(fd);
        return -1;
    }

    if (toread(fd, &result, sizeof(result)) != sizeof(result)) {
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

    if (towrite(fd, &req, sizeof(req)) != sizeof(req)) {
        // perror("towrite");
        close(fd);
        return -1;
    }

    if (toread(fd, &result, sizeof(result)) != sizeof(result)) {
        // perror("toread");
        close(fd);
        return -1;
    }

    return result;
}

#endif

// vim:et
