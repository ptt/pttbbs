/* 依據 .BOARD檔 & newsfeeds.bbs 列出參與轉信的所有板資料 */

#include "bbs.h"

#define INNDHOME  BBSHOME"/innd"

#define INND_NEWSFEED  INNDHOME "/newsfeeds.bbs"
#define INND_NODELIST  INNDHOME "/nodelist.bbs"
#define INND_BADFEED   INNDHOME "/badfeeds.bbs"	
#define INND_SCRIPT    INNDHOME "/bbsnnrpall.auto.sh"

extern boardheader_t *bcache;
extern int numboards;
int istran[MAX_BOARD];

typedef 
struct newssvr_t
{
  char name[30];
  char address[256];
  char type[10];
}newssvr_t; 

typedef
struct newsfeed_t
{
  char group[128];
  char board[15];
  char server[30];
}newsfeed_t;   

newssvr_t server[128];
newsfeed_t feedline[MAX_BOARD];
int servercount;
int feedcount;

int newsfeed_cmp(const void *a, const void *b)
{
   int i;
   i=strcasecmp(((newsfeed_t*)a)->server,((newsfeed_t*)b)->server);
   if(i) return i;
   return strcasecmp(((newsfeed_t*)a)->board,((newsfeed_t*)b)->board);
}

int get_server(char *name)
{
  int i;
  for(i=0;i<servercount;i++)
     if(!strcasecmp(server[i].name,name)) 
	{return i;}
  return -1;
}

int load_server()
{
    FILE *fp;
    char str[128];

    if (!(fp = fopen(INND_NODELIST, "r")))
    {
	return 0;
    }

    for(servercount=0; fgets(str, 128, fp); servercount++)
    {
        if(str[0]=='#') continue;
        sscanf(str,"%s %s %s",server[servercount].name,
		server[servercount].address,
		server[servercount].type);
    }
    fclose(fp);
   return servercount;
}

int load_newsfeeds()
{
    int bid;
    FILE *fp, *fo;
    char str[128];
    if (!(fp = fopen(INND_NEWSFEED, "r")))
    {
	return 0;
    }
    if (!(fo = fopen(INND_BADFEED, "w")))
    {
	return 0;
    }

    for(feedcount=0; fgets(str, 128, fp); feedcount++)
    {
        if(str[0]=='#') continue;
        sscanf(str,"%s %s %s",
		feedline[feedcount].group,feedline[feedcount].board,
		feedline[feedcount].server);
        bid=getbnum(feedline[feedcount].board);
        if(!bid) {
		fprintf(fo,"%s\n", feedline[feedcount].group );
		feedcount--;
		continue; /*移除沒有的看板i*/}

        strcpy(feedline[feedcount].board,bcache[bid-1].brdname);
        /*校正大小寫 */

        istran[bid-1]=1; 
        
    }
    fclose(fp);
    fclose(fo);
   qsort(feedline, feedcount, sizeof(newsfeed_t), newsfeed_cmp);
   return feedcount;
}
int dobbsnnrp(char *serverstr, int serverid,FILE *fpscript)
{
  char buf[256];
  printf("set %s\r\n",serverstr);
          strtok(serverstr,";\r\n");
          strtok(server[serverid].address,";\r\n"); //防hack
          sprintf(buf,INNDHOME"/bbsnnrp -c %s "
              INNDHOME"/active/%s.auto.active >>"
              INNDHOME"/log/inndBM.log 2>>"
              INNDHOME"/log/inndBM.log.err &\r\n",
              server[serverid].address,
              serverstr);
          system(buf);
          if(fpscript)
		fprintf(fpscript,INNDHOME"/bbsnnrp %s "
			INNDHOME"/active/%s.auto.active >>"
			INNDHOME"/log/inndBM.log 2>>"
			INNDHOME"/log/inndBM.log.err &\r\n",
			server[serverid].address,serverstr);
  return 0;
}
int main()
{
    int i,serverid=0;
    FILE *fp=NULL,*fpscript=fopen(INND_SCRIPT,"w");
    char buf[256],serverstr[30]="";
    resolve_boards();
    memset(istran,0,sizeof(int)*MAX_BOARD);
    load_server();
    load_newsfeeds();

    for(i=0;i<feedcount;i++)
	{
	  if(strcasecmp(serverstr,feedline[i].server))
	   {
	     if(get_server(feedline[i].server)==-1) continue;
	     if(fp) {
			fclose(fp);	
                        dobbsnnrp(serverstr,serverid,fpscript);
		    }
	     strcpy(serverstr,feedline[i].server);
	     serverid=get_server(feedline[i].server);
	     sprintf(buf,INNDHOME"/active/%s.auto.active",serverstr);
	     fp=fopen(buf,"w");
	   }
          if(fp)
             fprintf(fp,"%-35s 0000000000 0000000000 y\r\n",feedline[i].group);
	}
    if(fp) 
    {
      dobbsnnrp(serverstr,serverid,fpscript);
      fclose(fp);
    }
    if(fpscript)
     {
	fclose(fpscript);
	chmod(INND_SCRIPT,0700);
     }

    // 重設轉信與不轉信版標記
    for(i=0;i<numboards;i++)
     {
       if(bcache[i].brdname[0]=='\0' ||
	   (bcache[i].brdattr & BRD_GROUPBOARD) ) continue;
       if((bcache[i].brdattr & BRD_NOTRAN )&& istran[i])
         {
	   while(SHM->Bbusystate) {safe_sleep(1);}
  	   SHM->Bbusystate = 1;
           bcache[i].brdattr = bcache[i].brdattr & ~BRD_NOTRAN;
           strncpy(bcache[i].title + 5, "●", 2);
	   SHM->Bbusystate = 0;

           substitute_record(BBSHOME"/.BRD", &bcache[i],sizeof(boardheader_t),i+1);
	 }
       else if(!(bcache[i].brdattr & BRD_NOTRAN) && !istran[i]) 
         {
	   while(SHM->Bbusystate) {safe_sleep(1);}
           SHM->Bbusystate = 1;
           bcache[i].brdattr = bcache[i].brdattr | BRD_NOTRAN;
           strncpy(bcache[i].title + 5, "◎", 2);
           SHM->Bbusystate = 0;
           substitute_record(BBSHOME"/.BRD", &bcache[i],sizeof(boardheader_t),i+1);
	 }

     }
    return 0;
}
