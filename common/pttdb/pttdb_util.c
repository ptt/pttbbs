#include "pttdb_util.h"

Err
safe_free(void **a)
{
    if(!(*a)) return S_OK;
    
    free(*a);
    *a = NULL;
    return S_OK;
}
