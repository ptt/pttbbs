/* $Id$ */
#ifndef PTTDB_DICT_IDX_BY_UU_PRIVATE_H
#define PTTDB_DICT_IDX_BY_UU_PRIVATE_H

#include "ptterr.h"
#include "cmpttdb/pttdb_const.h"
#include "cmpttdb/pttdb_dict_idx_by_uu.h"

#ifdef __cplusplus
extern "C" {
#endif

Err _safe_destroy_dict_idx_by_uu_core(_DictIdxByUU *dict_idx_by_uu);

#ifdef __cplusplus
}
#endif

#endif /* PTTDB_DICT_IDX_BY_UU_PRIVATE_H */
