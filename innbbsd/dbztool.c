#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/file.h>
#include <time.h>
#include "his.h"

static datum    content, inputkey;
static char     dboutput[1025];
static char     dbinput[1025];

char           *
DBfetch(key)
    char           *key;
{
    char           *ptr;
    if (key == NULL)
	return NULL;
    sprintf(dbinput, "%.510s", key);
    inputkey.dptr = dbinput;
    inputkey.dsize = strlen(dbinput);
    content.dptr = dboutput;
    ptr = (char *)HISfilesfor(&inputkey, &content);
    if (ptr == NULL) {
	return NULL;
    }
    return ptr;
}

int
DBstore(key, paths)
    char           *key;
    char           *paths;
{
    time_t          now;
    time(&now);
    if (key == NULL)
	return -1;
    sprintf(dbinput, "%.510s", key);
    inputkey.dptr = dbinput;
    inputkey.dsize = strlen(dbinput);
    if (HISwrite(&inputkey, now, paths) == FALSE) {
	return -1;
    } else {
	return 0;
    }
}

int 
storeDB(mid, paths)
    char           *mid;
    char           *paths;
{
    char           *ptr;
    ptr = DBfetch(mid);
    if (ptr != NULL) {
	return 0;
    } else {
	return DBstore(mid, paths);
    }
}
