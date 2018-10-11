/* $Id$ */
#ifndef PTTUI_ID_COMMENT_ID_DICT_H
#define PTTUI_ID_COMMENT_ID_DICT_H

#include "ptterr.h"
#include "cmpttdb.h"
#include <string.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define N_PTTUI_ID_COMMENT_ID_DICT_LINK_LIST 256

typedef struct _PttUIIdCommentIdDict {
    struct _PttUIIdCommentIdDict *next;
    UUID the_id;
    int comment_id;
} _PttUIIdCommentIdDict;

typedef struct PttUIIdCommentIdDict {
    _PttUIIdCommentIdDict *data[N_PTTUI_ID_COMMENT_ID_DICT_LINK_LIST];
} PttUIIdCommentIdDict;

#ifdef __cplusplus
}
#endif

Err safe_destroy_pttui_id_comment_id_dict(PttUIIdCommentIdDict *id_comment_id_dict);

Err pttui_id_comment_id_dict_add_data(UUID the_id, int comment_id, PttUIIdCommentIdDict *id_comment_id_dict);

Err pttui_id_comment_id_dict_get_comment_id(PttUIIdCommentIdDict *id_comment_id_dict, UUID the_id, int *comment_id);


#endif /* PTTUI_ID_COMMENT_ID_DICT_H */
