#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "config.h"
#include "pttstruct.h"
#include "util.h"                

extern int numboards;
extern boardheader_t *bcache;
int main(int argc, char* argv[]){
struct stat  st;
boardheader_t *bptr;    
   int n;
   char pathname[1024];

   resolve_boards();
   for (n=numboards-1;n>0;n--)
    {

	bptr = &bcache[n];
	if(!strcmp(bptr->brdname,"ck53rd316"))continue;
        sprintf(pathname,"/home/bbs/boards/%s/.DIR",bptr->brdname);
        if(stat(pathname, &st) == -1)
           {
	      printf("%s is dead\n",pathname);
              sprintf (pathname,"cp -R /mnt/bbs/boards/%s /home/bbs/boards/%s",bptr->brdname,bptr->brdname);
	   }
    }
}







