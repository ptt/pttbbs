/* $Id$ */
#ifndef PTTDB_UTIL_H
#define PTTDB_UTIL_H

#include "ptterr.h"
#include "cmpttdb/pttdb_const.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

Err safe_free(void **a);

Err get_line_from_buf(char *p_buf, int offset_buf, int buf_size, char *p_line, int offset_line, int line_size, int *bytes_in_new_line);

Err pttdb_count_lines(char *content, int len, int *n_line);

#ifdef __cplusplus
}
#endif


#endif /* PTTDB_UTIL_H */
