#include "test_util_db.h"

Err
_DB_FORCE_DROP_COLLECTION(int collection)
{
    bool status;
    bson_error_t error;
    status = mongoc_collection_drop(MONGO_COLLECTIONS[collection], &error);

    if (!status) return S_ERR;

    return S_OK;
}
