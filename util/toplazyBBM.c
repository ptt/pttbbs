/* $Id$ */
#define _UTIL_C_
#include "bbs.h"

#define OUTFILE  BBSHOME "/etc/toplazyBBM"
#define FIREFILE BBSHOME "/etc/topfireBBM"

extern boardheader_t *bcache;
extern int numboards;

boardheader_t allbrd[MAX_BOARD];
typedef struct lostbm {
	char *bmname;
	char *title;
	char *ctitle;
	int lostdays;
} lostbm;
lostbm lostbms[MAX_BOARD];

typedef struct BMarray{
	char *bmname;
	int  flag;
}  BMArray;
BMArray bms[3];

int bmlostdays_cmp(const void *va, const void *vb)
{
	lostbm *a=(lostbm *)va, *b=(lostbm *)vb;
	if (a->lostdays > b->lostdays) return -1;
	else if (a->lostdays == b->lostdays) return 0;
	else return 1;
}


int LINK(char* src, char* dst){
	char cmd[200];
   if(symlink(src,dst) == -1)	
     {	
	sprintf(cmd, "/bin/cp -R %s %s", src, dst);
	return system(cmd);
     }
   return 0;
}

int main(int argc, char *argv[])
{
    int bmid, i, j=0;
    FILE *inf, *firef;

    attach_SHM();
    resolve_boards();

    if(passwd_init())
        exit(1);      

   memcpy(allbrd,bcache,numboards*sizeof(boardheader_t));

	/* write out the target file */
	printf("Starting Checking\n");
	inf   = fopen(OUTFILE, "w+");
	if(inf == NULL){
		printf("open file error : %s\n", OUTFILE);
		exit(1);
	}	
	firef = fopen(FIREFILE, "w+");
	if(firef == NULL){
		printf("open file error : %s\n", FIREFILE);
		exit(1);
	}
	
	fprintf(inf, "警告: 板主若於兩個月未上站,將予於免職\n");
	fprintf(inf,
        "看板名稱                                      "
	"    板主        幾天沒來啦\n"
        "---------------------------------------------------"
	"-------------------\n");

        fprintf(firef, "免職板主\n");
        fprintf(firef,
        "看板名稱                                      "
        "    板主        幾天沒來啦\n"
        "---------------------------------------------------"
        "-------------------\n"); 


	j = 0 ;
        for (i = 0; i < numboards; i++) {
	 char *p, bmbuf[IDLEN * 3 + 3];
	 int   index = 0, flag = 0, k, n;
	 userec_t xuser;
	 p=strtok(allbrd[i].BM,"/ ");

	 if(p)
		do
		{
		  if(allbrd[i].brdname[0] == '\0' || (allbrd[i].brdattr & BRD_GROUPBOARD) ==0 ) continue;
	          if (*p  == '[' ){p[strlen(p)-1]='\0'; p++;}
  		  bmid=getuser(p, &xuser);
  		  bms[index].bmname = p;
  		  bms[index].flag = 0;
		  if (((((int)time(NULL)-(int)xuser.lastlogin)/(60*60*24))>=7)
                 //&& isalpha(allbrd[i].brdname[0])
		 //&& isalpha(allbrd[i].BM[0])
                 && !(xuser.userlevel & PERM_SYSOPHIDE)
		 && !(xuser.userlevel & PERM_SYSOP))
		   {
			lostbms[j].bmname = p;
			lostbms[j].title = allbrd[i].brdname;
			lostbms[j].ctitle = allbrd[i].title;
			lostbms[j].lostdays =
			     ((int)time(NULL)-(int)xuser.lastlogin)/(60*60*24);
			
			printf("%s\n", lostbms[j].title);
			//超過六十天 免職
			if(lostbms[j].lostdays > 30){
				xuser.userlevel &= ~PERM_BM;
				bms[index].flag = 1;
				flag = 1;
			}
			j++;
		   }
		   index++;		  
		} while((p=strtok(NULL,"/ "))!=NULL);
	
		//從板主名單拿掉名字
		
		if(flag == 1){
			bmbuf[0] = '\0';
			for(k = 0 , n = 0; k < index; k++){
				if(!bms[k].flag){
					if( n++ != 0) strcat(bmbuf, "/");
					strcat(bmbuf, bms[k].bmname);	
				}	
			}
			strcpy(allbrd[i].BM, bmbuf);
		}
		
        }
    qsort(lostbms, j, sizeof(lostbm), bmlostdays_cmp);
 
    printf("Starting to mail\n");
    //write to the etc/toplazyBBM
    for ( i=0; i<j; i++)
    {
	if( lostbms[i].lostdays > 30){
		fprintf(firef, "%-*.*s%-*.*s%-*.*s%3d天沒上站\n", IDLEN, IDLEN, lostbms[i].title,
		BTLEN-10, BTLEN-10, lostbms[i].ctitle, IDLEN,IDLEN,
		lostbms[i].bmname,lostbms[i].lostdays);
	}else{
		fprintf(inf, "%-*.*s%-*.*s%-*.*s%3d天沒上站\n", IDLEN, IDLEN, lostbms[i].title, 
		BTLEN-10, BTLEN-10, lostbms[i].ctitle, IDLEN,IDLEN,
		lostbms[i].bmname,lostbms[i].lostdays);
	}
    }
    fclose(inf);
    fclose(firef);

    printf("Total %d boards.\n", j);
    
    //mail to the users
    for( i=0; i<j; i++)
    {
    	fileheader_t mymail;
    	char	genbuf[200];
    	int	lostdays;
    	
    	lostdays = lostbms[i].lostdays;

	if( (lostdays != 14) || (lostdays != 21) ) // 14 21 天不發信
		continue;
	   	
    	sprintf(genbuf, BBSHOME "/home/%c/%s", lostbms[i].bmname[0], lostbms[i].bmname);
    	stampfile(genbuf, &mymail);
    	
    	strcpy(mymail.owner, "[PTT警察局]");
    	
    	if(lostdays <= 30){
    		sprintf(mymail.title,
    		"\033[32m [小組長免職警告通知] \033[m %s BM %s", lostbms[i].title, lostbms[i].bmname);
    	}else{
    		sprintf(mymail.title,
    		"\033[32m [小組長免職通知] \033[m %s BM %s", lostbms[i].title, lostbms[i].bmname);
    	}
    	unlink(genbuf);
    	if(lostdays <= 30){
    		LINK(OUTFILE, genbuf);
    	}else{
    		LINK(FIREFILE, genbuf);
    	}
    	
    	sprintf(genbuf, BBSHOME "/home/%c/%s/.DIR", lostbms[i].bmname[0], lostbms[i].bmname);
  	append_record(genbuf, &mymail, sizeof(mymail)); 	
    }
  
    return 0;
}
