#include "pttdb.h"
#include "pttdb_internal.h"

/**********
 * Milli-timestamp
 **********/

/**
 * @brief Get current time in milli-timestamp
 * @details Get current time in milli-timestamp
 *
 * @param milli_timestamp milli-timestamp (to-compute)
 * @return Err
 */
Err
get_milli_timestamp(time64_t *milli_timestamp)
{
    struct timeval tv;
    struct timezone tz;

    int ret_code = gettimeofday(&tv, &tz);
    if (ret_code) return S_ERR;

    *milli_timestamp = ((time64_t)tv.tv_sec) * 1000L + ((time64_t)tv.tv_usec) / 1000L;

    return S_OK;
}

/**********
 * UUID
 **********/

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
gen_uuid(UUID uuid)
{
    Err error_code;

    time64_t milli_timestamp;
    time64_t *p_milli_timestamp;

    long int rand_num;
    long int *p_rand;
    int n_i;
    unsigned short *p_short_rand_num;

    unsigned short *p_short;
    unsigned int *p_int;
    unsigned char *p_char;
    _UUID _uuid;

    // last 8 chars as milli-timestamp, but only the last 6 chars will be used.
    error_code = get_milli_timestamp(&milli_timestamp);
    if (error_code) return error_code;

    p_char = &milli_timestamp;
    milli_timestamp <<= 16;
    p_char = &milli_timestamp;
    p_milli_timestamp = _uuid + 40;
    *p_milli_timestamp = milli_timestamp;

    rand_num = random();
    p_short_rand_num = &rand_num;
    p_short = _uuid + 40;
    *p_short = *p_short_rand_num;

    // first 40 chars as random, but 6th char is version (6 for now)
    p_rand = _uuid;
    for (int i = 0; i < (_UUIDLEN - 8) / sizeof(long int); i++) {
        rand_num = random();
        *p_rand = rand_num;
        p_rand++;
    }

    _uuid[6] &= 0x0f;
    _uuid[6] |= 0x60;

    b64_ntop(_uuid, _UUIDLEN, (char *)uuid, UUIDLEN);

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
gen_uuid_with_db(int collection, UUID uuid)
{
    Err error_code = S_OK;
    bson_t uuid_bson;

    for (int i = 0; i < N_GEN_UUID_WITH_DB; i++) {
        error_code = gen_uuid(uuid);
        if (error_code) {
            continue;
        }

        bson_init(&uuid_bson);
        error_code = _serialize_uuid_bson(uuid, MONGO_THE_ID, &uuid_bson);
        if (error_code) {
            bson_destroy(&uuid_bson);
            continue;
        }

        error_code = db_set_if_not_exists(collection, &uuid_bson);
        bson_destroy(&uuid_bson);

        if (!error_code) {
            return S_OK;
        }
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
gen_content_uuid_with_db(int collection, UUID uuid)
{
    Err error_code = S_OK;
    bson_t uuid_bson;

    for (int i = 0; i < N_GEN_UUID_WITH_DB; i++) {
        error_code = gen_uuid(uuid);
        if (error_code) {
            continue;
        }

        bson_init(&uuid_bson);
        error_code = _serialize_content_uuid_bson(uuid, MONGO_THE_ID, 0, &uuid_bson);
        if (error_code) {
            bson_destroy(&uuid_bson);
            continue;
        }

        error_code = db_set_if_not_exists(collection, &uuid_bson);
        bson_destroy(&uuid_bson);

        if (!error_code) {
            return S_OK;
        }
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
_serialize_uuid_bson(UUID uuid, const char *key, bson_t *uuid_bson)
{
    bool bson_status = bson_append_bin(uuid_bson, key, -1, uuid, UUIDLEN);
    if (!bson_status) return S_ERR;

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
_serialize_content_uuid_bson(UUID uuid, const char *key, int block_id, bson_t *uuid_bson)
{
    bool bson_status = bson_append_bin(uuid_bson, key, -1, uuid, UUIDLEN);
    if (!bson_status) return S_ERR;

    bson_status = bson_append_int32(uuid_bson, MONGO_BLOCK_ID, -1, block_id);
    if (!bson_status) return S_ERR;

    return S_OK;
}

Err
uuid_to_milli_timestamp(UUID uuid, time64_t *milli_timestamp)
{
    _UUID _uuid;
    time64_t *p_uuid = _uuid + 40;
    b64_pton(uuid, _uuid, _UUIDLEN);

    *milli_timestamp = *p_uuid;
    *milli_timestamp >>= 16;

    return S_OK;
}

/**********
 * Get line from buf
 **********/

/**
 * @brief Try to get a line (ending with \r\n) from buffer.
 * @details [long description]
 *
 * @param p_buf Starting point of the buffer.
 * @param offset_buf Current offset of the p_buf in the whole buffer.
 * @param bytes Total bytes of the buffer.
 * @param p_line Starting point of the line.
 * @param offset_line Offset of the line.
 * @param bytes_in_new_line To be obtained bytes in new extracted line.
 * @return Error
 */
Err
get_line_from_buf(char *p_buf, int offset_buf, int bytes, char *p_line, int offset_line, int *bytes_in_new_line)
{

    // check the end of buf
    if (offset_buf >= bytes) {
        *bytes_in_new_line = 0;

        return S_ERR;
    }

    // init p_buf offset
    p_buf += offset_buf;
    p_line += offset_line;

    // check bytes in line and in buf.
    if (offset_line && p_line[-1] == '\r' && p_buf[0] == '\n') {
        *p_line = '\n';
        *bytes_in_new_line = 1;

        return S_OK;
    }

    // check \r\n in buf.
    for (int i = offset_buf; i < bytes - 1; i++) {
        if (*p_buf == '\r' && *(p_buf + 1) == '\n') {
            *p_line = '\r';
            *(p_line + 1) = '\n';
            *bytes_in_new_line = i - offset_buf + 1 + 1;

            return S_OK;
        }

        *p_line++ = *p_buf++;
    }

    // last char
    *p_line++ = *p_buf++;
    *bytes_in_new_line = bytes - offset_buf;

    // XXX special case for all block as a continuous string. Although it's not end yet, it forms a block.
    if(*bytes_in_new_line == MAX_BUF_SIZE) {
        return S_OK;
    }

    return S_ERR;
}
