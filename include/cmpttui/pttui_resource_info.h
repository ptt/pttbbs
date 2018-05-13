/* $Id$ */
#ifndef PTTUI_RESOURCE_INFO_H
#define PTTUI_RESOURCE_INFO_H

#include "ptterr.h"
#include "cmpttdb.h"
#include "cmpttlib.h"
#include "cmpttui/pttui_resource_dict.h"
#include "cmpttui/pttui_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PttUIResourceInfo {
    PttQueue queue[N_PTTDB_CONTENT_TYPE * N_PTTDB_STORAGE_TYPE];
} PttUIResourceInfo;

Err
pttui_resource_info_push_queue(PttUIBuffer *buffer, PttUIResourceInfo *resource_info, enum PttDBContentType content_type, enum PttDBStorageType storage_type);

Err destroy_pttui_resource_info(PttUIResourceInfo *resource_info);

Err pttui_resource_info_to_resource_dict(PttUIResourceInfo *resource_info, PttUIResourceDict *resource_dict);

#ifdef __cplusplus
}
#endif

#endif /* PTTUI_RESOURCE_INFO_H */
