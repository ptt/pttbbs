/* $Id$ */
#ifndef UTIL_DB_PRIVATE_H
#define UTIL_DB_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <mongoc.h>
#include "ptterr.h"

// MONGO_COLLECTIONS
extern mongoc_collection_t **MONGO_COLLECTIONS;

#ifdef __cplusplus
}
#endif

#endif /* UTIL_DB_H */
