/* $Id$ */
#ifndef TEST_H
#define TEST_H

#ifdef MONGO_CLIENT_URL
#include <mongoc.h>
#endif

#include "cmpttdb/pttdb_util.h"
#include "cmpttdb/util_db_private.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/**********
 * Mongo
 **********/

// XXX NEVER USE UNLESS IN TEST
Err _DB_FORCE_DROP_COLLECTION(int collection);

/**********
 * Misc
 **********/

Err form_rand_list(int n, int **rand_list);

#ifdef __cplusplus
}
#endif

#endif /* TEST_H */
