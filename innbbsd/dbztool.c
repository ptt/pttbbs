#include <sys/file.h>
#include "his.h"

#define DEBUG 1
#undef DEBUG

static datum content, inputkey, inputvalue;
static char dboutput[1025];
static char dbinput[1025];
static char valueinput[100];

enum {SUBJECT, FROM, NAME};
char *DBfetch(key)
char *key;
{
   int i;
   char *tail, *ptr;
   if (key == NULL) return NULL;
   sprintf(dbinput,"%.510s",key);
   inputkey.dptr = dbinput;
   inputkey.dsize = strlen(dbinput);
   content.dptr = dboutput;
   ptr = (char*)HISfilesfor(&inputkey,&content);
   if (ptr == NULL) {
       return NULL;
   }
   return ptr;
}

DBstore(key,paths)
char *key;
char *paths;
{
   int i;
   char *tail;
   time_t now;
   time(&now);
   if (key == NULL) return -1;
   sprintf(dbinput,"%.510s",key);
   inputkey.dptr = dbinput;
   inputkey.dsize = strlen(dbinput);
   if (HISwrite(&inputkey, now, paths ) == FALSE) {
      return -1;
   } else {
      return 0;
   }
}

int storeDB(mid,paths)
char *mid;
char *paths;
{
   char *key,*ptr;
   int rel;
   ptr = DBfetch(mid); 
   if (ptr != NULL) {
     return 0;
   } else {
     return DBstore(mid , paths);
   }
}

my_mkdir (idir,mode)
char *idir;
int mode;
{
	char buffer[LEN];
	char *ptr, *dir = buffer;
	struct stat st;
	strncpy(dir, idir, LEN - 1);
	for (;dir!=NULL && *dir;) {
		ptr = (char*)strchr(dir,'/');
		if (ptr != NULL) {
		    *ptr = '\0';
		}
	        if (stat(dir,&st) != 0) {
		    if (mkdir(dir,mode) != 0 ) 
			 return -1;
		}
		chdir(dir);
		if (ptr != NULL)
		  dir = ptr +1;
		else
		  dir = ptr;
	}
	return 0;
}

