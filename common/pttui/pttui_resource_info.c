#include "cmpttui/pttui_resource_info.h"

Err
pttui_resource_info_push_queue(PttUIBuffer *buffer, PttUIResourceInfo *resource_info, enum PttDBContentType content_type, enum PttDBStorageType storage_type)
{
    Err error_code = S_OK;

    int queue_idx = content_type * N_PTTDB_STORAGE_TYPE + storage_type;

    PttQueue *queue = &(resource_info->queue[queue_idx]);

    error_code = ptt_enqueue_p(buffer, queue);

    return error_code;
}

Err
destroy_pttui_resource_info(PttUIResourceInfo *resource_info)
{
    PttQueue *p_queue = resource_info->queue;
    for(int i = 0; i < N_PTTDB_CONTENT_TYPE * N_PTTDB_STORAGE_TYPE; i++, p_queue++) {
        destroy_ptt_queue(&resource_info->queue[i]);
    }

    return S_OK;
}

Err
pttui_resource_info_to_resource_dict(PttUIResourceInfo *resource_info, PttUIResourceDict *resource_dict)
{
    Err error_code = S_OK;

    PttQueue *p_queue = &resource_info->queue[PTTDB_CONTENT_TYPE_MAIN * N_PTTDB_STORAGE_TYPE + PTTDB_STORAGE_TYPE_MONGO];
    error_code = pttui_resource_dict_get_main_from_db(p_queue, resource_dict);
    if(error_code) return error_code;

    p_queue = &resource_info->queue[PTTDB_CONTENT_TYPE_MAIN * N_PTTDB_STORAGE_TYPE + PTTDB_STORAGE_TYPE_FILE];
    error_code = pttui_resource_dict_get_main_from_file(p_queue, resource_dict);
    if(error_code) return error_code;

    p_queue = &resource_info->queue[PTTDB_CONTENT_TYPE_COMMENT * N_PTTDB_STORAGE_TYPE + PTTDB_STORAGE_TYPE_MONGO];
    error_code = pttui_resource_dict_get_comment_from_db(p_queue, resource_dict);
    if(error_code) return error_code;

    p_queue = &resource_info->queue[PTTDB_CONTENT_TYPE_COMMENT * N_PTTDB_STORAGE_TYPE + PTTDB_STORAGE_TYPE_FILE];
    error_code = pttui_resource_dict_get_comment_from_file(p_queue, resource_dict);
    if(error_code) return error_code;

    p_queue = &resource_info->queue[PTTDB_CONTENT_TYPE_COMMENT_REPLY * N_PTTDB_STORAGE_TYPE + PTTDB_STORAGE_TYPE_MONGO];
    error_code = pttui_resource_dict_get_comment_reply_from_db(p_queue, resource_dict);
    if(error_code) return error_code;

    p_queue = &resource_info->queue[PTTDB_CONTENT_TYPE_COMMENT_REPLY * N_PTTDB_STORAGE_TYPE + PTTDB_STORAGE_TYPE_FILE];
    error_code = pttui_resource_dict_get_comment_reply_from_file(p_queue, resource_dict);
    if(error_code) return error_code;

    return S_OK;
}
