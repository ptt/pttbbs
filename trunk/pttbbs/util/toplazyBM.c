/* $Id: toplazyBM.c,v 1.17 2002/11/02 11:02:44 in2 Exp $ */
#define _UTIL_C_
#include "bbs.h"
#define OUTFILE  BBSHOME "/etc/toplazyBM"
#define FIREFILE BBSHOME "/etc/firelazyBM"
extern boardheader_t *bcache;
extern int numboards;

boardheader_t allbrd[MAX_BOARD];
extern userec_t xuser;
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
BMArray bms[5];


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
    time_t now=time(NULL); 
    resolve_boards();

    if(passwd_mmap())
        exit(1);      

   memcpy(allbrd,bcache,numboards*sizeof(boardheader_t));

	/* write out the target file */
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
         p = allbrd[i].BM;
         if(*p=='[') p++;
         if(allbrd[i].brdname[0] == '\0' ||
            !isalpha(allbrd[i].brdname[0])
            ) continue;

	 p=strtok(allbrd[i].BM,"/ ]");
         for(index=0; p && index<5; index++)
		{
                  if(!p[0])  {index--;
                              p=strtok(NULL,"/ ]");
                              continue;}
  		  bmid=getuser(p);
  		  bms[index].bmname = p;
  		  bms[index].flag = 0;
		  if ((now-xuser.lastlogin)>=45*86400
		       && !(xuser.userlevel & PERM_SYSOP))
		   {
			lostbms[j].bmname = p; 
			lostbms[j].title = allbrd[i].brdname;
			lostbms[j].ctitle = allbrd[i].title;
			lostbms[j].lostdays =
			     (now-xuser.lastlogin)/86400;
			//超過90天 免職
			if(lostbms[j].lostdays > 90){
				xuser.userlevel &= ~PERM_BM;
				bms[index].flag = 1;
				flag = 1;
				passwd_update(bmid, &xuser);
			}
			j++;
		   }
                 p=strtok(NULL,"/ ]");
		}
	
		if(flag == 1){
			bmbuf[0] = '\0';
			for(k = 0 , n = 0; k < index; k++){
				if(!bms[k].flag){
					if( n++ != 0) strcat(bmbuf, "/");
					strcat(bmbuf, bms[k].bmname);	
				}	
			}
			strcpy(allbrd[i].BM, bmbuf);
                        if( substitute_record(BBSHOME"/"FN_BOARD, &allbrd[i], 
                                           sizeof(boardheader_t), i+1) == -1){
			printf("Update Board Faile : %s\n", allbrd[i].brdname);
		  	   }
			reset_board(i+1);
		}
        }
    qsort(lostbms, j, sizeof(lostbm), bmlostdays_cmp);
 
    //write to the etc/toplazyBM
    for ( i=0; i<j; i++)
    {
	if( lostbms[i].lostdays > 90){
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
    
    //printf("Total %d boards.\n", count);
    
    //mail to the users
    for( i=0; i<j; i++)
    {
    	fileheader_t mymail;
    	char	genbuf[200];
    	int	lostdays;
    	
    	lostdays = lostbms[i].lostdays;

	if( lostdays != 45 && lostdays != 60 && lostdays!=75 &&(lostdays <= 90))
		continue;

    	sprintf(genbuf, BBSHOME "/home/%c/%s", 
                         lostbms[i].bmname[0], lostbms[i].bmname);
    	stampfile(genbuf, &mymail);
    	
    	strcpy(mymail.owner, "[PTT警察局]");
    	if(lostdays <= 90){
    		sprintf(mymail.title,
    		"\033[32m版主通知\033[m %s版版主%s", lostbms[i].title, 
                   lostbms[i].bmname);
    	}else{
    		sprintf(mymail.title,
    		  "\033[32m版主自動免職通知\033[m %s 版主 %s",lostbms[i].title, 
                       lostbms[i].bmname);
    	}
    	unlink(genbuf);
    	if(lostdays <= 90){
    		LINK(OUTFILE, genbuf);
    	}else{
    		LINK(FIREFILE, genbuf);
    	}
    	
    	sprintf(genbuf, BBSHOME "/home/%c/%s/.DIR", 
                  lostbms[i].bmname[0], lostbms[i].bmname);
  	append_record(genbuf, &mymail, sizeof(mymail)); 	
    }
    return 0;
}
