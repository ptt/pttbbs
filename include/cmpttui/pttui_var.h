/* $Id$ */
#ifndef PTTUI_VAR_H
#define PTTUI_VAR_H

#include "ptterr.h"

#include "cmpttdb.h"
#include "cmpttui/pttui_state.h"
#include "cmpttui/pttui_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

extern PttUIState PTTUI_STATE;
extern PttUIState PTTUI_BUFFER_STATE;
extern FileInfo PTTUI_FILE_INFO;
extern PttUIBufferInfo PTTUI_BUFFER_INFO;
extern PttUIBuffer *PTTUI_BUFFER_TOP_LINE;

#ifdef __cplusplus
}
#endif

#endif /* PTTUI_VAR_H */
