/* $Id$ */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <mongoc.h>
#include "ptterr.h"
#include "util_db_private.h"

#ifdef __cplusplus
extern "C" {
#endif

/**********
 * Mongo
 **********/

// XXX NEVER USE UNLESS IN TEST
Err _DB_FORCE_DROP_COLLECTION(int collection);

#ifdef __cplusplus
}
#endif

#endif /* TEST_UTIL_H */
