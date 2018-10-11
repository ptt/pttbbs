/* $Id$ */
#ifndef PTTLIB_UTIL_H
#define PTTLIB_UTIL_H

#include "ptterr.h"

#define MAX_FILENAME_SIZE 256

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <dirent.h>    
#include <string.h>
#include <fcntl.h>    
#include <unistd.h>  
#include <stdio.h>
#include <stdlib.h>

Err safe_strcat(char **buf, int *max_buf_size, int alloc_size, int *len_buf, char *new_buf, int len_new_buf);

int Rmdir(char *dir, char *dir_prefix);

#ifdef __cplusplus
}
#endif

#endif /* PTT_QUEUE_H */

