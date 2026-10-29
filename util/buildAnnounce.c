/* 建立所有看板精華區的連結 */

#include "bbs.h"
#define GROUPROOT BBSHOME"/man/group"

extern boardheader_t *bcache;

int cmpboardclass(const void *a, const void *b)
{
    boardheader_t **brd = (boardheader_t **)a;
    boardheader_t **tmp = (boardheader_t **)b;
	int cmp = strcmp((*brd)->bclass, (*tmp)->bclass);
	if (cmp)
	    return cmp;
	return strncasecmp((*brd)->brdname, (*tmp)->brdname, IDLEN);
} 
/* Groups built or being built. The class root may be listed inside one of
 * its own groups (on Ptt, 0ClassRoot is in PttBoard), and building it again
 * there would recurse until the stack runs out. */
static unsigned char building[MAX_BOARD + 1];

void buildchilds(int level,char *path,int gid)
{
    int i,count=0;
    boardheader_t *ptr, **selected=NULL;
    fileheader_t item;
    int preserved=32;
    selected=malloc(preserved * sizeof(boardheader_t*));
    assert(selected);
    
    assert(0 < gid && gid <= MAX_BOARD);
    building[gid] = 1;

    int boardcount = num_boards();
    /* XXX It will cost O(ngroup * boardcount) totally. */
    for(i=0; i<boardcount;  i++) {
        ptr =&SHM->bcache[i];
	if(ptr->gid != gid)
	    continue;
	if((ptr->brdattr & BRD_GROUPBOARD) && building[i + 1])
	    continue;
	if((ptr->brdattr&(BRD_NOCREDIT | BRD_HIDE))!=0)
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
        printf("%*.*s+-%-14s %-5.5s %-s \n",level*2,level*2,"| | | | | | | | |",
	       ptr->brdname, ptr->bclass, ptr->desc);

        if(ptr->brdattr & BRD_GROUPBOARD){
	    // The paths below are made of the group names: check them here.
	    if (invalid_pname(ptr->brdname)) {
		printf("something wrong: %s, %s\n", path, ptr->brdname);
	    } else {
		AUTO_STR(newpath, "%s/%s", path, ptr->brdname);
		Mkdir(newpath);
		buildchilds(level+1, newpath, ptr-bcache+1);
	    }
	}
	else{
	    // Check the name only: invalid_pname() refuses the absolute path,
	    // which is GROUPROOT and the group names checked above.
	    if(!invalid_pname(ptr->brdname) && isalpha(ptr->brdname[0])) {
		char apath[PATHLEN];
		setapath(apath, ptr->brdname);
		AUTO_STR(target, BBSHOME "/%s", apath);
		AUTO_STR(linkpath, "%s/%s", path, ptr->brdname);
		if (symlink(target, linkpath) < 0)
		    fprintf(stderr, "%s: %s\n", linkpath, strerror(errno));
	    } else {
		printf("something wrong: %s, %s\n",path, ptr->brdname);
	    }
	}
        memset(&item, 0, sizeof(item));
        if (ptr->moderators[0].unum > 0) {
            const char *mid = getuserid(ptr->moderators[0].unum);
            STRLCPY(item.owner, mid ? mid : "");
        }
        SNPRINTF(item.title, "%-13.13s %-32.32s", level?ptr->brdname:"", ptr->desc);
        item.filemode =  0 ;
	STRLCPY(item.filename, ptr->brdname);
        AUTO_STR(dirpath, "%s/.DIR", path);
	append_fileheader(dirpath, &item);
    }
    free(selected);
}


int main(void)
{ 
    char path[512];
    setsid();
    STRLCPY(path, GROUPROOT);
    RmTree(GROUPROOT);
    Mkdir(GROUPROOT);
    attach_SHM();
    resolve_boards();
    buildchilds(0,path,1);
    return 0;
}
