/* $Id: merge.c 3650 2007-12-08 02:37:03Z piaip $ */
#define _XOPEN_SOURCE
#define _ISOC99_SOURCE
/* this is a interface provided when we merge BBS */ 
#include "bbs.h"
#include "fpg.h"

int
m_sob(void)
{
    char genbuf[256], buf[256], userid[25], passbuf[24], msg[2048]="";
    int count=0, i, isimported=0, corrected;
    FILE *fp;
    sobuserec man;
    time4_t d;

    clear();
    move(1,0);

    outs(
 "   請注意 這是只給陽光沙灘使用者!\n"
 "      讓沙灘的使用者轉移個人資產以及重要信用資料, 享有平等安全的環境.\n"
 "      如果您不需要, 請直離開.\n"
 "    -----------------------------------------------------------------\n"
 "    特別叮嚀:\n"
 "      為了帳號安全,您只有連續十次密碼錯誤的機會,請小心輸入.\n"
 "      連續次錯誤您的變身功\能就會被開罰單並直接通知站長.\n"
 "      請不要在變身過程中不正常斷線, 刻意斷線變半獸人站長不救唷.\n"
	);

   if(getkey("是否要繼續?(y/N)")!='y') return 0;
   if(search_ulistn(usernum,2)) 
        {vmsg("請登出其他視窗, 以免變身失敗"); return 0;}
   do
   {
    if(!getdata(10,0, "      沙灘的ID [大小寫要完全正確]:", userid, 20,
	       DOECHO)) return 0;
    if(bad_user_id(userid)) continue;
    sprintf(genbuf, "sob/passwd/%c/%s.inf",userid[0], userid);
    if(!(fp=fopen(genbuf, "r"))) 
       {
        isimported = 1;
        strcat(genbuf, ".done");
        if(!(fp=fopen(genbuf, "r")))
         {
           vmsg("查無此人或已經匯入過..請注意大小寫 ");
           isimported = 0;
           continue;
         }
       }
    count = fread(&man, sizeof(man), 1, fp);
    fclose(fp);
   }while(!count);
   count = 0;
   do{
    if(!getdata(11,0, "      沙灘的密碼:", passbuf, sizeof(passbuf), 
		  NOECHO)) return 0;
    if(++count>=10)
    {
          cuser.userlevel |= PERM_VIOLATELAW;
          cuser.vl_count++;
	  passwd_update(usernum, &cuser);
          post_violatelaw(cuser.userid, "[PTT警察]", "測試帳號錯誤十次",
		          "違法觀察");
          mail_violatelaw(cuser.userid, "[PTT警察]", "測試帳號錯誤十次",
		          "違法觀察");

          return 0;
    }
    if(!(corrected = checkpasswd(man.passwd, passbuf)))
       vmsg("密碼錯誤"); 
   } while(!corrected);
   move(12,0);
   clrtobot();

   if(!isimported)
     {
       if(!dashf(genbuf))  // avoid multi-login
       {
         vmsg("請不要嘗試多重id踹匯入");
         return 0;
       }
       sprintf(buf,"%s.done",genbuf);
       rename(genbuf,buf);
#ifdef MERGEMONEY

   reload_money(); 

   sprintf(buf, 
           "您的沙灘鸚鵡螺 %10d 換算成 " MONEYNAME " 幣為 %9d (匯率 22:1), \n"
           "    沙灘貝殼有 %10d 換算為 " MONEYNAME " 幣為 %9d (匯率 222105:1), \n"
           "    原有 %10d 匯入後共有 %d\n",
	   (int)man.goldmoney, (int)man.goldmoney/22, 
	   (int)man.silvermoney, (int)man.silvermoney/222105,
	   cuser.money,
	   (int)(cuser.money + man.goldmoney/22 + man.silvermoney/222105));
   demoney(man.goldmoney/22 + man.silvermoney/222105 );
   strcat(msg, buf); 
#endif

     i =  cuser.exmailbox + man.exmailbox + man.exmailboxk/2000;
     if (i > MAX_EXKEEPMAIL) i = MAX_EXKEEPMAIL;
     sprintf(buf, "您的沙灘信箱有 %d (%dk), 原有 %d 匯入後共有 %d\n", 
	    man.exmailbox, man.exmailboxk, cuser.exmailbox, i);
     strcat(msg, buf);
     cuser.exmailbox = i;

     if(man.userlevel & PERM_MAILLIMIT)
      {
       sprintf(buf, "開啟信箱無上限\n");
       strcat(msg, buf);
       cuser.userlevel |= PERM_MAILLIMIT;
      }

     if (cuser.firstlogin > man.firstlogin)
	 d = man.firstlogin;
     else
	 d = cuser.firstlogin;
     cuser.firstlogin = d;

     if (cuser.numlogins < man.numlogins)
	 i = man.numlogins;
     else
	 i = cuser.numlogins;

     sprintf(buf, "沙灘進站次數 %d 此帳號 %d 將取 %d \n", man.numlogins,
	   cuser.numlogins, i);
     strcat(msg,buf);
     cuser.numlogins = i;

     if (cuser.numposts < man.numposts )
	 i = man.numposts;
     else
	 i = cuser.numposts;
     sprintf(buf, "沙灘文章次數 %d 此帳號 %d 將取 %d\n", 
                 man.numposts,cuser.numposts,i);
     strcat(msg,buf);
     cuser.numposts = i;
     outs(msg);
     while (search_ulistn(usernum,2)) 
        {vmsg("請將重覆上站其他線關閉! 再繼續");}
     passwd_update(usernum, &cuser);
   }
   sethomeman(genbuf, cuser.userid);
   mkdir(genbuf, 0600);
   sprintf(buf, "tar zxvf %c/%s.tar.gz>/dev/null",
	   userid[0], userid);
   chdir("sob/home");
   system(buf);
   chdir(BBSHOME);

   if (getans("是否匯入個人信箱? (Y/n)")!='n')
   {
	sethomedir(buf, cuser.userid);
	sprintf(genbuf, "sob/home/%c/%s/.DIR",
		userid[0], userid);
	merge_dir(buf, genbuf, 1);
        strcat(msg, "匯入個人信箱\n");
   }
   if(getans("是否匯入個人信箱精華區(個人作品集)? (會覆蓋\現有設定) (y/N)")=='y')
   {
        fileheader_t fh;
        sprintf(buf,
	 "rm -rd home/%c/%s/man>/dev/null ; "
         "mv sob/home/%c/%s/man home/%c/%s>/dev/null;"
         "mv sob/home/%c/%s/gem home/%c/%s/man>/dev/null", 
              cuser.userid[0], cuser.userid,
	      userid[0], userid,
	      cuser.userid[0], cuser.userid,
	      userid[0], userid,
	      cuser.userid[0], cuser.userid);
        system(buf);
        strcat(msg, "匯入個人信箱精華區(個人作品集)\n");
        sprintf(buf,"home/%c/%s/man/gem", cuser.userid[0], cuser.userid);
        if(dashd(buf))
         {
          strcat(fh.title, "◆ 個人作品集");
          strcat(fh.filename, "gem");
          sprintf(fh.owner, cuser.userid);
          sprintf(buf, "home/%c/%s/man/.DIR", cuser.userid[0], cuser.userid);
          append_record(buf, &fh, sizeof(fh));
         }
   }
   if(getans("是否匯入好友名單? (會覆蓋\現有設定, ID可能是不同人)? (y/N)")=='y')
   {
       sethomefile(genbuf, cuser.userid, "overrides");
       sprintf(buf, "sob/home/%c/%s/overrides",userid[0],userid);
       Copy(buf, genbuf);
       strcat(buf, genbuf);
       friend_load(FRIEND_OVERRIDE);
       strcat(msg, "匯入好友名單\n");
   }
   sprintf(buf, "帳號匯入報告 %s -> %s ", userid, cuser.userid);
   post_msg(GLOBAL_SECURITY, buf, msg, "[系統安全局]");

   vmsg("恭喜您完成帳號變身..");
   return 0;
}

void
m_sob_brd(char *bname, char *fromdir)
{
  char fbname[25], buf[256];
  fileheader_t fh;

  fromdir[0]=0;
  do{

     if(!getdata(20,0, "SOB的板名 [英文大小寫要完全正確]:", fbname, 20,
	        DOECHO)) return;
  }
  while((invalid_brdname(fbname)&1));

  sprintf(buf, "sob/man/%s.tar.gz", fbname);
  if(!dashf(buf))
  {
       vmsg("無此看板");
       return;
  }
  chdir(BBSHOME"/sob/boards");
  sprintf(buf, "tar zxf %s.tar.gz >/dev/null",fbname);
  system(buf);
  chdir(BBSHOME"/sob/man");
  sprintf(buf, "tar zxf %s.tar.gz >/dev/null", fbname);
  system(buf);
  chdir(BBSHOME);
  sprintf(buf, "mv sob/man/%s man/boards/%c/%s", fbname,
	    bname[0], bname);
  system(buf);
  sprintf(fh.title, "◆ %s 精華區", fbname);
  sprintf(fh.filename, fbname);
  sprintf(fh.owner, cuser.userid);
  sprintf(buf, "man/boards/%c/%s/.DIR", bname[0], bname);
  append_record(buf, &fh, sizeof(fh));
  sprintf(fromdir, "sob/boards/%s/.DIR", fbname);
  vmsgf("即將匯入 %s 板資料..按鍵後需要一點時間",fbname);
}
