#include "cmbbs.h"

int
belong(const char *filelist, const char *key)
{
    return file_exist_record(filelist, key);
}

