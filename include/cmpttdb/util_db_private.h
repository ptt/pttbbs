/* $Id$ */
#ifndef UTIL_DB_PRIVATE_H
#define UTIL_DB_PRIVATE_H

#include <mongoc.h>

#include "ptterr.h"
#include "cmpttdb/pttdb_const.h"
#include "cmpttdb/util_db.h"

#ifdef __cplusplus
extern "C" {
#endif

// MONGO_COLLECTIONS
extern mongoc_collection_t **MONGO_COLLECTIONS;

#ifdef __cplusplus
}
#endif

#endif /* UTIL_DB_PRIVATE_H */
