/* $Id$ */
#ifndef PTTDB_DICT_BSON_BY_UU_H
#define PTTDB_DICT_BSON_BY_UU_H

#include "ptterr.h"
#include "pttdb_const.h"
#include "util_db.h"
#include "pttdb_uuid.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef struct _DictBsonByUU {
    UUID uuid;
    bson_t *b;
    struct _DictBsonByUU *next;
} _DictBsonByUU;

typedef struct DictBsonByUU {
    int n_dict;
    _DictBsonByUU **dicts;
} DictBsonByUU;

#define MAX_N_DICT_BSON_BY_UU 256

/**********
 * DictBSonByUU
 **********/
Err init_dict_bson_by_uu(DictBsonByUU *dict_bson_by_uu, int n_dict_bson_by_uu);
Err add_to_dict_bson_by_uu(UUID uuid, bson_t *b, DictBsonByUU *dict_bson_by_uu);
Err get_bson_from_dict_bson_by_uu(DictBsonByUU *dict_bson_by_uu, UUID uuid, bson_t **b);
Err bsons_to_dict_bson_by_uu(bson_t **b, int n_b, char *key, DictBsonByUU *dict_bson_by_uu);
Err safe_destroy_dict_bson_by_uu(DictBsonByUU *dict_bson_by_uu);

Err display_dict_bson_by_uu(DictBsonByUU *dict_bson_by_uu, char *prompt);

#ifdef __cplusplus
}
#endif

#endif /* PTTDB_DICT_BSON_BY_UU_H */
