/* $Id$ */
#ifndef PTTUI_RESOURCE_DICT_H
#define PTTUI_RESOURCE_DICT_H

#include "ptterr.h"
#include "cmpttlib.h"
#include "cmpttdb.h"
#include "cmpttui/pttui_buffer.h"
#include "cmpttui/pttui_id_comment_id_dict.h"

#ifdef __cplusplus
extern "C" {
#endif

#define N_PTTUI_RESOURCE_DICT_LINK_LIST 256

typedef struct _PttUIResourceDictLinkList {
    struct _PttUIResourceDictLinkList *next;
    enum PttDBContentType content_type;
    UUID the_id;
    int comment_id;
    int block_id;
    int file_id;
    int len;
    char *buf;
} _PttUIResourceDictLinkList;

typedef struct PttUIResourceDict {
    _PttUIResourceDictLinkList *data[N_PTTUI_RESOURCE_DICT_LINK_LIST];
    PttUIIdCommentIdDict id_comment_id_dict;
    UUID main_id;
} PttUIResourceDict;

Err init_pttui_resource_dict(UUID main_id, PttUIResourceDict *resource_dict);

Err safe_destroy_pttui_resource_dict(PttUIResourceDict *resource_dict);

Err pttui_resource_dict_get_data(PttUIResourceDict *resource_dict, UUID the_id, int block_id, int file_id, int *len, char **buf);

Err pttui_resource_dict_get_link_list(PttUIResourceDict *resource_dict, UUID the_id, int block_id, int file_id, _PttUIResourceDictLinkList **dict_link_list);

Err pttui_resource_dict_get_main_from_db(PttQueue *queue, PttUIResourceDict *resource_dict);

Err pttui_resource_dict_get_comment_from_db(PttQueue *queue, PttUIResourceDict *resource_dict);

Err pttui_resource_dict_get_comment_reply_from_db(PttQueue *queue, PttUIResourceDict *resource_dict);

Err pttui_resource_dict_get_main_from_file(PttQueue *queue, PttUIResourceDict *resource_dict);

Err pttui_resource_dict_get_comment_from_file(PttQueue *queue, PttUIResourceDict *resource_dict);

Err pttui_resource_dict_get_comment_reply_from_file(PttQueue *queue, PttUIResourceDict *resource_dict);

Err pttui_resource_dict_get_next_buf(char *p_buf, int buf_offset, int len, char **p_next_buf, int *buf_next_offset);

Err pttui_resource_dict_integrate_with_modified_pttui_buffer_info(PttUIBuffer *head, PttUIBuffer *tail, PttUIResourceDict *resource_dict, FileInfo *file_info);

Err pttui_resource_dict_save_to_tmp_file(PttUIResourceDict *resource_dict);

Err log_pttui_resource_dict(PttUIResourceDict *resource_dict, char *prompt);

Err pttui_resource_dict_reset_file_info(PttUIResourceDict *resource_dict, FileInfo *file_info);

#ifdef __cplusplus
}
#endif

#endif /* PTTUI_RESOURCE_DICT_H */

