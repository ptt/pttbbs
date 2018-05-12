/* $Id$ */
#ifndef PTTDB_FILE_PRIVATE_H
#define PTTDB_FILE_PRIVATE_H

#include "ptterr.h"
#include "cmpttdb/pttdb_const.h"
#include "cmpttlib.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>
#include "var.h"
#include "config.h"
#include "cmsys.h"
#include "pttstruct.h"
#include "proto.h"

#define MAX_FILENAME_SIZE 256

#ifndef PTTDB_FILE_PREFIX_DIR
#define PTTDB_FILE_PREFIX_DIR "storage"
#endif


#ifdef __cplusplus
}
#endif

#endif /* PTTDB_FILE_PRIVATE_H */
