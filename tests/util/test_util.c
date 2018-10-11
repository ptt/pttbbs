#include "test.h"

Err
_DB_FORCE_DROP_COLLECTION(int collection)
{
#ifdef MONGO_CLIENT_URL
        bool status;
    bson_error_t error;
    status = mongoc_collection_drop(MONGO_COLLECTIONS[collection], &error);

    if (!status) return S_ERR;

#endif
    return S_OK;
}

Err
form_rand_list(int n, int **p_rand_list) {
    *p_rand_list = malloc(sizeof(int) * n);
    if(!p_rand_list) return S_ERR;

    int *rand_list = *p_rand_list;

    int *available_pos = malloc(sizeof(int) * n);
    for(int i = 0; i < n; i++) {
        available_pos[i] = i;
    }

    int n_available_pos = n;
    int idx;
    int pos;
    int tmp;        
    for(int i = 0; i < n; i++, n_available_pos--) {
        idx = random() % n_available_pos;
        pos = available_pos[idx];
        rand_list[pos] = i;

        // swap with last one
        tmp = pos;
        available_pos[idx] = available_pos[n_available_pos - 1];
        available_pos[n_available_pos - 1] = tmp;
    }

    safe_free((void **)&available_pos);

    return S_OK;
}
