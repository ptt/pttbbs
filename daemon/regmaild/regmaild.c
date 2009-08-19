// Register Mail Daemon
// $Id$
//
// sqlite portion from wens
// Create:       Hung-Te Lin <piaip@csie.ntu.edu.tw>
// Initial Date: 2009/06/19
//
// Copyright (C) 2009, Hung-Te Lin <piaip@csie.ntu.edu.tw>
// All rights reserved

// TODO:

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include "common.h"
#include "pttstruct.h"

///////////////////////////////////////////////////////////////////////
// Common Section

#include <sqlite3.h>

// local definiions
#ifndef EMAILDB_PATH
#define EMAILDB_PATH BBSHOME "/emaildb.db"
#endif

sqlite3 *g_Db = NULL;

static int 
regmaildb_open(sqlite3 **Db, const char *fpath) {
    int rc;

    assert(fpath);
    if ((rc = sqlite3_open(fpath, Db)) != SQLITE_OK)
        return rc;

    // create table if it doesn't exist
    rc = sqlite3_exec(*Db, "CREATE TABLE IF NOT EXISTS emaildb (userid TEXT, email TEXT, PRIMARY KEY (userid));"
                "CREATE INDEX IF NOT EXISTS email ON emaildb (email);",
                NULL, NULL, NULL);

    return rc;
}

///////////////////////////////////////////////////////////////////////
// InitEmaildb Tool Main 

#ifdef INIT_MAIN

// standalone initialize builder

#define TRANSCATION_PERIOD (4096)
int main(int argc, char *argv[])
{
    int fd = 0;
    userec_t xuser;
    off_t sz = 0, i = 0, valids = 0;
    sqlite3 *Db = NULL;
    sqlite3_stmt *Stmt = NULL, *tranStart = NULL, *tranEnd = NULL;
    const char *fpath = EMAILDB_PATH;

    // init passwd
    sz = dashs(FN_PASSWD);
    fd = open(FN_PASSWD, O_RDONLY);
    if (fd < 0 || sz <= 0)
    {
        fprintf(stderr, "cannot open ~/.PASSWDS.\n");
        return 0;
    }
    sz /= sizeof(userec_t);

    if (argc > 1)
        fpath = argv[1];

    // init emaildb
    if (regmaildb_open(&Db, fpath) != SQLITE_OK)
    {
        fprintf(stderr, "cannot initialize emaildb: %s.\n", fpath);
        return 0;
    }

    if (sqlite3_prepare(Db, "REPLACE INTO emaildb (userid, email) VALUES (lower(?),lower(?));",
                -1, &Stmt, NULL) != SQLITE_OK ||
        sqlite3_prepare(Db, "BEGIN TRANSACTION;", -1, &tranStart, NULL) != SQLITE_OK ||
        sqlite3_prepare(Db, "COMMIT;", -1, &tranEnd, NULL) != SQLITE_OK)
    {
        fprintf(stderr, "SQLite 3 internal error.\n");
        return 0;
    }

    sqlite3_step(tranStart);
    sqlite3_reset(tranStart);
    while (read(fd, &xuser, sizeof(xuser)) == sizeof(xuser))
    {
        i++;
        // got a record
        if (strlen(xuser.userid) < 2 || strlen(xuser.userid) > IDLEN)
            continue;
        if (strlen(xuser.email) < 5)
            continue;

        if (sqlite3_bind_text(Stmt, 1, xuser.userid, strlen(xuser.userid), 
                    SQLITE_STATIC) != SQLITE_OK)
        {
            fprintf(stderr, "\ncannot prepare userid param.\n");
            break;
        }
        if (sqlite3_bind_text(Stmt, 2, xuser.email, strlen(xuser.email), 
                    SQLITE_STATIC) != SQLITE_OK)
        {
            fprintf(stderr, "\ncannot prepare email param.\n");
            break;
        }

        if (sqlite3_step(Stmt) != SQLITE_DONE)
        {
            fprintf(stderr, "\ncannot execute statement.\n");
            break;
        }
        sqlite3_reset(Stmt);

        valids ++;
        if (valids % 10 == 0)
            fprintf(stderr, "%d/%d (valid: %d)\r", 
                    (int)i, (int)sz, (int)valids);
        if (valids % TRANSCATION_PERIOD == 0)
        {
            sqlite3_step(tranEnd);
            sqlite3_step(tranStart);
            sqlite3_reset(tranEnd);
            sqlite3_reset(tranStart);
        }
    }

    if (valids % TRANSCATION_PERIOD)
        sqlite3_step(tranEnd);

    if (Stmt != NULL)
        sqlite3_finalize(Stmt);

    if (Db != NULL)
        sqlite3_close(Db);

    close(fd);
    return 0;
}

#else  // !INIT_MAIN

///////////////////////////////////////////////////////////////////////
// As Daemon

#include <event.h>
#include "bbs.h"
#include "daemons.h"

int 
regmaildb_check_email(const char * email, int email_len, const char *myid)
{
    int count = -1;
    sqlite3 *Db = NULL;
    sqlite3_stmt *Stmt = NULL;

    if (g_Db)
        Db =g_Db;
    else if (regmaildb_open(&Db, EMAILDB_PATH) != SQLITE_OK)
        goto end;

    // XXX == is faster than LIKE in this case, although it does not support '%' and case sensitive
    if (sqlite3_prepare(Db, "SELECT userid FROM emaildb WHERE email == lower(?);",
                -1, &Stmt, NULL) != SQLITE_OK)
        goto end;

    if (sqlite3_bind_text(Stmt, 1, email, email_len, SQLITE_STATIC) != SQLITE_OK)
        goto end;

    count = 0;
    while (sqlite3_step(Stmt) == SQLITE_ROW) {
        char *result;
        userec_t u;

        if ((result = (char*)sqlite3_column_text(Stmt, 0)) == NULL)
            break;

        // ignore my self, because I may be the one going to
        // use mail.
        if (strcasecmp(result, myid) == 0)
            continue;

        // force update
        u.email[0] = 0;
        
        if (passwd_load_user(result, &u))
            if (strcasecmp(email, u.email) == 0)
                count++;
    }

end:
    if (Stmt != NULL)
        if (sqlite3_finalize(Stmt) != SQLITE_OK)
            count = -1;

    if (Db != NULL && !g_Db)
        sqlite3_close(Db);

    return count;
}

int 
regmaildb_update_email(const char * userid, int userid_len, const char * email, int email_len)
{
    int ret = -1;

    sqlite3 *Db = NULL;
    sqlite3_stmt *Stmt = NULL;

    if (g_Db)
        Db =g_Db;
    else if (regmaildb_open(&Db, EMAILDB_PATH) != SQLITE_OK)
        goto end;

    if (strcmp(email, "x") == 0)
    {
        if (sqlite3_prepare(Db, "DELETE FROM emaildb WHERE userid == lower(?);",
                    -1, &Stmt, NULL) != SQLITE_OK)
            goto end;

        if (sqlite3_bind_text(Stmt, 1, userid, userid_len, SQLITE_STATIC) != SQLITE_OK)
            goto end;

    } else {

        if (sqlite3_prepare(Db, "REPLACE INTO emaildb (userid, email) VALUES (lower(?),lower(?));",
                    -1, &Stmt, NULL) != SQLITE_OK)
            goto end;

        if (sqlite3_bind_text(Stmt, 1, userid, userid_len, SQLITE_STATIC) != SQLITE_OK)
            goto end;

        if (sqlite3_bind_text(Stmt, 2, email, email_len, SQLITE_STATIC) != SQLITE_OK)
            goto end;
    }

    if (sqlite3_step(Stmt) == SQLITE_DONE)
        ret = 0;

end:
    if (Stmt != NULL)
        sqlite3_finalize(Stmt);
    if (Db != NULL && !g_Db)
        sqlite3_close(Db);

    return ret;
}

///////////////////////////////////////////////////////////////////////
// Ambiguous user id checking

void
build_unambiguous_userid(char *uid)
{
    int i = 0;
    const char *ambtbl[] = {    // need to also update ambchars if you touch these
	"0Oo",
	"1Il",
	NULL
    };

    for (i = 0; ambtbl[i]; )
    {
	size_t pos = strcspn(uid, ambtbl[i]);
	if (!uid[pos])
	{
	    i++;
	    continue;
	}
	uid[pos] = ambtbl[i][0];
	uid += (pos+1);	// skip the processed character
    }
}

const char *ambchars = "0Oo1Il";    // super set of ambtbl
char *unamb_user_list;
int   idx_unamb_ulist   = 0;
int   alloc_unamb_ulist = 0;
#define init_unamb_ulist_size   (1000)
#define inc_unamb_ulist_size    (1000)
#define element_unamb_ulist_size    (IDLEN+1)

void 
add_unamb_ulist(const char *uid)
{
    assert(idx_unamb_ulist <= MAX_USERS);
    if (idx_unamb_ulist >= alloc_unamb_ulist)
    {
        if (!alloc_unamb_ulist)
            alloc_unamb_ulist = init_unamb_ulist_size;
        else
            alloc_unamb_ulist += inc_unamb_ulist_size;
        unamb_user_list = realloc(unamb_user_list, alloc_unamb_ulist * element_unamb_ulist_size);
    }
    strlcpy(unamb_user_list + idx_unamb_ulist * element_unamb_ulist_size,
            uid, element_unamb_ulist_size);
    idx_unamb_ulist ++;
}

void
print_unamb_ulist(const char *prefix)
{
    int i;
    fprintf(stderr, "\n%s:\n", prefix);
    for (i = 0; i < idx_unamb_ulist; i++)
    {
        fprintf(stderr, " %s\n", unamb_user_list + i*element_unamb_ulist_size);
    }
    fprintf(stderr, "end\n");
}

void
build_unambiguous_user_list()
{
    int i;
    char xuid[IDLEN+1];
    idx_unamb_ulist = 0;    // rebuild ulist
    for (i = 0; i < MAX_USERS-1; i++)
    {
        const char *uid = SHM->userid[i];
        if (!uid[strcspn(uid, ambchars)])
            continue;

        strlcpy(xuid, uid, sizeof(xuid));
        build_unambiguous_userid(xuid);
        add_unamb_ulist(xuid);
    }
    fprintf(stderr, "build_unambiguous_user_list: found %d entries.\n", idx_unamb_ulist);
    // print_unamb_ulist("ambiguous list");
    if (idx_unamb_ulist > 1)
        qsort(unamb_user_list, idx_unamb_ulist, sizeof(xuid),
                (int (*)(const void *, const void *)) strcasecmp);
    // print_unamb_ulist("unambiguous list");
}

// fast version but requires unamb_user_list
int 
find_ambiguous_userid2(const char *userid)
{
    char ambuid[element_unamb_ulist_size];

    assert(userid && *userid);

    // if NULL, found nothing.
    if (!userid[strcspn(userid, ambchars)])
	return 0;

    // build un-ambiguous uid
    strlcpy(ambuid, userid, sizeof(ambuid));
    build_unambiguous_userid(ambuid);

    if (bsearch(ambuid, unamb_user_list, idx_unamb_ulist, element_unamb_ulist_size, 
            (int (*)(const void *, const void *)) strcasecmp) == NULL)
        return 0;
    return 1;
}

// slow version
int 
find_ambiguous_userid(const char *userid)
{
    size_t uidlen = 0, iamb;
    char ambuid[IDLEN+1], shmuid[IDLEN+1];
    int i;

    assert(userid && *userid);

    // if NULL, found nothing.
    iamb = strcspn(userid, ambchars);
    if (!userid[iamb])
	return 0;

    // build un-ambiguous uid
    uidlen = strlcpy(ambuid, userid, sizeof(ambuid));
    build_unambiguous_userid(ambuid);

    for (i = 0; i < MAX_USERS; i++)
    {
	const char *ruid = SHM->userid[i];

	// quick test: same non-amb prefix, and remote uid has amb characters
	if (iamb > 0 && tolower(*ruid) != tolower(*ambuid))
	    continue;
	if (!ruid[strcspn(ruid, ambchars)])
	    continue;

	// copy and check remote uid length
	if (strlcpy(shmuid, ruid, sizeof(shmuid)) != uidlen)
	    continue;

	build_unambiguous_userid(shmuid);
	if (strcasecmp(shmuid, ambuid) == 0)
	    return 1;
    }

    return 0;
}

int
regcheck_ambiguous_id(const char *userid)
{
    if (!userid || !*userid)
        return 0;
    if (find_ambiguous_userid(userid))
        return 1;
    return 0;
}

///////////////////////////////////////////////////////////////////////
// Callbacks

// Command syntax:
// operation\n
// userid\n
// email\n
//
// operation: set,count

static void 
client_cb(int fd, short event, void *arg)
{
    struct event *ev = (struct event*) arg;
    regmaildb_req req = {0};
    int ret = -1;

    assert(ev);

    if ( (event & EV_TIMEOUT) ||
        !(event & EV_READ) ||
         toread(fd, &req, sizeof(req)) != sizeof(req) ||
         req.cb != sizeof(req))
    {
        fprintf(stderr, "error: corrupted request.\r\n");
        close(fd);
        free(ev);
        return;
    }

    if (!*req.email)
    {
        fprintf(stderr, "invalid request: uid=[%s]\r\n", req.userid);
        close(fd);
        free(ev);
        return;
    }

    switch(req.operation)
    {
        case REGMAILDB_REQ_COUNT:
            ret = regmaildb_check_email(req.email, strlen(req.email), req.userid);
            fprintf(stderr, "%-*s check  mail (result: %d): [%s]\r\n", 
                    IDLEN, req.userid, ret, req.email);
            if (towrite(fd, &ret, sizeof(ret)) != sizeof(ret))
            {
                fprintf(stderr, " error: cannot write response...\r\n");
            }
            break;

        case REGMAILDB_REQ_SET:
            ret = regmaildb_update_email(req.userid, strlen(req.userid),
                    req.email, strlen(req.email));
            fprintf(stderr, "%-*s UPDATE mail (result: %d): [%s]\r\n", 
                    IDLEN, req.userid, ret, req.email);
            if (towrite(fd, &ret, sizeof(ret)) != sizeof(ret))
            {
                fprintf(stderr, " error: cannot write response...\r\n");
            }
            break;

        case REGCHECK_REQ_AMBIGUOUS:
            ret = regcheck_ambiguous_id(req.userid);
            fprintf(stderr, "%-*s check ambiguous id exist (result: %d)\r\n", 
                    IDLEN, req.userid, ret);
            if (towrite(fd, &ret, sizeof(ret)) != sizeof(ret))
            {
                fprintf(stderr, " error: cannot write response...\r\n");
            }
            break;

        default:
            fprintf(stderr, "error: invalid operation: %d.\r\n", req.operation);
            close(fd);
            free(ev);
            return;
    }

    // close connection anyway
    close(fd);
    free(ev);
}

///////////////////////////////////////////////////////////////////////

struct timeval tv = {60, 0};
static struct event ev_listen;

static void listen_cb(int fd, short event, void *arg)
{
    int cfd;

    if ((cfd = accept(fd, NULL, NULL)) < 0 )
	return;

    struct event *ev = malloc(sizeof(struct event));

    event_set(ev, cfd, EV_READ, client_cb, ev);
    event_add(ev, &tv);
}

///////////////////////////////////////////////////////////////////////
// Daemon Main 

int main(int argc, char *argv[])
{
    int     ch, sfd;
    char   *iface_ip = REGMAILD_ADDR;
    int     as_daemon = 1;

    Signal(SIGPIPE, SIG_IGN);

    chdir(BBSHOME);
    attach_SHM();

    /* Give up root privileges: no way back from here */
    setgid(BBSGID);
    setuid(BBSUID);

    while ( (ch = getopt(argc, argv, "i:hD")) != -1 )
	switch( ch ){
	case 'i':
	    iface_ip = optarg;
	    break;
        case 'D':
            as_daemon = 0;
            break;
	case 'h':
	default:
	    fprintf(stderr, "usage: %s [-D] [-i [interface_ip]:port]\n", argv[0]);
	    return 1;
	}

    if ( (sfd = tobind(iface_ip)) < 0 )
	return 1;

    if (as_daemon) daemonize(BBSHOME "/run/regmaild.pid", NULL);
    regmaildb_open(&g_Db, EMAILDB_PATH);
    build_unambiguous_user_list();

    event_init();
    event_set(&ev_listen, sfd, EV_READ | EV_PERSIST, listen_cb, &ev_listen);
    event_add(&ev_listen, NULL);
    fprintf(stderr, "start dispatch.\r\n");
    event_dispatch();

    return 0;
}

#endif // !INIT_MAIN

// vim:et
