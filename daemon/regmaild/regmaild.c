// Register Check Daemon
// $Id$
//
// sqlite portion from wens
// Create:       Hung-Te Lin <piaip@csie.ntu.edu.tw>
// Initial Date: 2009/06/19
// --------------------------------------------------------------------------
// Copyright (C) 2009, Hung-Te Lin <piaip@csie.ntu.edu.tw>
// All rights reserved
// Distributed under BSD license (GPL compatible).
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// --------------------------------------------------------------------------

// TODO:

#include <stdbool.h>
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

#include "pttstruct.h"
#include "common.h"

///////////////////////////////////////////////////////////////////////
// EmailDB Common Section

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
    rc = sqlite3_exec(*Db, 
            "CREATE TABLE IF NOT EXISTS emaildb (userid TEXT, email TEXT, PRIMARY KEY (userid));"
            "CREATE INDEX IF NOT EXISTS email ON emaildb (email);"
            "CREATE TABLE IF NOT EXISTS verifydb (\n"
            "  userid TEXT, generation INTEGER,\n"
            "  vmethod INTEGER, vkey TEXT, timestamp INTEGER,\n"
            "  PRIMARY KEY (userid, generation, vmethod));"
            "CREATE INDEX IF NOT EXISTS vmethodkey ON verifydb (vmethod, vkey);",
            NULL, NULL, NULL);

    return rc;
}

///////////////////////////////////////////////////////////////////////
// InitEmaildb Tool Main 

#ifdef INIT_MAIN

// standalone initialize builder

static bool
emaildb_prepare_stmt(sqlite3 *db, sqlite3_stmt **stmt)
{
    if (sqlite3_prepare(db, "REPLACE INTO emaildb (userid, email) VALUES (lower(?),lower(?));",
                -1, stmt, NULL) != SQLITE_OK)
    {
        fprintf(stderr, "Error preparing stmt: %s\n", sqlite3_errmsg(db));
        return false;
    }
    return true;
}

static bool
emaildb_bind_args(sqlite3 *db, sqlite3_stmt *stmt, const userec_t *u)
{
    if (sqlite3_bind_text(stmt, 1, u->userid, -1, SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 2, u->email, -1, SQLITE_STATIC) != SQLITE_OK)
    {
        fprintf(stderr, "Error binding args: %s\n", sqlite3_errmsg(db));
        return false;
    }
    return true;
}

static bool
emaildb_filter_user(const userec_t *u)
{
    if (strlen(u->userid) < 2 || strlen(u->userid) > IDLEN)
        return false;
    if (strlen(u->email) < 5)
        return false;
    return true;
}

typedef struct {
    bool (*prepare_stmt)(sqlite3 *db, sqlite3_stmt **stmt);
    bool (*bind_args)(sqlite3 *db, sqlite3_stmt *stmt, const userec_t *u);
    bool (*filter_user)(const userec_t *u);
} initemaildb_ops;

static initemaildb_ops emaildb_ops = {
    .prepare_stmt = emaildb_prepare_stmt,
    .bind_args = emaildb_bind_args,
    .filter_user = emaildb_filter_user,
};

#define TRANSCATION_PERIOD (4096)
int main(int argc, char *argv[])
{
    int fd = 0;
    userec_t xuser;
    off_t sz = 0, i = 0, valids = 0;
    sqlite3 *Db = NULL;
    sqlite3_stmt *Stmt = NULL, *tranStart = NULL, *tranEnd = NULL;
    const char *fpath = EMAILDB_PATH;
    const initemaildb_ops *ops = &emaildb_ops;

    // init passwd
    sz = dashs(FN_PASSWD);
    fd = open(FN_PASSWD, O_RDONLY);
    if (fd < 0 || sz <= 0)
    {
        fprintf(stderr, "cannot open %s.\n", FN_PASSWD);
        return 1;
    }
    sz /= sizeof(userec_t);

    if (argc > 1)
        fpath = argv[1];

    // init emaildb
    if (regmaildb_open(&Db, fpath) != SQLITE_OK)
    {
        fprintf(stderr, "cannot initialize emaildb: %s.\n", fpath);
        return 1;
    }

    if (!ops->prepare_stmt(Db, &Stmt))
        return 1;

    if (sqlite3_prepare(Db, "BEGIN TRANSACTION;", -1, &tranStart, NULL) != SQLITE_OK ||
        sqlite3_prepare(Db, "COMMIT;", -1, &tranEnd, NULL) != SQLITE_OK)
    {
        fprintf(stderr, "Error preparing txn stmts: %s\n", sqlite3_errmsg(Db));
        return 1;
    }

    int error = 0;
    sqlite3_step(tranStart);
    sqlite3_reset(tranStart);
    while (read(fd, &xuser, sizeof(xuser)) == sizeof(xuser))
    {
        i++;
        // got a record
        if (!ops->filter_user(&xuser))
            continue;

        if (!ops->bind_args(Db, Stmt, &xuser))
        {
            error = 1;
            break;
        }

        if (sqlite3_step(Stmt) != SQLITE_DONE)
        {
            error = 1;
            fprintf(stderr, "cannot execute statement: %s\n",
                    sqlite3_errmsg(Db));
            break;
        }
        sqlite3_reset(Stmt);

        valids++;
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

    fprintf(stderr, "%d/%d (valid: %d)\n",
            (int)i, (int)sz, (int)valids);

    if (Db != NULL)
        sqlite3_close(Db);

    close(fd);
    return error;
}

#else  // !INIT_MAIN

///////////////////////////////////////////////////////////////////////
// As Daemon

#include <event.h>
#include <buffer.h>
#include "bbs.h"
#include "daemons.h"

int 
regmaildb_check_email(const char * email, int email_len, const char *myid)
{
    int count = -1;
    sqlite3 *Db = g_Db;
    sqlite3_stmt *Stmt = NULL;

    // XXX == is faster than LIKE in this case, although it does not support '%' and case sensitive
    if (sqlite3_prepare(Db, "SELECT userid FROM emaildb WHERE email == lower(?);",
                -1, &Stmt, NULL) != SQLITE_OK)
        goto end;

    if (sqlite3_bind_text(Stmt, 1, email, email_len, SQLITE_STATIC) != SQLITE_OK)
    {
        fprintf(stderr, "failed in sqlite3_bind_text\n");
        goto end;
    }

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
    {
        int r = sqlite3_finalize(Stmt);
        if (r != SQLITE_OK)
        {
            fprintf(stderr, "sqlite3_finalize error: %d %s\n", r, sqlite3_errmsg(Db));
            count = -1;
        }
    }

    return count;
}

int 
regmaildb_update_email(const char * userid, int userid_len, const char * email, int email_len)
{
    int ret = -1;

    sqlite3 *Db = g_Db;
    sqlite3_stmt *Stmt = NULL;

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

    return ret;
}

static void
verifydb_count(const verifydb_req *req, verifydb_count_rep *rep)
{
    sqlite3 *Db = g_Db;
    sqlite3_stmt *Stmt = NULL;
    int r = SQLITE_OK;

    rep->cb = sizeof(*rep);
    rep->status = VERIFYDB_ERROR;

    r = sqlite3_prepare(Db, "SELECT userid, generation FROM verifydb "
            "WHERE vmethod = ? AND vkey = ?;", -1, &Stmt, NULL);
    if (r != SQLITE_OK)
        goto end;

    r = sqlite3_bind_int64(Stmt, 1, req->vmethod);
    if (r != SQLITE_OK)
        goto end;
    r = sqlite3_bind_text(Stmt, 2, req->vkey, -1, SQLITE_STATIC);
    if (r != SQLITE_OK)
        goto end;

    while (sqlite3_step(Stmt) == SQLITE_ROW) {
        userec_t u = {};
        const char *userid = (const char *)sqlite3_column_text(Stmt, 0);
        int64_t generation = sqlite3_column_int64(Stmt, 1);

        if (userid == NULL)
            continue;  // Bad record.

        if (!passwd_load_user(userid, &u))
            continue;  // No such user.

        // Make sure the record corresponds to user of current generation.
        if (u.firstlogin != generation) {
            fprintf(stderr, "verifydb count: skipped old record: %s "
                    "(generation: db %ld, current %d)\n",
                    u.userid, generation, u.firstlogin);
            continue;
        }

        if (strcasecmp(userid, req->userid) == 0)
            rep->count_self++;
        else
            rep->count_other++;
    }

    rep->status = VERIFYDB_OK;

end:
    if (r != SQLITE_OK)
        fprintf(stderr, "sqlite3: %d %s\n", r, sqlite3_errmsg(Db));

    if (Stmt != NULL) {
        r = sqlite3_finalize(Stmt);
        if (r != SQLITE_OK)
            fprintf(stderr, "sqlite3_finalize error: %d %s\n", r, sqlite3_errmsg(Db));
    }
}

static int
verifydb_set(const verifydb_req *req)
{
    int ret = VERIFYDB_ERROR;

    sqlite3 *Db = g_Db;
    sqlite3_stmt *Stmt = NULL;

    if (!req->vkey[0]) {
        if (sqlite3_prepare(Db, "DELETE FROM verifydb "
                    "WHERE userid = lower(?) AND generation = ? AND vmethod = ?;",
                    -1, &Stmt, NULL) != SQLITE_OK)
            goto end;
        if (sqlite3_bind_text(Stmt, 1, req->userid, -1, SQLITE_STATIC) != SQLITE_OK)
            goto end;
        if (sqlite3_bind_int64(Stmt, 2, req->generation) != SQLITE_OK)
            goto end;
        if (sqlite3_bind_int64(Stmt, 3, req->vmethod) != SQLITE_OK)
            goto end;
    } else {
        if (sqlite3_prepare(Db, "REPLACE INTO verifydb "
                    "(userid, generation, vmethod, vkey, timestamp) VALUES "
                    "(lower(?),?,?,?,?);",
                    -1, &Stmt, NULL) != SQLITE_OK)
            goto end;
        if (sqlite3_bind_text(Stmt, 1, req->userid, -1, SQLITE_STATIC) != SQLITE_OK)
            goto end;
        if (sqlite3_bind_int64(Stmt, 2, req->generation) != SQLITE_OK)
            goto end;
        if (sqlite3_bind_int64(Stmt, 3, req->vmethod) != SQLITE_OK)
            goto end;
        if (sqlite3_bind_text(Stmt, 4, req->vkey, -1, SQLITE_STATIC) != SQLITE_OK)
            goto end;
        int64_t timestamp = time(NULL);
        if (sqlite3_bind_int64(Stmt, 5, timestamp) != SQLITE_OK)
            goto end;
    }

    if (sqlite3_step(Stmt) == SQLITE_DONE)
        ret = VERIFYDB_OK;

end:
    if (Stmt != NULL)
        sqlite3_finalize(Stmt);

    return ret;
}

static void
verifydb_get(const verifydb_req *req, int fd)
{
    sqlite3 *Db = g_Db;
    sqlite3_stmt *Stmt = NULL;
    struct evbuffer *buf = NULL;
    int r = SQLITE_OK;

    verifydb_get_rep rep = {};
    rep.cb = sizeof(rep);
    rep.status = VERIFYDB_ERROR;
    rep.entry_size = sizeof(verifydb_entry);

    r = sqlite3_prepare(Db, "SELECT timestamp, vmethod, vkey FROM verifydb "
            "WHERE userid = lower(?) AND generation = ? AND "
            "(1 = ? OR vmethod = ?) "
            "ORDER BY vmethod ASC;",
            -1, &Stmt, NULL);
    if (r != SQLITE_OK)
        goto end;

    r = sqlite3_bind_text(Stmt, 1, req->userid, -1, SQLITE_STATIC);
    if (r != SQLITE_OK)
        goto end;
    r = sqlite3_bind_int64(Stmt, 2, req->generation);
    if (r != SQLITE_OK)
        goto end;
    r = sqlite3_bind_int64(Stmt, 3, req->vmethod == VMETHOD_UNSET ? 1 : 0);
    if (r != SQLITE_OK)
        goto end;
    r = sqlite3_bind_int64(Stmt, 4, req->vmethod);
    if (r != SQLITE_OK)
        goto end;

    buf = evbuffer_new();
    if (!buf)
        goto end;
    while (sqlite3_step(Stmt) == SQLITE_ROW) {
        const int64_t timestamp = sqlite3_column_int64(Stmt, 0);
        const int32_t vmethod = sqlite3_column_int64(Stmt, 1);
        const char *vkey = (const char *)sqlite3_column_text(Stmt, 2);
        if (!vkey)
            vkey = "";

        verifydb_entry ent = {};
        ent.timestamp = timestamp;
        ent.vmethod = vmethod;
        strlcpy(ent.vkey, vkey, sizeof(ent.vkey));

        fprintf(stderr, "verifydb get-entry: %s generation [%ld] vmethod [%d] "
                "(timestamp %ld, vmethod [%d], vkey [%s])\n",
                req->userid, req->generation, req->vmethod,
                ent.timestamp, ent.vmethod, ent.vkey);

        if (evbuffer_add(buf, &ent, sizeof(ent)) != 0)
            goto end;
        rep.num_entries++;
    }

    rep.status = VERIFYDB_OK;
    if (evbuffer_prepend(buf, &rep, sizeof(rep)) != 0)
        goto end;

    fprintf(stderr, "verifydb get: %s generation [%ld] vmethod [%d] "
            "(status %d, num_entries %lu)\n",
            req->userid, req->generation, req->vmethod,
            rep.status, rep.num_entries);

    size_t len = evbuffer_get_length(buf);
    if (towrite(fd, evbuffer_pullup(buf, len), len) != len)
        fprintf(stderr, "error: cannot write response...\n");

end:
    if (buf)
        evbuffer_free(buf);

    if (r != SQLITE_OK)
        fprintf(stderr, "sqlite3: %d %s\n", r, sqlite3_errmsg(Db));

    if (Stmt != NULL) {
        r = sqlite3_finalize(Stmt);
        if (r != SQLITE_OK)
            fprintf(stderr, "sqlite3_finalize error: %d %s\n", r, sqlite3_errmsg(Db));
    }
}

///////////////////////////////////////////////////////////////////////
// Ambiguous user id checking

// XXX TODO add a linear queue to handle the difference between each reload?

// XXX the reason to use case-insensitive letters (o, iL) here
// is because we really see people trying to register then
// ask SYSOP to change their id.
#define AMBLIST1    "0Oo"
#define AMBLIST2    "1IliL"
#define AMBLIST3    "5Ss"
#define AMBLIST4    "2Zz"

void
build_unambiguous_userid(char *uid)
{
    int i = 0;
    const char *ambtbl[] = {    // need to also update ambchars if you touch these
        AMBLIST1,
        AMBLIST2,
        AMBLIST3,
        AMBLIST4,
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

const char *ambchars = AMBLIST1 AMBLIST2 AMBLIST3 AMBLIST4; // super set

typedef struct {
    char *list;
    int   num;
    int   alloc;
} AmbUserList;

AmbUserList unamb_ulist,  orig_ulist;
#define ulist_size_init   (MAX_USERS/ 4)  // usually 2/3 of valid accounts
#define ulist_size_inc    (MAX_USERS/10)
#define ulist_size_element     (IDLEN+1)

time_t  unamb_ulist_cache_ts = 0;               // timestamp of last hit
#define unamb_ulist_cache_lifetime  (60*60*1)   // update unamb user list for every 1 hours

void 
add_amb_ulist(AmbUserList *ulist, const char *uid)
{
    assert(ulist->num <= MAX_USERS);
    if (ulist->num >= ulist->alloc)
    {
        ulist->alloc += (ulist->alloc ? ulist_size_inc  : ulist_size_init);
        ulist->list = realloc(ulist->list, ulist->alloc * ulist_size_element);
    }
    strlcpy(ulist->list + ulist->num * ulist_size_element,
            uid, ulist_size_element);
    ulist->num++;
}

void
print_amb_ulist(AmbUserList *ulist, const char *prefix)
{
    int i;
    fprintf(stderr, "\n%s:\n", prefix);
    for (i = 0; i < ulist->num; i++)
    {
        fprintf(stderr, " %s\n", ulist->list + i*ulist_size_element);
    }
    fprintf(stderr, "end\n");
}

void
reload_unambiguous_user_list()
{
    int i, ivalid = 0;
    char xuid[IDLEN+1];
    time_t now = time(NULL);

    if (now < unamb_ulist_cache_ts + unamb_ulist_cache_lifetime)
        return;

    fprintf(stderr, "start to reload unambiguous user list: %s", ctime(&now));
    unamb_ulist_cache_ts = now;
    // rebuild both list
    unamb_ulist.num = 0;
    orig_ulist.num  = 0;
    for (i = 0; i < MAX_USERS-1; i++)
    {
        const char *uid = SHM->userid[i];
        if (*uid) ivalid ++;
        if (!uid[strcspn(uid, ambchars)])
            continue;

        add_amb_ulist(&orig_ulist, uid);
        strlcpy(xuid, uid, sizeof(xuid));
        build_unambiguous_userid(xuid);
        add_amb_ulist(&unamb_ulist, xuid);
    }
    fprintf(stderr, "reload_unambiguous_user_list: found %d/%d (%d%%) entries.\n",
            unamb_ulist.num, ivalid, (int)(unamb_ulist.num / (double)ivalid * 100));

    // print_amb_ulist(&unamb_ulist, "ambiguous list");
    if (unamb_ulist.num > 1)
    {
        qsort(unamb_ulist.list, unamb_ulist.num, ulist_size_element,
                (int (*)(const void *, const void *)) strcasecmp);
        qsort(orig_ulist.list, orig_ulist.num, ulist_size_element,
                (int (*)(const void *, const void *)) strcasecmp);
    }
    // print_amb_ulist(&unamb_ulist, "unambiguous list");
}

// fast version but requires unamb_user_list
int 
find_ambiguous_userid2(const char *userid)
{
    char ambuid[ulist_size_element];
    int i;
    char *p;

    assert(userid && *userid);

    // if NULL, found nothing.
    if (!userid[strcspn(userid, ambchars)])
	return 0;

    // build un-ambiguous uid
    reload_unambiguous_user_list();
    strlcpy(ambuid, userid, sizeof(ambuid));
    build_unambiguous_userid(ambuid);

    if ((p = bsearch(ambuid, unamb_ulist.list, unamb_ulist.num, ulist_size_element, 
            (int (*)(const void *, const void *)) strcasecmp)) == NULL)
        return 0;

    // search the original id in our list (if not found, safe to claim as ambiguous)
    if (bsearch(userid, orig_ulist.list, orig_ulist.num, ulist_size_element, 
            (int (*)(const void *, const void *)) strcasecmp) == NULL)
    {
        // fprintf(stderr, " really ambiguous: %s\n", userid);
        return 1;
    }

    // the id was in our list (and should be removed now). determine if it's an exact match.
    i = (p - unamb_ulist.list) / ulist_size_element;
    assert (i >=0 && i < unamb_ulist.num);

    // forware and backward to see if this is an exact match.
    if (i > 0 && strcasecmp(unamb_ulist.list + (i-1) * ulist_size_element, ambuid) == 0)
    {
        // fprintf(stderr, " ambiguous in prior: %s\n", userid);
        return 1;
    }
    if (i+1 < unamb_ulist.num && strcasecmp(unamb_ulist.list + (i+1) * ulist_size_element, ambuid) == 0)
    {
        // fprintf(stderr, " ambiguous in next: %s\n", userid);
        return 1;
    }
    // fprintf(stderr, " an exact match: %s\n", userid);
    return 0;
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
    if (find_ambiguous_userid2(userid))
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

static bool
check_null_terminated(const char *s, size_t sz)
{
    return memchr(s, 0, sz) != NULL;
}

static bool
validate_regmaildb_request(const regmaildb_req *req)
{
    if (req->cb != sizeof(*req)) {
        fprintf(stderr, "error: corrupted request, cb: %lu != %lu\n",
                req->cb, sizeof(*req));
        return false;
    }

    switch (req->operation)
    {
        case REGMAILDB_REQ_COUNT:
        case REGMAILDB_REQ_SET:
            if (!*req->userid || !*req->email) {
                fprintf(stderr, "invalid request: op=[%d], uid=[%s]\n",
                        req->operation,
                        req->userid);
                return false;
            }
            break;

        case REGCHECK_REQ_AMBIGUOUS:
            // No checks.
            break;

        default:
            fprintf(stderr, "unknown request: op=[%d]\n", req->operation);
            return false;
    }
    return true;
}

static bool
validate_verifydb_request(const verifydb_req *req)
{
    if (req->cb != sizeof(*req)) {
        fprintf(stderr, "error: corrupted request, cb: %lu != %lu\n",
                req->cb, sizeof(*req));
        return false;
    }
    if (!check_null_terminated(req->userid, sizeof(req->userid))) {
        fprintf(stderr, "invalid request: op=[%d], userid is not null-terminated\n",
                req->operation);
        return false;
    }
    if (!*req->userid) {
        fprintf(stderr, "invalid request: op=[%d], empty userid\n",
                req->operation);
        return false;
    }
    if (req->operation == VERIFYDB_REQ_COUNT ||
        req->operation == VERIFYDB_REQ_SET) {
        if (req->vmethod == VMETHOD_UNSET) {
            fprintf(stderr, "invalid request: op=[%d], vmethod is not set\n",
                    req->operation);
            return false;
        }
        if (!check_null_terminated(req->vkey, sizeof(req->vkey))) {
            fprintf(stderr, "invalid request: op=[%d], vkey is not null-terminated\n",
                    req->operation);
            return false;
        }
    }
    return true;
}

static void 
client_cb(int fd, short event, void *arg)
{
    struct event *ev = (struct event*) arg;
    regmaildb_req_storage storage = {};
    regmaildb_req_header *header = &storage.header;
    int8_t *rest = (int8_t *)&storage;
    rest += sizeof(*header);

    assert(ev);

    if ((event & EV_TIMEOUT) ||
        !(event & EV_READ) ||
        toread(fd, header, sizeof(*header)) != sizeof(*header) ||
        header->cb < sizeof(*header) ||
        header->cb > sizeof(storage) ||
        toread(fd, rest, header->cb - sizeof(*header)) != header->cb - sizeof(*header))
    {
        fprintf(stderr, "error: corrupted request.\n");
        goto end;
    }

    switch (header->operation)
    {
        case REGMAILDB_REQ_COUNT:
        {
            regmaildb_req *req = &storage.regmaildb;
            if (!validate_regmaildb_request(req))
                goto end;

            int ret = regmaildb_check_email(req->email, strlen(req->email), req->userid);
            fprintf(stderr, "%-*s check  mail (result: %d): [%s]\n", 
                    IDLEN, req->userid, ret, req->email);
            if (towrite(fd, &ret, sizeof(ret)) != sizeof(ret))
            {
                fprintf(stderr, " error: cannot write response...\n");
            }
            break;
        }

        case REGMAILDB_REQ_SET:
        {
            regmaildb_req *req = &storage.regmaildb;
            if (!validate_regmaildb_request(req))
                goto end;

            int ret = regmaildb_update_email(req->userid, strlen(req->userid),
                    req->email, strlen(req->email));
            fprintf(stderr, "%-*s UPDATE mail (result: %d): [%s]\n", 
                    IDLEN, req->userid, ret, req->email);
            if (towrite(fd, &ret, sizeof(ret)) != sizeof(ret))
            {
                fprintf(stderr, " error: cannot write response...\n");
            }
            break;
        }

        case REGCHECK_REQ_AMBIGUOUS:
        {
            regmaildb_req *req = &storage.regmaildb;
            if (!validate_regmaildb_request(req))
                goto end;

            int ret = regcheck_ambiguous_id(req->userid);
            fprintf(stderr, "%-*s check ambiguous id exist (result: %d)\n", 
                    IDLEN, req->userid, ret);
            if (towrite(fd, &ret, sizeof(ret)) != sizeof(ret))
            {
                fprintf(stderr, " error: cannot write response...\n");
            }
            break;
        }

        case VERIFYDB_REQ_COUNT:
        {
            const verifydb_req *req = &storage.verifydb;
            if (!validate_verifydb_request(req))
                goto end;

            verifydb_count_rep rep = {};
            verifydb_count(req, &rep);
            fprintf(stderr, "verifydb count: %s generation [%ld] vmethod [%d] vkey [%s] "
                    "(status: %d, self: %d, other %d)\n",
                    req->userid, req->generation, req->vmethod, req->vkey,
                    rep.status, rep.count_self, rep.count_other);
            if (towrite(fd, &rep, sizeof(rep)) != sizeof(rep))
                fprintf(stderr, " error: cannot write response...\n");
            break;
        }

        case VERIFYDB_REQ_SET:
        {
            const verifydb_req *req = &storage.verifydb;
            if (!validate_verifydb_request(req))
                goto end;

            int ret = verifydb_set(req);
            fprintf(stderr, "verifydb set: %s generation [%ld] vmethod [%d] vkey [%s] (status: %d)\n",
                    req->userid, req->generation, req->vmethod, req->vkey, ret);
            if (towrite(fd, &ret, sizeof(ret)) != sizeof(ret))
                fprintf(stderr, " error: cannot write response...\n");
            break;
        }

        case VERIFYDB_REQ_GET:
        {
            const verifydb_req *req = &storage.verifydb;
            if (!validate_verifydb_request(req))
                goto end;

            verifydb_get(req, fd);
            break;
        }

        default:
            fprintf(stderr, "error: invalid operation: %d.\n", header->operation);
            break;
    }

end:
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

    if (as_daemon) daemonize(BBSHOME "/run/regmaild.pid", BBSHOME "/log/regmaild.log");

    int res = regmaildb_open(&g_Db, EMAILDB_PATH);
    if (res != SQLITE_OK) {
        fprintf(stderr, "regmaildb_open: %d %s\n", res, sqlite3_errstr(res));
        return 1;
    }
    reload_unambiguous_user_list();

    event_init();
    event_set(&ev_listen, sfd, EV_READ | EV_PERSIST, listen_cb, &ev_listen);
    event_add(&ev_listen, NULL);
    fprintf(stderr, "start dispatch.\n");
    event_dispatch();

    return 0;
}

#endif // !INIT_MAIN

// vim:et
