/* $Id$ */
#ifndef UTIL_DB_INTERNAL_H
#define UTIL_DB_INTERNAL_H

#include <mongoc.h>
#include "ptterr.h"

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

#endif /* UTIL_DB_INTERNAL_H */