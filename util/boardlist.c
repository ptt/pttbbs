/* $Id$ */
/* 這是用來將樹狀分類輸出成 perl module (可以給像是 man/ 使用) */
#include "bbs.h"

static void
load_uidofgid(const int gid, const int type)
{
    boardheader_t  *bptr, *currbptr;
    int             n, childcount = 0;
    currbptr = &bcache[gid - 1];
    for (n = 0; n < numboards; ++n) {
	bptr = SHM->bsorted[type][n];
	if (bptr->brdname[0] == '\0')
	    continue;
	if (bptr->gid == gid) {
	    if (currbptr == &bcache[gid - 1])
		currbptr->firstchild[type] = bptr;
	    else {
		currbptr->next[type] = bptr;
		currbptr->parent = &bcache[gid - 1];
	    }
	    childcount++;
	    currbptr = bptr;
	}
    }
    bcache[gid - 1].childcount = childcount;
    if (currbptr == &bcache[gid - 1])
	currbptr->firstchild[type] = NULL;
    else
	currbptr->next[type] = NULL;
}

char *skipEscape(char *s)
{
    static  char    buf[TTLEN * 2 + 1];
    int     r, w;
    for( w = r = 0 ; s[r] != 0 ; ++r ){
	if( s[r] == '\'' || s[r] == '\\' )
	    buf[w++] = '\\';
	buf[w++] = s[r];
    }
    buf[w++] = 0;
    return buf;
}

void dumpdetail(void)
{
    int     i, k, bid;
    boardheader_t  *bptr;
    char    BM[IDLEN * 3 + 3], *p;
    char    smallbrdname[IDLEN + 1];
    for( i = 0 ; i < MAX_BOARD ; ++i ){
	bptr = &bcache[i];
	
	if( !bptr->brdname[0] ||
	    (bptr->brdattr & (BRD_HIDE | BRD_TOP)) ||
	    (bptr->level && !(bptr->brdattr & BRD_POSTMASK) &&
	     (bptr->level & 
	      ~(PERM_BASIC|PERM_CHAT|PERM_PAGE|PERM_POST|PERM_LOGINOK))) )
	    continue;

	for( k = 0 ; bptr->brdname[k] ; ++k )
	    smallbrdname[k] = (isupper(bptr->brdname[k]) ? 
			       tolower(bptr->brdname[k]) :
			       bptr->brdname[k]);
	smallbrdname[k] = 0;

	bid = bptr - bcache;
	printf("$db{'tobid.%s'} = %d;\n", bptr->brdname, bid);
	printf("$db{'tobrdname.%d'} = '%s';\n", bid, bptr->brdname);
	printf("$db{'look.%s'} = '%s';\n", smallbrdname, bptr->brdname);
	printf("$db{'%d.isboard'} = %d;\n", bid,
	       (bptr->brdattr & BRD_GROUPBOARD) ? 0 : 1);
	printf("$db{'%d.brdname'} = '%s';\n", bid, bptr->brdname);
	printf("$db{'%d.title'} = '%s';\n", bid, skipEscape(&bptr->title[7]));
	strlcpy(BM, bptr->BM, sizeof(BM));
	for( p = BM ; *p != 0 ; ++p )
	    if( !isalpha(*p) && !isdigit(*p) )
		*p = ' ';
	for( k = 0, p = strtok(BM, " ") ; p != NULL ; ++k, p = strtok(NULL, " ") )
	    printf("$db{'%d.BM.%d'} = '%s';\n", bid, k, p);
    }
}

void dumpclass(int bid)
{
    boardheader_t  *bptr;
    bptr = &bcache[bid];
    if (bptr->firstchild[0] == NULL || bptr->childcount <= 0)
	load_uidofgid(bid + 1, 0); /* 因為這邊 bid從 0開始, 所以再 +1 回來 */
    printf("$db{'class.%d'} = $serializer->serialize([", bid);
    for (bptr = bptr->firstchild[0]; bptr != NULL ; bptr = bptr->next[0]) {
	if( (bptr->brdattr & (BRD_HIDE | BRD_TOP)) ||
	    (bptr->level && !(bptr->brdattr & BRD_POSTMASK) &&
	     (bptr->level & 
	      ~(PERM_BASIC|PERM_CHAT|PERM_PAGE|PERM_POST|PERM_LOGINOK))) )
	    continue;

	printf("%5d,\t", bptr - bcache);
    }
    printf("]);\n");

    bptr = &bcache[bid];
    for (bptr = bptr->firstchild[0]; bptr != NULL ; bptr = bptr->next[0]) {
	if( (bptr->brdattr & (BRD_HIDE | BRD_TOP)) ||
	    (bptr->level && !(bptr->brdattr & BRD_POSTMASK) &&
	     (bptr->level & 
	      ~(PERM_BASIC|PERM_CHAT|PERM_PAGE|PERM_POST|PERM_LOGINOK))) )
	    continue;

	if( bptr->brdattr & BRD_GROUPBOARD )
	    dumpclass(bptr - bcache);
    }
}

int main(int argc, char **argv)
{
    attach_SHM();

    printf("#!/usr/bin/perl\n"
           "# this is auto-generated perl module from boardlist.c\n"
           "# please do NOT modify this directly!\n"
           "# usage: make boardlist; ./boardlist | perl\n"
           "# Id of boardlist.c: $Id$\n"
           "use DB_File;\n"
	   "use Data::Serializer;\n"
	   "\n"
	   "$serializer = Data::Serializer->new(serializer => 'Storable', digester => 'MD5',compress => 0,);\n"
           "tie %%db, 'DB_File', 'boardlist.db', (O_RDWR | O_CREAT), 0666, $DB_HASH;\n"
	   );
    dumpclass(0);
    dumpdetail();
    printf("untie %%db;\n");
    return 0;
}
