/* $Id: gamble.c,v 1.10 2002/06/07 18:47:40 ptt Exp $ */
#include "bbs.h"

#ifndef _BBS_UTIL_C_
#define MAX_ITEM	8	//最大 賭項(item) 個數
#define MAX_ITEM_LEN	30	//最大 每一賭項名字長度
#define MAX_SUBJECT_LEN 650	//8*81 = 648 最大 主題長度 

static char betname[MAX_ITEM][MAX_ITEM_LEN];
static int currbid;

int post_msg(char* bname, char* title, char *msg, char* author)
{
    FILE *fp;
    int bid;
    fileheader_t fhdr;
    char genbuf[256];
                
    /* 在 bname 版發表新文章 */
    sprintf(genbuf, "boards/%c/%s", bname[0], bname);
    stampfile(genbuf, &fhdr);
    fp = fopen(genbuf,"w");
    
    if(!fp)
      return -1;
    
    fprintf(fp, "作者: %s 看板: %s\n標題: %s \n",author,bname,title);
    fprintf(fp, "時間: %s\n", ctime(&now));
                                                
    /* 文章的內容 */
    fprintf(fp, "%s", msg);
    fclose(fp);
                                                                        
    /* 將檔案加入列表 */
    strcpy(fhdr.title, title);
    strcpy(fhdr.owner, author);
    setbdir(genbuf,bname);
    if(append_record(genbuf, &fhdr, sizeof(fhdr))!=-1)
      if((bid = getbnum(bname)) > 0)
          setbtotal(bid);
    return 0;
}

int post_file(char* bname,  char* title, char *filename, char* author)
{
 int size=dashs(filename);
 char *msg;
 FILE *fp;

 if(size<=0) return -1;
 if(!(fp=fopen(filename,"r")) ) return -1;
 msg= (char *)malloc(size);
 fread(msg,1,size,fp);
 size= post_msg(bname, title, msg, author);
 fclose(fp);
 free(msg);
 return size;
}


static int load_ticket_record(char *direct, int ticket[])
{
   char buf[256];
   int i, total=0;
   FILE *fp;
   sprintf(buf,"%s/"FN_TICKET_RECORD,direct);
   if(!(fp=fopen(buf,"r"))) return 0;
   for(i=0;i<MAX_ITEM && fscanf(fp, "%9d ",&ticket[i]); i++) 
      total=total+ticket[i]; 
   fclose(fp);
   return total;
}

static int show_ticket_data(char *direct, int *price, boardheader_t *bh) {
    int i,count, total = 0, end=0,
        ticket[MAX_ITEM] = {0, 0, 0, 0, 0, 0, 0, 0};
    FILE *fp;
    char genbuf[256];

    clear();
    if (bh)
      {
        sprintf(genbuf,"%s 賭盤", bh->brdname);
        showtitle(genbuf, BBSNAME);
      }	
    else
        showtitle("Ptt賭盤", BBSNAME);
    move(2, 0);
    sprintf(genbuf, "%s/"FN_TICKET_ITEMS, direct);
    if(!(fp = fopen(genbuf,"r")))
    {
      prints("\n目前並沒有舉辦賭盤\n");
      sprintf(genbuf, "%s/"FN_TICKET_OUTCOME, direct);
      if(more(genbuf, NA));
      return 0;
    }
    fgets(genbuf,MAX_ITEM_LEN,fp);
    *price=atoi(genbuf);
    for(count=0; fgets(betname[count],MAX_ITEM_LEN,fp)&&count<MAX_ITEM; count++)
	strtok(betname[count],"\r\n");
    fclose(fp);

    prints("\033[32m站規:\033[m 1.可購買以下不同類型的彩票。每張要花 \033[32m%d\033[m 元。\n"
         "      2.%s\n"
         "      3.開獎時只有一種彩票中獎, 有購買該彩票者, 則可依購買的張數均分"
         "總賭金。\n"
         "      4.每筆獎金由系統抽取 5% 之稅金%s。\n\n"
         "\033[32m%s:\033[m", *price,
         bh?"此賭盤由版主負責舉辦並且決定開獎時間結果, 站長不管, 願賭服輸。":
             "系統每天 2:00 11:00 16:00 21:00 開獎。",
	 bh?", 其中 2% 分給開獎版主":"",
         bh?"版主自訂規則及說明":"前幾次開獎結果");


    sprintf(genbuf, "%s/"FN_TICKET, direct);
    if(!dashf(genbuf))
	{
	  sprintf(genbuf, "%s/"FN_TICKET_END, direct);
	  end=1;
	}
    show_file(genbuf, 8, -1, NO_RELOAD);
    move(15,0);
    prints("\033[1;32m目前下注狀況:\033[m\n");

    total=load_ticket_record(direct, ticket);
    
    prints("\033[33m");
    for(i = 0 ; i<count; i++)
      {
	prints("%d.%-8s: %-7d", i+1, betname[i], ticket[i]);
	if(i==3) prints("\n");
      }
    prints("\033[m\n\033[42m 下注總金額:\033[31m %d 元 \033[m",total*(*price));
    if(end)
     {
       prints("\n賭盤已經停止下注\n");
       return -count;
     }
    return count;
}

static void append_ticket_record(char *direct, int ch, int n, int count) {
    FILE *fp;
    int ticket[8] = {0,0,0,0,0,0,0,0}, i;
    char genbuf[256];
    sprintf(genbuf, "%s/"FN_TICKET_USER, direct);
    
    if((fp = fopen(genbuf,"a"))) {
	fprintf(fp, "%s %d %d\n", cuser.userid, ch, n);
	fclose(fp);
    }
    load_ticket_record(direct, ticket);
    ticket[ch] += n;
    sprintf(genbuf, "%s/" FN_TICKET_RECORD, direct);
    if((fp = fopen(genbuf,"w"))) {
        for(i=0; i<count; i++)
           fprintf(fp,"%d ", ticket[i]);
	fclose(fp);
    }
}

#define lockreturn0(unmode, state) if(lockutmpmode(unmode, state)) return 0
int ticket(int bid)
{
    int ch, n, price, count, end=0;
    char path[128],fn_ticket[128];
    boardheader_t *bh=NULL; 
    
    if(bid)
      {
       bh=getbcache(bid);
       setbpath(path, bh->brdname);   
       setbfile(fn_ticket, bh->brdname, FN_TICKET);   
       currbid = bid;
      }
    else
       strcpy(path,"etc/");

    lockreturn0(TICKET, LOCK_MULTI);
    while(1) {
	count=show_ticket_data(path, &price, bh);
        if(count<=0) 
        {
	   pressanykey();
           break;
        } 
        move(20, 0);
        reload_money();
	prints("\033[44m錢: %-10d  \033[m\n\033[1m請選擇要購買的種類(1~%d)"
        "[Q:離開]\033[m:", cuser.money,count);
	ch = igetch();
	/*-- 
	  Tim011127
	  為了控制CS問題 但是這邊還不能完全解決這問題,        
	  若user通過檢查下去, 剛好版主開獎, 還是會造成user的這次紀錄
	  很有可能跑到下次賭盤的紀錄去, 也很有可能被版主新開賭盤時洗掉
	  不過這邊至少可以做到的是, 頂多只會有一筆資料是錯的
	--*/
        if(bid && !dashf(fn_ticket))
        {
           move(b_lines-1,0);
           prints("哇!! 耐ㄚ捏...版主已經停止下注了 不能賭嚕");
	   pressanykey();
	   break;
        }

        if(ch=='q' || ch == 'Q')
	    break;
        ch-='1';
	if(end || ch >= count || ch < 0)
	    continue;
        n=0;
        ch_buyitem(price, "etc/buyticket", &n);
	if(n > 0)
		append_ticket_record(path,ch,n,count);
      }	
    unlockutmpmode();
    return 0;
}

int openticket(int bid) {
    char path[128],buf[256],outcome[128];
    int i, money=0, count, bet, price, total = 0, ticket[8]={0,0,0,0,0,0,0,0};
    boardheader_t *bh=getbcache(bid);
    FILE  *fp, *fp1;

    setbpath(path, bh->brdname); 
    count=-show_ticket_data(path, &price, bh);
    if(count==0)
      {
         setbfile(buf,bh->brdname,FN_TICKET_END);
         unlink(buf); //Ptt: 有bug
         return 0;
      }
    lockreturn0(TICKET, LOCK_MULTI);
    do
    {
     do
     {
       getdata(20, 0,
         "\033[1m選擇中獎的號碼(0:不開獎 99:取消退錢)\033[m:", buf, 3, LCECHO);
       bet=atoi(buf);
       move(0,0);
       clrtoeol();
     } while( (bet<0 || bet>count) && bet!=99);
     if(bet==0) 
        {unlockutmpmode(); return 0;}
     getdata(21, 0, "\033[1m再次確認輸入號碼\033[m:", buf, 3, LCECHO);
    }while(bet!=atoi(buf));

    bet -= 1; //轉成矩陣的index

    total=load_ticket_record(path, ticket);
    setbfile(buf,bh->brdname,FN_TICKET_END);
    if(!(fp1 = fopen(buf,"r")))
      {
         unlockutmpmode();
         return 0; 
      }
        // 還沒開完獎不能賭博 只要mv一項就好 
    if(bet!=98)
     {
      money=total*price;
      demoney(money*0.02);
      mail_redenvelop("[賭場抽頭]", cuser.userid, money*0.02, 'n');
      money = ticket[bet] ?  money*0.95/ticket[bet]:9999999; 
     }
    else
     {
      vice(price*10, "賭盤退錢手續費");
      money=price;
     }
    setbfile(outcome,bh->brdname,FN_TICKET_OUTCOME);
    if((fp = fopen(outcome, "w")))
    {  
      fprintf(fp,"賭盤說明\n");
      while(fgets(buf,256,fp1))
         {
	   buf[255]=0;
	   fprintf(fp,"%s",buf);
         }
      fprintf(fp,"下注情況\n");

      fprintf(fp, "\033[33m");
      for(i = 0 ; i<count; i++)
      {
        fprintf(fp, "%d.%-8s: %-7d",i+1,betname[i], ticket[i]);
        if(i==3) fprintf(fp,"\n");
      }
      fprintf(fp, "\033[m\n");
      
      if(bet!=98)
       {                                  
        fprintf(fp, "\n\n開獎時間： %s \n\n"
             "開獎結果： %s \n\n"
             "所有金額： %d 元 \n"
             "中獎比例： %d張/%d張  (%f)\n"
             "每張中獎彩票可得 %d 枚Ｐ幣 \n\n",
             Cdatelite(&now), betname[bet], total*price, ticket[bet], total,
             (float) ticket[bet] / total, money);
             
        fprintf(fp, "%s 賭盤開出:%s 所有金額:%d 元 獎金/張:%d 元 機率:%1.2f\n",
              Cdatelite(&now), betname[bet], total*price, money,
              total? (float)ticket[bet] / total:0);
       }
      else 
        fprintf(fp, "\n\n賭盤取消退錢： %s \n\n",  Cdatelite(&now));
              
    }
    fclose(fp1); 

    setbfile(buf, bh->brdname, FN_TICKET_END);
    unlink(buf);
    if(fork())
      {  // Ptt 用fork防止不正常斷線洗錢
        move(22,0);
    prints("系統將於稍後自動把中獎結果公佈於看板 若參加者多會需要幾分鐘時間..");
        pressanykey();
        unlockutmpmode();
        return 0;
      }
    close(0);
    close(1);
    /*
      以下是給錢動作
    */
    setbfile(buf, bh->brdname, FN_TICKET_USER);
    if ((bet==98 || ticket[bet]) && (fp1 = fopen(buf, "r")))  
    {
        int mybet, uid;
        char userid[IDLEN];
        
        while (fscanf(fp1, "%s %d %d\n", userid, &mybet, &i) != EOF)
        {
           if (bet==98 )
           {
                fprintf(fp,"%s 買了 %d 張 %s, 退回 %d 枚Ｐ幣\n"
                       ,userid, i, betname[mybet], money);
                sprintf(buf, "%s 賭場退錢! $ %d", bh->brdname,  money * i);
           }
           else if (mybet == bet)
           {
                fprintf(fp,"恭喜 %s 買了%d 張 %s, 獲得 %d 枚Ｐ幣\n"
                       ,userid, i, betname[mybet], money);
                sprintf(buf, "%s 中獎咧! $ %d", bh->brdname,  money * i);
           }
           if((uid=getuser(userid))==0) continue;
           deumoney(uid, money * i);
           mail_id(userid, buf, "etc/ticket.win", "Ptt賭場");
        }   
        fclose(fp1);
    }
    fclose(fp);

    if(bet!=98)
      sprintf(buf, "[公告] %s 賭盤開獎", bh->brdname);
    else
      sprintf(buf, "[公告] %s 賭盤取消", bh->brdname);
    post_file(bh->brdname, buf, outcome, "[賭神]");
    post_file("Record", buf+7, outcome, "[馬路探子]");

    setbfile(buf, bh->brdname, FN_TICKET_RECORD); 
    unlink(buf);
    setbfile(buf, bh->brdname, FN_TICKET_USER); 
    unlink(buf);
    exit(1);
    return 0;
}

int ticket_main() {
    ticket(0);
    return 0;
}
#endif
