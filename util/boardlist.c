/* 這是用來將樹狀分類輸出成 perl module (可以給像是 man/ 使用) */
#include "bbs.h"
/* 產生 hash 的內容如下:

   $db{'tobid.BRDNAME'}  把 BRDNAME(大小寫需正確) 查 bid
   $db{'parent.BID'}     從 BID 查 parent 的 bid
   $db{'tobrdname.BID'}  從 BID 查英文看板名稱
   $db{'look.SBRDNAME'}  從全部都是小寫的 SBRDNAME 查正確的看板大小寫
   $db{'BID.isboard'}    看 BID 是看板(1)或群組(0)
   $db{'BID.brdname'}    從 BID 查 brdname
   $db{'BID.title'}      查 BID 的中文板名
   $db{'BID.over18'}     是否為 18 禁
   $db{'BID.BM.0'} .. $db{'BID.BM.4'}
                         該板板主 ID
 */
int parent[MAX_BOARD];

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

	bid = bptr - bcache + 1;
	printf("$db{'tobid.%s'} = %d;\n", bptr->brdname, bid);
	printf("$db{'parent.%d'} = %d;\n", bid, parent[bid]);
	printf("$db{'tobrdname.%d'} = '%s';\n", bid, bptr->brdname);
	printf("$db{'look.%s'} = '%s';\n", smallbrdname, bptr->brdname);
	printf("$db{'%d.isboard'} = %d;\n", bid,
	       (bptr->brdattr & BRD_GROUPBOARD) ? 0 : 1);
	printf("$db{'%d.brdname'} = '%s';\n", bid, bptr->brdname);
	printf("$db{'%d.title'} = '%s';\n", bid, skipEscape(&bptr->title[7]));
	printf("$db{'%d.over18'} = '%d';\n",
	       bid, (bptr->brdattr & BRD_OVER18) ? 1 : 0);
	strlcpy(BM, bptr->BM, sizeof(BM));
	for( p = BM ; *p != 0 ; ++p )
	    if( !isalpha(*p) && !isdigit(*p) )
		*p = ' ';
	for( k = 0, p = strtok(BM, " ") ; p != NULL ; ++k, p = strtok(NULL, " ") )
	    printf("$db{'%d.BM.%d'} = '%s';\n", bid, k, p);
    }
}

void dumpclass(int gid)
{
    boardheader_t  *bptr;
    int bid;
    const int type = BRD_GROUP_LL_TYPE_NAME;
    bptr = getbcache(gid);
    if (bptr->firstchild[type] == 0 || bptr->childcount <= 0)
	resolve_board_group(gid, type);
    printf("$db{'class.%d'} = $serializer->serialize([", gid);
    for( bid = bptr->firstchild[type] ; bid > 0 ; bid = bptr->next[type] ) {
	bptr = getbcache(bid);
	if( (bptr->brdattr & (BRD_HIDE | BRD_TOP)) ||
	    (bptr->level && !(bptr->brdattr & BRD_POSTMASK) &&
	     (bptr->level & 
	      ~(PERM_BASIC|PERM_CHAT|PERM_PAGE|PERM_POST|PERM_LOGINOK))) )
	    continue;

	printf("%5d,\t", bid);
	parent[bid] = gid;
    }
    printf("]);\n");

    bptr = getbcache(gid);
    for( bid = bptr->firstchild[type] ; bid > 0 ; bid = bptr->next[type] ) {
	bptr = getbcache(bid);
	if( (bptr->brdattr & (BRD_HIDE | BRD_TOP)) ||
	    (bptr->level && !(bptr->brdattr & BRD_POSTMASK) &&
	     (bptr->level & 
	      ~(PERM_BASIC|PERM_CHAT|PERM_PAGE|PERM_POST|PERM_LOGINOK))) )
	    continue;
	if( bptr->brdattr & BRD_GROUPBOARD )
	    dumpclass(bid);
    }
}

void dumpallbrdname(void)
{
    int     i;
    boardheader_t  *bptr;
    FILE    *fp;
    
    if( !(fp = fopen("boardlist.all", "wt")) )
	return;

    for( i = 0 ; i < MAX_BOARD ; ++i ){
	bptr = &bcache[i];
	
	if( !bptr->brdname[0] ||
	    (bptr->brdattr & (BRD_HIDE | BRD_TOP | BRD_GROUPBOARD)) ||
	    (bptr->level && !(bptr->brdattr & BRD_POSTMASK) &&
	     (bptr->level & 
	      ~(PERM_BASIC|PERM_CHAT|PERM_PAGE|PERM_POST|PERM_LOGINOK))) )
	    continue;
	fprintf(fp, "%s\n", bptr->brdname);
    }
    fclose(fp);
}

int main(void)
{
    attach_SHM();

    printf("#!/usr/bin/perl\n"
           "# this is auto-generated perl module from boardlist.c\n"
           "# please do NOT modify this directly!\n"
           "# usage: make boardlist; ./boardlist | perl\n"
           "use DB_File;\n"
	   "use Data::Serializer;\n"
	   "\n"
	   "unlink 'boardlist.db', 'boardlist.list';\n"
	   "$serializer = Data::Serializer->new(serializer => 'Storable', digester => 'MD5',compress => 0,);\n"
           "tie %%db, 'DB_File', 'boardlist.db', (O_RDWR | O_CREAT), 0666, $DB_HASH;\n"
	   );
    dumpclass(1);
    dumpdetail();
    dumpallbrdname();
    printf("untie %%db;\n");
    return 0;
}
