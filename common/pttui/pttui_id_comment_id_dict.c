#include "cmpttui/pttui_id_comment_id_dict.h"

Err
safe_destroy_pttui_id_comment_id_dict(PttUIIdCommentIdDict *id_comment_id_dict)
{
    _PttUIIdCommentIdDict *p = NULL;
    _PttUIIdCommentIdDict *tmp = NULL;

    for(int i = 0; i < N_PTTUI_ID_COMMENT_ID_DICT_LINK_LIST; i++) {
        p = id_comment_id_dict->data[i];

        while(p) {
            tmp = p->next;
            free(p);
            p = tmp;
        }

        id_comment_id_dict->data[i] = NULL;
    }

    return S_OK;
}

Err
pttui_id_comment_id_dict_add_data(UUID the_id, int comment_id, PttUIIdCommentIdDict *id_comment_id_dict)
{
    int the_idx = ((int)the_id[0]) % N_PTTUI_ID_COMMENT_ID_DICT_LINK_LIST;
    _PttUIIdCommentIdDict *p = id_comment_id_dict->data[the_idx];

    if(!p) {
        id_comment_id_dict->data[the_idx] = malloc(sizeof(_PttUIIdCommentIdDict));
        p = id_comment_id_dict->data[the_idx];
    }
    else {
        for(; p->next; p = p->next) {
            if(!memcmp(p->the_id, the_id, UUIDLEN)) return S_OK;
        }
        if(!p->next) {
            p->next = malloc(sizeof(_PttUIIdCommentIdDict));
            p = p->next;
        }
    }

    p->next = NULL;
    memcpy(p->the_id, the_id, UUIDLEN);
    p->comment_id = comment_id;

    return S_OK;
}

Err
pttui_id_comment_id_dict_get_comment_id(PttUIIdCommentIdDict *id_comment_id_dict, UUID the_id, int *comment_id)
{
    int the_idx = ((int)the_id[0]) % N_PTTUI_ID_COMMENT_ID_DICT_LINK_LIST;
    _PttUIIdCommentIdDict *p = id_comment_id_dict->data[the_idx];
    for(; p; p = p->next) {
        if(!memcmp(the_id, p->the_id, UUIDLEN)) {
            *comment_id = p->comment_id;
            break;
        }
    }

    if(!p) {
        *comment_id = 0;
        return S_ERR_NOT_EXISTS;
    }

    return S_OK;

}
