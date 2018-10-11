/* $Id$ */
#ifndef PTTDB_DICT_IDX_BY_UU_H
#define PTTDB_DICT_IDX_BY_UU_H

#include "ptterr.h"
#include "cmpttdb/pttdb_const.h"
#include "cmpttdb/util_db.h"
#include "cmpttdb/pttdb_uuid.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef struct _DictIdxByUU {
    UUID uuid;
    int idx;
    struct _DictIdxByUU *next;
} _DictIdxByUU;

typedef struct DictIdxByUU {
    int n_dict;
    _DictIdxByUU **dicts;
} DictIdxByUU;

#define MAX_N_dict_idx_BY_UU 256

/**********
 * DictIdxByUU
 **********/
Err init_dict_idx_by_uu(DictIdxByUU *dict_idx_by_uu, int n_dict_idx_by_uu);
Err add_to_dict_idx_by_uu(UUID uuid, int the_idx, DictIdxByUU *dict_idx_by_uu);
Err get_idx_from_dict_idx_by_uu(DictIdxByUU *dict_idx_by_uu, UUID uuid, int *the_idx);
Err bsons_to_dict_idx_by_uu(bson_t **b, int n_b, char *key, DictIdxByUU *dict_idx_by_uu);
Err safe_destroy_dict_idx_by_uu(DictIdxByUU *dict_idx_by_uu);

#ifdef __cplusplus
}
#endif

#endif /* PTTDB_DICT_IDX_BY_UU_H */
