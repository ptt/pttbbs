/* $Id$ */

// #include "bbs.h"
#include "cmbbs.h"
#include <assert.h>
// #include <stdio.h>
// #include <stdlib.h>
#include <string.h>
#include <ctype.h>

int 
is_validuserid(const char *id)
{
    int len, i;
    if(id==NULL)
	return 0;
    len = strlen(id);

    if (len < 2 || len>IDLEN)
	return 0;

    if (!isalpha(id[0]))
	return 0;
    for (i = 1; i < len; i++)
	if (!isalnum(id[i]))
	    return 0;
    return 1;
}

