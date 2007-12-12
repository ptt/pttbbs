/* $Id$ */
#include "bbs.h"

/* use new pager: piaip's more. */
int more(char *fpath, int promptend)
{
    return pmore(fpath, promptend);
}

