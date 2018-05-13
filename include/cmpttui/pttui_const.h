/* $Id$ */
#ifndef PTTUI_CONST_H
#define PTTUI_CONST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "config.h"

extern const char * const BIG5[13];
extern const char * const BIG5_mode[13];

extern const char *table[8];
extern const char *table_mode[6];

#define MAX_TEXTLINE_SIZE WRAPMARGIN
#define PTTUI_EDIT_TMP_DIR "pttui_edit/M"
#define PTTUI_NEWLINE "\n"
#define LEN_PTTUI_NEWLINE 1

#ifdef __cplusplus
}
#endif

#endif /* PTT_CONST_H */
