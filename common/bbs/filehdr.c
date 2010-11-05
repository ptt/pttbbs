#include <string.h>
#include "cmsys.h"
#include "cmbbs.h"

static int
_is_same_fhdr_filename(const void *ptr1, const void *ptr2) {
    return strcmp(((const fileheader_t*)ptr1)->filename,
                  ((const fileheader_t*)ptr2)->filename) == 0;
}

int
substitute_fileheader(const char *dir_path,
                       const void *srcptr, const void *destptr, int id)
{
    return substitute_record2(dir_path, srcptr, destptr, sizeof(fileheader_t),
                              id, _is_same_fhdr_filename);
}

int
delete_fileheader(const char *dir_path, const void *rptr, int id)
{
    return delete_record2(dir_path, rptr, sizeof(fileheader_t),
                          id, _is_same_fhdr_filename);
}
