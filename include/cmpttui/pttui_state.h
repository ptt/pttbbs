/* $Id$ */
#ifndef PTTUI_STATE_H
#define PTTUI_STATE_H

#include "ptterr.h"
#include "cmpttdb.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PttUIState {
    UUID main_id;

    enum PttDBContentType top_line_content_type;
    UUID top_line_id;
    int top_line_block_offset;
    int top_line_line_offset;
    int top_line_comment_offset;

    int n_window_line;
} PttUIState;

#ifdef __cplusplus
}
#endif

#endif /* PTTUI_STATE_H */
