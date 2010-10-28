/* 建立所有看板精華區的連結 */

#include "bbs.h"
#define GROUPROOT BBSHOME"/man/group"

extern boardheader_t *bcache;
extern int numboards;

int cmpboardclass(const void *a, const void *b)
{
    boardheader_t **brd = (boardheader_t **)a;
    boardheader_t **tmp = (boardheader_t **)b;
    return (strncmp((*brd)->title, (*tmp)->title, 4)<<8)+
	   strncasecmp((*brd)->brdname, (*tmp)->brdname, IDLEN);
} 
void buildchilds(int level,char *path,int gid)
{
    char newpath[512];
    int i,count=0;
    boardheader_t *ptr, **selected=NULL;
    fileheader_t item;
    char *p;
    int preserved=32;
    selected=malloc(preserved * sizeof(boardheader_t*));
    
    /* XXX It will cost O(ngroup * numboards) totally. */
    for(i=0; i<numboards;  i++) {
        ptr =&SHM->bcache[i];
	if(ptr->gid != gid)
	    continue;
	if((ptr->brdattr&(BRD_BAD | BRD_HIDE))!=0)
	    continue;
	if((ptr->level && !(ptr->brdattr & BRD_POSTMASK)))
	    continue;
	if(count==preserved) {
	    preserved*=2;
	    selected=(boardheader_t**)realloc(selected, preserved*sizeof(boardheader_t*));
	}
	assert(selected);
        selected[count++]=ptr;
    }       
    qsort(selected, count, sizeof(boardheader_t *),
	  cmpboardclass); 
    for(i=0;i<count;i++)
    {
        ptr=selected[i];
        printf("%*.*s+-%-14s %-s \n",level*2,level*2,"| | | | | | | | |",
	       ptr->brdname, ptr->title);

        if(ptr->brdattr & BRD_GROUPBOARD){
	    snprintf(newpath,sizeof(newpath),"%s/%s",path,ptr->brdname);
	    mkdir(newpath,0755);
	    buildchilds(level+1,newpath,ptr-bcache+1); 
	}
	else{
	    if(!invalid_pname(path) && !invalid_pname(ptr->brdname) &&
		    isalpha(ptr->brdname[0])) {
		sprintf(newpath,"/bin/ln -s "BBSHOME"/man/boards/%c/%s %s/%s",
			ptr->brdname[0], ptr->brdname,path,ptr->brdname);
		system(newpath);
	    } else {
		printf("something wrong: %s, %s\n",path, ptr->brdname);
	    }
	}
        strlcpy(item.owner,ptr->BM, sizeof(item.owner));
	if((p=strchr(item.owner,'/'))!=NULL)
	    *p='\0';
        snprintf(item.title,sizeof(item.title),"%-13.13s %-32.32s", level?ptr->brdname:"", ptr->title+7);
        item.filemode =  0 ;
	strlcpy(item.filename,ptr->brdname,sizeof(item.filename));
        snprintf(newpath,sizeof(newpath),"%s/.DIR",path);
	append_record(newpath, &item, sizeof(item));
    }
    free(selected);
}


int main(int argc, char **argv)
{ 
    char path[512];
    setsid();
    strcpy(path,GROUPROOT);
    system("rm -rf "GROUPROOT);
    mkdir(GROUPROOT,0755);
    attach_SHM();
    resolve_boards();
    buildchilds(0,path,1);
    return 0;
}
