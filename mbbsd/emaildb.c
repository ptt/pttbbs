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

	if ((result = sqlite3_column_text(Stmt, 0)) == NULL)
	    break;
	
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

// vim: sw=4
