#include "cmpttdb/pttdb_uuid.h"
#include "cmpttdb/pttdb_uuid_private.h"

const UUID EMPTY_ID = {};

/**
 * @brief Generate customized uuid (maybe we can use libuuid to save some random thing.)
 * @details Generate customized uuid (ASSUMING little endian in the system):
 *          1. byte[0-6]: random
 *          2. byte[7]: 0x60 as version
 *          3. byte[8-9]: random
 *          4: byte[10-15]: milli-timestamp
 *
 * @param uuid [description]
 */
Err
gen_uuid(UUID uuid, time64_t milli_timestamp)
{
    Err error_code;

    time64_t *p_milli_timestamp;

    // RAND_MAX is 2147483647

    int *p_rand;
    const int n_random = (UUIDLEN - 8) / 4;

    long int rand_num;
    unsigned short *p_short_rand_num;

    unsigned short *p_short;
    unsigned char *p_char;
    // _UUID _uuid;

    // last 8 chars as milli-timestamp, but only the last 6 chars will be used. little-endian
    if(!milli_timestamp) {
        error_code = get_milli_timestamp(&milli_timestamp);
        if (error_code) return error_code;
    }

    milli_timestamp <<= 16;
    p_char = (unsigned char *)&milli_timestamp;

    p_milli_timestamp = (time64_t *)(uuid + UUIDLEN - 8);
    *p_milli_timestamp = milli_timestamp;

    rand_num = random();
    p_short_rand_num = (unsigned short *)&rand_num;
    p_short = (unsigned short*)(uuid + UUIDLEN - 8);
    *p_short = *p_short_rand_num;

    // first 16 chars as random, but 6th char is version (6 for now)
    p_rand = (int *)uuid;
    for (int i = 0; i < n_random; i++) {
        rand_num = random();
        *p_rand = (int)rand_num;
        p_rand++;
    }

    uuid[6] &= 0x0f;
    uuid[6] |= 0x60;

    // b64_ntop(_uuid, _UUIDLEN, (char *)uuid, UUIDLEN);

    return S_OK;
}

/**
 * @brief Gen uuid and check with db.
 * @details Gen uuid and check with db for uniqueness.
 *
 * @param collection Collection
 * @param uuid uuid (to-compute).
 * @return Err
 */
Err
gen_uuid_with_db(int collection, UUID uuid, time64_t milli_timestamp)
{
    Err error_code = S_OK;
    bson_t *uuid_bson = NULL;

    for (int i = 0; i < N_ITER_GEN_UUID_WITH_DB; i++) {
        error_code = gen_uuid(uuid, milli_timestamp);

        if(!error_code) {
            error_code = serialize_uuid_bson(uuid, &uuid_bson);
        }

        if(!error_code) {
            error_code = db_set_if_not_exists(collection, uuid_bson);
        }

        bson_safe_destroy(&uuid_bson);

        if (!error_code) return S_OK;
    }

    return error_code;
}

/**
 * @brief Gen uuid with block-id as 0 and check with db.
 * @details Gen uuid and check with db for uniqueness.
 *
 * @param collection Collection
 * @param uuid uuid (to-compute).
 * @return Err
 */
Err
gen_content_uuid_with_db(int collection, UUID uuid, time64_t milli_timestamp)
{
    Err error_code = S_OK;
    bson_t *uuid_bson = NULL;

    for (int i = 0; i < N_ITER_GEN_UUID_WITH_DB; i++) {
        error_code = gen_uuid(uuid, milli_timestamp);

        if(!error_code) {
            error_code = serialize_content_uuid_bson(uuid, 0, &uuid_bson);
        }

        if(!error_code) {
            error_code = db_set_if_not_exists(collection, uuid_bson);
        }

        bson_safe_destroy(&uuid_bson);

        if (!error_code) return S_OK;
    }

    return error_code;
}

/**
 * @brief Serialize uuid to bson
 * @details Serialize uuid to bson, and name the uuid as the_id.
 *
 * @param uuid uuid
 * @param db_set_bson bson (to-compute)
 * @return Err
 */
Err
serialize_uuid_bson(UUID uuid, bson_t **uuid_bson)
{
    *uuid_bson = BCON_NEW(
        MONGO_THE_ID, BCON_BINARY(uuid, UUIDLEN)
        );

    return S_OK;
}

/**
 * @brief Serialize uuid to bson
 * @details Serialize uuid to bson, and name the uuid as the_id.
 *
 * @param uuid uuid
 * @param db_set_bson bson (to-compute)
 * @return Err
 */
Err
serialize_content_uuid_bson(UUID uuid, int block_id, bson_t **uuid_bson)
{
    *uuid_bson = BCON_NEW(
        MONGO_THE_ID, BCON_BINARY(uuid, UUIDLEN),
        MONGO_BLOCK_ID, BCON_INT32(block_id)
        );

    return S_OK;
}

Err
uuid_to_milli_timestamp(UUID uuid, time64_t *milli_timestamp)
{
    //_UUID _uuid;
    time64_t *p_uuid = (time64_t *)(uuid + UUIDLEN - 8);
    //b64_pton((char *)uuid, _uuid, _UUIDLEN);

    *milli_timestamp = *p_uuid;
    *milli_timestamp >>= 16;

    return S_OK;
}

/**
 * @brief [brief description]
 * @details b64-urlsafe: ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_
 * 
 * @param uuid [description]
 * @return [description]
 */
char *
display_urlsafe_uuid(UUID uuid)
{
    char *result = malloc(DISPLAY_UUIDLEN + 1);
    result[DISPLAY_UUIDLEN] = 0;

    b64_ntop(uuid, UUIDLEN, (char *)result, DISPLAY_UUIDLEN);
    char *p_result = result;
    for(int i = 0; i < DISPLAY_UUIDLEN; i++, p_result++) {
        if(*p_result == '+') *p_result = '-';
                if(*p_result == '/') *p_result = '_';
    }

    return result;
}

char *
display_uuid(UUID uuid)
{
    char *result = malloc(DISPLAY_UUIDLEN + 1);
    result[DISPLAY_UUIDLEN] = 0;

    b64_ntop(uuid, UUIDLEN, (char *)result, DISPLAY_UUIDLEN);

    return result;
}
