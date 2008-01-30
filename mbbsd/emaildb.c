/* $Id$ */
#include <sqlite3.h>
#include "bbs.h"

#define EMAILDB_PATH BBSHOME "/emaildb.db"

static int emaildb_open(sqlite3 **Db) {
    int rc;

    if ((rc = sqlite3_open(EMAILDB_PATH, Db)) != SQLITE_OK)
	return rc;

    // create table if it doesn't exist
    rc = sqlite3_exec(*Db, "CREATE TABLE IF NOT EXISTS emaildb (userid TEXT, email TEXT, PRIMARY KEY (userid));"
		"CREATE INDEX IF NOT EXISTS email ON emaildb (email);",
		NULL, NULL, NULL);

    return rc;
}

#ifndef INIT_MAIN
int emaildb_check_email(char * email, int email_len)
{
    int count = -1;
    sqlite3 *Db = NULL;
    sqlite3_stmt *Stmt = NULL;

    if (emaildb_open(&Db) != SQLITE_OK)
	goto end;

    if (sqlite3_prepare(Db, "SELECT userid FROM emaildb WHERE email LIKE lower(?);",
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
	if (strcasecmp(result, cuser.userid) == 0)
	    continue;
	
	if (getuser(result, &u))
	    if (strcasecmp(email, u.email) == 0)
		count++;
    }

end:
    if (Stmt != NULL)
	if (sqlite3_finalize(Stmt) != SQLITE_OK)
	    count = -1;

    if (Db != NULL)
	sqlite3_close(Db);

    return count;
}
#endif

int emaildb_update_email(char * userid, int userid_len, char * email, int email_len)
{
    int ret = -1;
    sqlite3 *Db = NULL;
    sqlite3_stmt *Stmt = NULL;

    if (emaildb_open(&Db) != SQLITE_OK)
	goto end;

    if (sqlite3_prepare(Db, "REPLACE INTO emaildb (userid, email) VALUES (lower(?),lower(?));",
		-1, &Stmt, NULL) != SQLITE_OK)
	goto end;

    if (sqlite3_bind_text(Stmt, 1, userid, userid_len, SQLITE_STATIC) != SQLITE_OK)
	goto end;

    if (sqlite3_bind_text(Stmt, 2, email, email_len, SQLITE_STATIC) != SQLITE_OK)
	goto end;

    if (sqlite3_step(Stmt) == SQLITE_DONE)
	ret = 0;

end:
    if (Stmt != NULL)
	sqlite3_finalize(Stmt);
    if (Db != NULL)
	sqlite3_close(Db);

    return ret;
}

#ifdef INIT_MAIN

// standalone initialize builder

#define TRANSCATION_PERIOD (4096)
int main()
{
    int fd = 0;
    userec_t xuser;
    off_t sz = 0, i = 0, valids = 0;
    sqlite3 *Db = NULL;
    sqlite3_stmt *Stmt = NULL, *tranStart = NULL, *tranEnd = NULL;

    // init passwd
    sz = dashs(FN_PASSWD);
    fd = open(FN_PASSWD, O_RDONLY);
    if (fd < 0 || sz <= 0)
    {
	fprintf(stderr, "cannot open ~/.PASSWDS.\n");
	return 0;
    }
    sz /= sizeof(userec_t);

    // init emaildb
    if (emaildb_open(&Db) != SQLITE_OK)
    {
	fprintf(stderr, "cannot initialize emaildb.\n");
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
#endif

// vim: sw=4
