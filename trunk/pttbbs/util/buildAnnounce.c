/* 建立所有看板精華區的連結 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "config.h"
#include "pttstruct.h"
#define GROUPROOT BBSHOME"/man/group"

extern bcache_t *brdshm;   
extern boardheader_t *bcache;
extern void resolve_boards();

void buildchilds(int level,char *path,boardheader_t *bptr)
{
    char newpath[512];
    boardheader_t *ptr;
    fileheader_t item;
    
    if(bptr->firstchild[1]==(boardheader_t*)~0 || bptr->firstchild[1]==NULL)
	return;
    for(ptr =(void*) bptr->firstchild[1];
	ptr!=(boardheader_t*)~0 ;ptr=ptr->next[1]){
	
	if(
	   (ptr->brdattr&(BRD_BAD | BRD_GROUPBOARD | BRD_NOCOUNT | BRD_HIDE))!=0
	   ||
	   (ptr->level && !(ptr->brdattr & BRD_POSTMASK)))
	    continue;
        printf("%*.*s+-%-14s %-s \n",level*2,level*2,"| | | | | | | | |",
	       ptr->brdname, ptr->title);
        if(ptr->brdattr & BRD_GROUPBOARD){
	    sprintf(newpath,"%s/%s",path,ptr->brdname);
	    mkdir(newpath,0766);
	    buildchilds(level+1,newpath,ptr); 
	}
	else{
	    printf("%s4\n",ptr->brdname);
	    sprintf(newpath,"/bin/ln -s "BBSHOME"/man/boards/%c/%s %s/%s",
		    /* maybe something wrong with this             ^^^^^ */
		    ptr->brdname[0], ptr->brdname,path,ptr->brdname);
	    system(newpath);
	}
	printf("%s5\n",ptr->brdname);
        sprintf(newpath,"%s/.DIR",path);
        strcpy(item.owner,ptr->BM);
	strtok(item.owner,"/");
        strcpy(item.title,ptr->title+7);
	item.savemode = 'D';
        sprintf(item.filename,ptr->brdname);
	append_record(newpath, &item, sizeof(item));
    }       
}


int main()
{ 
    char path[512];
    setsid();
    strcpy(path,GROUPROOT);
    system("rm -rf "GROUPROOT);
    mkdir(GROUPROOT,0766);
    resolve_boards();
    buildchilds(0,path,&bcache[0]);
    return 0;
}
