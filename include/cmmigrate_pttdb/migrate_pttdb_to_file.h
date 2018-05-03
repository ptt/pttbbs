/* $Id$ */
#ifndef MIGRATE_PTTDB_TO_FILE_H
#define MIGRATE_PTTDB_TO_FILE_H

#include "ptterr.h"
#include "cmpttdb.h"

#ifdef __cplusplus
extern "C" {
#endif

Err migrate_pttdb_to_file(UUID main_id, const char *fpath);

#ifdef __cplusplus
}
#endif

#endif /* MIGRATE_PTTDB_TO_FILE_H */