/* $Id$ */
#ifndef PTTDB_UUID_H
#define PTTDB_UUID_H

#include <mongoc.h>
#include "ptterr.h"
#include "cmutil_time.h"
#include "cmpttdb/pttdb_const.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UUIDLEN 16
#define DISPLAY_UUIDLEN 24

#define N_ITER_GEN_UUID_WITH_DB 10

// XXX hack for UUID

typedef unsigned char UUID[UUIDLEN];

extern const UUID EMPTY_ID;

/**********
 * UUID
 **********/
Err gen_uuid(UUID uuid, time64_t milli_timestamp);
Err gen_uuid_with_db(int collection, UUID uuid, time64_t milli_timestamp);
Err gen_content_uuid_with_db(int collection, UUID uuid, time64_t milli_timestamp);

Err uuid_to_milli_timestamp(UUID uuid, time64_t *milli_timestamp);

Err serialize_uuid_bson(UUID uuid, bson_t **uuid_bson);
Err serialize_content_uuid_bson(UUID uuid, int block_id, bson_t **uuid_bson);

char *display_uuid(UUID uuid);
char *display_urlsafe_uuid(UUID uuid);


#ifdef __cplusplus
}
#endif

#endif /* PTTDB_UUID_H */
