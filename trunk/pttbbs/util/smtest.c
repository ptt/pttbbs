#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include "config.h"
#include "pttstruct.h"
#include "util.h"
#include "perm.h"
#include "common.h" 
#include "proto.h"

#define WARNFILE  BBSHOME "/etc/DeleteBoard.warn"
#define EXECFILE  BBSHOME"/etc/DeleteBoard.exec"
#define WARNLIST  BBSHOME"/etc/DeleteBoardList.warn"
#define EXECLIST  BBSHOME"/etc/DeleteBoardList.exec" 

extern boardheader_t *bcache;
extern int numboards;

boardheader_t allbrd[MAX_BOARD];
extern userec_t xuser;


int LINK(char* src, char* dst){
	char cmd[200];
	
	if( link(src, dst) == 0){
		return 0;
	}
	
	sprintf(cmd, "/bin/cp -R %s %s", src, dst);
	return system(cmd);
}

int outofdate(char *hdrdate, char thedate[], int *zf)
{
  int k = 0;
  char *dd;
  char latestdate[6];
  int date1[2], date2[2],datetemp;
  *zf = 0;

  strcpy(latestdate, thedate);

  dd = strtok(hdrdate,"/");
  if(dd == NULL) return 2;
  if(dd)
    k = 0;
    do{
      if (*dd  == '[' ){dd[strlen(dd)-1]='\0'; dd++;}
        date1[k] = atoi(dd);
      k++;
    } while((dd=strtok(NULL,"/ "))!=NULL);

  dd = strtok(latestdate,"/");
  if(dd)
    k = 0;
    do{
      if (*dd  == '[' ){dd[strlen(dd)-1]='\0'; dd++;}
        date2[k] = atoi(dd);
      k++;
    } while((dd=strtok(NULL,"/ "))!=NULL);

  if(date2[0] == date1[0] && date2[1] >= date1[1])
    return 0;

  datetemp = date2[0];

  for(k = 1;k <= 5;k++)
  {
    datetemp -= 1;
    if((datetemp) <= 0){
      datetemp = 12;
    }
    if(k < 3 && datetemp == date1[0]) return 0;
    if(k == 3 && datetemp == date1[0] && date2[1] <= date1[1]) return 0;
    if(k == 3 && datetemp == date1[0] && date2[1] > date1[1]) return 1;
    if(k == 4 && datetemp == date1[0] && date2[1] > date1[1]) return 1;
  }
  *zf = 1;
  return 1;
}

void mailtouser(char *bmname, char *bname, int zf)
{
  fileheader_t mymail;
  char    genbuf[200];

  sprintf(genbuf, BBSHOME "/home/%c/%s", bmname[0], bmname);
  stampfile(genbuf, &mymail);
  strcpy(mymail.owner, "[PTT警察局]");

  if(zf == 0){
    sprintf(mymail.title,"\033[32m [廢版警告通知]"
 	"\033[m %s版(BM:%s)",bname, bmname);
  }else{
    sprintf(mymail.title,"\033[32m [廢版通知] "
 	"\033[m %s版(BM:%s)",bname, bmname);
  }
  mymail.savemode = 0 ;
  unlink(genbuf);
  if(zf == 0){
    LINK(WARNFILE, genbuf);
  }else{
    LINK(EXECFILE, genbuf);
  }

  sprintf(genbuf, BBSHOME "/home/%c/%s/.DIR", bmname[0], bmname);
  append_record(genbuf, &mymail, sizeof(mymail)); 
}

int main()
{
    int bmid, i, j, k, rd, ood, flag, zapflag = 0,warncount = 0,execcount = 0;
    char *p, *bmsname[3], fname[256], hdrdatetemp[6],thedate[6];
    char bname[32],genbuf[200];
    fileheader_t hdr;
    FILE *inf, *def;
    
    ///// set date //////
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    sprintf(thedate, "%2d/%02d", tm->tm_mon + 1, tm->tm_mday);

    ////// board //////

    resolve_boards();
    if(passwd_mmap())
        exit(1);
    memcpy(allbrd,bcache,numboards*sizeof(boardheader_t));

    ////// write out the target file //////
    inf = fopen(WARNLIST, "w+");
    if(inf == NULL){
        printf("open file error : %s\n", WARNLIST);
        exit(1);
    }

    def = fopen(EXECLIST, "w+"); 
    if(def == NULL)
    {
        printf("open file error : %s\n", EXECLIST);
        exit(1);
    }

    ////// fprint table title /////
    fprintf(inf,"\n[廢版警告]即日起一個月內若看板"
	"使用率仍然過低，則予以廢除。\n\n"
	"英文版名    類別 中文版名           日期    "
    	"    版主名單\n\n");

    fprintf(def,"\n[廢版公告]下列看板因使用率仍然過低，故予以廢除。\n\n"
        "英文版名    類別 中文版名           日期    "
        "    版主名單\n\n");

    ////// start process /////
    j = 0 ;
    for (i = 0; i < numboards; i++) {
        rd = 0;
	if(allbrd[i].brdname[0] == '\0') continue;
	if((allbrd[i].brdattr & BRD_NOZAP) || 
	(allbrd[i].brdattr & BRD_GROUPBOARD) ||
	(allbrd[i].brdattr & BRD_WARNDEL) || 
	(allbrd[i].brdattr & BRD_HIDE) ||
	(allbrd[i].brdattr & BRD_POSTMASK) ||
	(allbrd[i].brdattr & BRD_VOTEBOARD) ||
	(allbrd[i].brdattr & BRD_BAD) ||
	(allbrd[i].level != 0)) continue;

	sprintf(fname, BBSHOME "/boards/%s/.DIR",allbrd[i].brdname); 

	/* get date to choose junk board  */
	/* exception when ood == 2 */
	flag = 30;
	rd = get_num_records(fname, sizeof(fileheader_t));
	if(rd <= 30)
	{
	    get_record(fname, &hdr, sizeof (hdr), 1);
	    strcpy(hdrdatetemp, hdr.date);
	    ood = outofdate(hdrdatetemp,thedate, &zapflag);
	}
	else
	{
	  do{
	    if(rd == 0) 
	      {
		ood = 0;
		break;
	      }
	    get_record(fname, &hdr, sizeof (hdr), rd - flag);
	    strcpy(hdrdatetemp, hdr.date);
	    ood = outofdate(hdrdatetemp,thedate, &zapflag);
	    flag += 5;
    	  }while(ood == 2 && flag < 60);
	}
	if(ood == 0) continue;

	warncount++;
	/* print to file */
        fprintf(inf,"%-*.*s%-*.*s %-*.*s%-*.*s\n", IDLEN, IDLEN,
	allbrd[i].brdname, BTLEN-26, BTLEN-26, allbrd[i].title, 
	IDLEN - 5, IDLEN-5,hdr.date, IDLEN * 3, IDLEN * 3, allbrd[i].BM);

        /* post warn file to each board */
        sprintf(genbuf,"~/bin/post %s [廢版警告通知]"
        " [PTT警察局] %s",allbrd[i].brdname,WARNFILE);
        system(genbuf);

	/* user extract to mail */
        p=strtok(allbrd[i].BM,"/ ");
	if(p){
	    k = 0;
	    do
	    {
	        if (*p  == '[' ){p[strlen(p)-1]='\0'; p++;}
  		  bmid=getuser(p);
  		  bmsname[k] = p;
		  if(isalpha(allbrd[i].BM[0])&& !(xuser.userlevel &PERM_SYSOP))
		  {
			mailtouser(bmsname[k],allbrd[i].title, zapflag);
		  }
	        k++;
	    } while((p=strtok(NULL,"/ "))!=NULL);
	}
	/* set attribute of DeleteBoardWarn Flag */
	bcache[i].brdattr = allbrd[i].brdattr | BRD_WARNDEL;

	/* zap boards */
	if (zapflag == 1)
	{
	execcount++;
        /* print to file */
        fprintf(def,"%-*.*s%-*.*s %-*.*s%-*.*s\n", IDLEN, IDLEN,
        allbrd[i].brdname, BTLEN-26, BTLEN-26, allbrd[i].title,
        IDLEN - 5, IDLEN-5,hdr.date, IDLEN * 3, IDLEN * 3, allbrd[i].BM); 

//	strcpy(bname, allbrd[i].brdname);
//	sprintf(genbuf,
//        "/bin/tar zcvf ~/tmp/board_%s.tgz boards/%s man/%s>/dev/null 2>&1;"
//        "/bin/rm -fr ~/boards/%s man/%s",bname, bname, bname, bname,bname);
//      system(genbuf);

//      memset(&allbrd[i], 0, sizeof(allbrd[i]));
//      sprintf(allbrd[i].title, "[%s] deleted by System", bname);
//      substitute_record(fn_board, &bh, sizeof(allbrd[i]), bid);
//      reset_board(bid);
	}

    }

        /* post to  Record, ViolateLaw */
    if(warncount > 0){
      sprintf(genbuf,"~/bin/post Record [廢版警告通知]"
      " [PTT警察局] %s",WARNLIST);
        system(genbuf);
      sprintf(genbuf,"~/bin/post ViolateLaw [廢版警告通知]"
      " [PTT警察局] %s",WARNLIST);
        system(genbuf);
	}
    if(execcount > 0){
      sprintf(genbuf,"~/bin/post Record [廢版公告]"
      " [PTT警察局] %s",EXECLIST);
        system(genbuf);
      sprintf(genbuf,"~/bin/post ViolateLaw [廢版公告]"
      " [PTT警察局] %s",EXECLIST);
        system(genbuf);
    }


/*     Below is for test only      */
/*
	mailtouser("Smile","test", 1);
	mailtouser("Smile","test", 0);

        strcpy(bname, "Test");
        sprintf(genbuf,"~/bin/post %s test Smile ~/etc/test.fileaaa",bname);
	system(genbuf);


    bid = getbnum(bname); 
        strcpy(bname,"jourslamdunk");
      sprintf(genbuf,
        "/bin/tar zcvf ~/tmp/board_%s.tgz boards/%s man/%s>/dev/null 2>&1;"
        "/bin/rm -fr ~/boards/%s man/%s",bname, bname, bname,bname,bname);
      system(genbuf);

      memset(&bh, 0, sizeof(bh));
      sprintf(bh.title, "[%s] deleted by %s", bname,cuser.userid);
      substitute_record(fn_board, &bh, sizeof(bh), bid);
      reset_board(bid);
*/
    return 0;
}
