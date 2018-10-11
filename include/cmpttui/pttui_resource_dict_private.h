/* $Id$ */
#ifndef PTTUI_RESOURCE_DICT_PRIVATE_H
#define PTTUI_RESOURCE_DICT_PRIVATE_H

#include "ptterr.h"
#include "cmpttlib.h"
#include "cmpttui/pttui_buffer.h"
#include "cmpttui/pttui_const.h"
#include "cmpttui/pttui_resource_dict.h"
#include "cmpttui/pttui_util.h"
#include "cmpttdb.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>
#include "proto.h"

Err _pttui_resource_dict_get_content_block_from_db_core(UUID uuid, int min_block_id, int max_block_id, enum MongoDBId mongo_db_id, enum PttDBContentType content_type, PttUIResourceDict *resource_dict);

Err _pttui_resource_dict_get_content_block_from_db_core2(bson_t *q, int max_n_content_block, enum MongoDBId mongo_db_id, enum PttDBContentType content_type, PttUIResourceDict *resource_dict);

Err _pttui_resource_dict_content_block_db_to_dict(bson_t **b_content_blocks, int n_content_block, enum PttDBContentType content_type, PttUIResourceDict *resource_dict);

Err _pttui_resource_dict_comment_db_to_dict(bson_t **b_comments, int n_comment, PttUIResourceDict *resource_dict);

Err _pttui_resource_dict_add_data(UUID the_id, int block_id, int file_id, int len, char *buf, enum PttDBContentType content_type, PttUIResourceDict *resource_dict);

Err _pttui_resource_dict_get_content_block_from_file_core(PttUIBuffer *buffer, PttUIResourceDict *resource_dict);

Err _pttui_resource_dict_save_to_tmp_file(_PttUIResourceDictLinkList *dict_link_list, UUID main_id);

Err _pttui_resource_dict_integrate_with_modified_pttui_buffer_info_dict_last_buf(_PttUIResourceDictLinkList *the_dict, char *p_dict_buf, int dict_buf_offset, int len_dict_buf, int line_offset_dict_buf, char *tmp_buf, int max_buf_size, int len_tmp_buf, int line_offset_tmp_buf, FileInfo *file_info);


#ifdef __cplusplus
}
#endif

#endif /* PTTUI_RESOURCE_DICT_PRIVATE_H */

