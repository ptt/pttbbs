/* $Id: merge.c 2060 2004-06-11 17:18:06Z Ptt $ */
#define _XOPEN_SOURCE
#define _ISOC99_SOURCE
/* this is a interface provided when we merge BBS */ 
#include "bbs.h"
#include "fpg.h"

int
m_fpg()
{
    char genbuf[256], buf[256], userid[25], passbuf[24], msg[2048]="";
    int count=0, i, isimported=0;
    FILE *fp;
    ACCT man;
    time_t d;

    clear();
    move(1,0);

    outs(
 "    ¤p³½ªºµµ¦âªá¶é,\n"
 "      Åýªá¶éªº¨Ï¥ÎªÌÂà²¾­Ó¤H¸ê²£¥H¤Î­«­n«H¥Î¸ê®Æ, ¨É¦³¥­µ¥¦w¥þªºÀô¹Ò.\n"
 "      ¦pªG±z¤£»Ý­n, ½Ðª½±µ«ö[Enter]Â÷¶}.\n"
 "    -----------------------------------------------------------------\n"
 "    ¯S§O¥mÀ{:\n"
 "      ¬°¤F±b¸¹¦w¥þ,±z¥u¦³³sÄò¤T¦¸±K½X¿ù»~ªº¾÷·|,½Ð¤p¤ß¿é¤J.\n"
 "      ³sÄò¤T¦¸¿ù»~±zªºÅÜ¨­¥\\¯à´N·|³Q¶}»@³æ¨Ãª½±µ³qª¾¯¸ªø.\n"
 "      ½Ð¤£­n¦bÅÜ¨­¹Lµ{¤¤¤£¥¿±`Â_½u, ¨è·NÂ_½uÅÜ¥bÃ~¤H¯¸ªø¤£±Ï­ò.\n"
	);


   if(search_ulistn(usernum,2)) 
        {vmsg("½Ðµn¥X¨ä¥Lµøµ¡, ¥H§KÅÜ¨­¥¢±Ñ"); return 0;}
   do
   {
    if(!getdata(10,0, "      ¤p³½ªºID [­^¤å¤j¤p¼g­n§¹¥þ¥¿½T]:", userid, 20,
	       DOECHO)) return 0;
    if(bad_user_id(userid)) continue;
    sprintf(genbuf, "/home/bbs/fpg/home/%c/%s.ACT",userid[0], userid);
    if(!(fp=fopen(genbuf, "r"))) 
       {
        isimported = 1;
        strcat(genbuf, ".done");
        if(!(fp=fopen(genbuf, "r")))
         {
           vmsg("¬dµL¦¹¤H©Î¤w¸g¶×¤J¹L..½Ðª`·N¤j¤p¼g ");
           isimported = 0;
           continue;
         }
       }
    count = fread(&man, sizeof(man), 1, fp);
    fclose(fp);
   }while(!count);
   count = 0;
   do{
    getdata(11,0, "      ¤p³½ªº±K½X:", passbuf, sizeof(passbuf), 
		  NOECHO);
    if(++count>=3)
    {
          cuser.userlevel |= PERM_VIOLATELAW;
          cuser.vl_count++;
	  passwd_update(usernum, &cuser);
          post_violatelaw(cuser.userid, "[PTTÄµ¹î]", "´ú¸Õ¤p³½±b¸¹¿ù»~¤T¦¸",
		          "¹HªkÆ[¹î");
          mail_violatelaw(cuser.userid, "[PTTÄµ¹î]", "´ú¸Õ¤p³½±b¸¹¿ù»~¤T¦¸",
		          "¹HªkÆ[¹î");

          return 0;
    }
   } while(!checkpasswd(man.passwd, passbuf));
   move(12,0);
   clrtobot();

   if(!isimported)
     {
       if(!dashf(genbuf))  // avoid multi-login
       {
         vmsg("½Ð¤£­n¹Á¸Õ¦h­«id¿å¶×¤J");
         return 0;
       }
       sprintf(buf,"%s.done",genbuf);
       rename(genbuf,buf);
#ifdef MERGEMONEY
    int price[10] = {74, 21, 29, 48, 67, 11, 9, 43, 57, 72};
    unsigned lmarket=0;

   reload_money(); 

   for(i=0; i<10; i++)
     lmarket += man.market[i]/(674 / price[i]);
   sprintf(buf, 
           "±zªºªá¶é¹ô¦³ %10d ´«ºâ¦¨ Ptt ¹ô¬° %9d (Àu´f¶×²v 155:1), \n"
           "    »È¦æ¦³   %10d ´«ºâ¬° Ptt ¹ô¬° %9d (¶×²v¬° 674:1), \n"
           "    ªá¥«»ù­È %10d ´«ºâ¬° Ptt ¹ô¬° %9d (¶×²v¬° 674:1), \n"
           "    ­ì¦³P¹ô  %10d ¶×¤J«á¦@¦³ %d\n",
            man.money, man.money/155, 
            man.bank, man.bank/674,
            lmarket*674, lmarket,
            cuser.money, cuser.money + man.money/155 + man.bank/674 + lmarket);
   demoney(man.money/155 + man.bank/674 + lmarket);
   strcat(msg, buf); 
#endif

     i =  cuser.exmailbox + man.mailk + man.keepmail;
     if (i > 1000) i = 1000;
     sprintf(buf, "±zªºªá¶é«H½c¦³ %d (%dk), ­ì¦³ %d ¶×¤J«á¦@¦³ %d\n", 
	    man.keepmail, man.mailk, cuser.exmailbox, i);
     strcat(msg, buf);
     cuser.exmailbox = i;

     if(man.userlevel & PERM_MAILLIMIT)
      {
       sprintf(buf, "¶}±Ò«H½cµL¤W­­\n");
       strcat(msg, buf);
       cuser.userlevel |= PERM_MAILLIMIT;
      }

     if(cuser.firstlogin > man.firstlogin) d = man.firstlogin;
     else  d = cuser.firstlogin;
     sprintf(buf, "ªá¶éµù¥U¤é´Á %s ", Cdatedate(&(man.firstlogin)));
     strcat(msg,buf);
     sprintf(buf, "¦¹±b¸¹µù¥U¤é´Á %s ±N¨ú ",Cdatedate(&(cuser.firstlogin)));
     strcat(msg,buf);
     sprintf(buf, "±N¨ú %s\n", Cdatedate(&d) );
     strcat(msg,buf);
     cuser.firstlogin = d;

     if(cuser.numlogins < man.numlogins) i = man.numlogins;
     else i = cuser.numlogins;

     sprintf(buf, "ªá¶é¶i¯¸¦¸¼Æ %d ¦¹±b¸¹ %d ±N¨ú %d \n", man.numlogins,
	   cuser.numlogins, i);
     strcat(msg,buf);
     cuser.numlogins = i;

     if(cuser.numposts < man.numposts ) i = man.numposts;
     else i = cuser.numposts;
     sprintf(buf, "ªá¶é¤å³¹¦¸¼Æ %d ¦¹±b¸¹ %d ±N¨ú %d\n", 
                 man.numposts,cuser.numposts,i);
     strcat(msg,buf);
     cuser.numposts = i;
     outs(msg);
     while(search_ulistn(usernum,2)) 
        {vmsg("½Ð±N­«ÂÐ¤W¯¸¨ä¥L½uÃö³¬! ¦AÄ~Äò");}
     passwd_update(usernum, &cuser);
   }
   sethomeman(genbuf, cuser.userid);
   mkdir(genbuf, 0600);
   sprintf(buf, "tar zxvf home/%c/%s.tgz>/dev/null",
	   userid[0], userid);
   chdir("fpg");
   system(buf);
   chdir(BBSHOME);

   if (getans("¬O§_¶×¤J­Ó¤H«H½c? (Y/n)")!='n')
    {
	sethomedir(buf, cuser.userid);
	sprintf(genbuf, "fpg/home/bbs/home/%c/%s/.DIR",
		userid[0], userid);
	merge_dir(buf, genbuf, 1);
        strcat(msg, "¶×¤J­Ó¤H«H½c\n");
    }
   if(getans("¬O§_¶×¤J­Ó¤H«H½cºëµØ°Ï? (·|ÂÐ»\\²{¦³³]©w) (y/N)")=='y')
   {
        sprintf(buf,
	   "rm -rd home/%c/%s/man>/dev/null ; mv fpg/home/bbs/home/%c/%s/man home/%c/%s", 
              cuser.userid[0], cuser.userid,
	      userid[0], userid,
	      cuser.userid[0], cuser.userid);
        system(buf);
        strcat(msg, "¶×¤J­Ó¤H«H½cºëµØ°Ï\n");
   }
   if(getans("¬O§_¶×¤J¦n¤Í¦W³æ? (·|ÂÐ»\\²{¦³³]©w, ID¥i¯à¬O¤£¦P¤H)? (y/N)")=='y')
   {
       sethomefile(genbuf, cuser.userid, "overrides");
       sprintf(buf, "fpg/home/bbs/home/%c/%s/overrides",userid[0],userid);
       Copy(buf, genbuf);
       strcat(buf, genbuf);
       friend_load(FRIEND_OVERRIDE);
       strcat(msg, "¶×¤J¦n¦³¤ÍÍ³ææ\n");
   }
   sprintf(buf, "±b¸¹¶×¤J³ø§i %s -> %s ", userid, cuser.userid);
   post_msg("Security", buf, msg, "[¨t²Î¦w¥þ§½]");
   sprintf(buf, "fpg/home/bbs/home/%c/%s/PttID", userid[0],userid);
   if((fp = fopen(buf, "w")))
     {
        fprintf(fp, "%s\n", cuser.userid);
        fprintf(fp, "%s", msg);
	fclose(fp);
     }

   vmsg("®¥³ß±z§¹¦¨±b¸¹ÅÜ¨­..");
   return 0;
}

void
m_fpg_brd(char *bname, char *fromdir)
{
  char fbname[25], buf[256];
  fileheader_t fh;

  fromdir[0]=0;
  do{

     if(!getdata(20,0, "¤p³½ªºªO¦W [­^¤å¤j¤p¼g­n§¹¥þ¥¿½T]:", fbname, 20,
	        DOECHO)) return;
  }
  while(invalid_brdname(fbname));

  sprintf(buf, "fpg/boards/%s.inf", fbname);
  if(!dashf(buf))
  {
       vmsg("µL¦¹¬ÝªO");
       return;
  }
  chdir("fpg");
  sprintf(buf, "tar zxf boards/%s.tgz >/dev/null",fbname);
  system(buf);
  sprintf(buf, "tar zxf boards/%s.man.tgz >/dev/null", fbname);
  system(buf);
  chdir(BBSHOME);
  sprintf(buf, "mv fpg/home/bbs/man/boards/%s man/boards/%c/%s", fbname,
	    bname[0], bname);
  system(buf);
  sprintf(fh.title, "¡» %s ºëµØ°Ï", fbname);
  sprintf(fh.filename, fbname);
  sprintf(fh.owner, cuser.userid);
  sprintf(buf, "man/boards/%c/%s/.DIR", bname[0], bname);
  append_record(buf, &fh, sizeof(fh));
  sprintf(fromdir, "fpg/home/bbs/boards/%s/.DIR", fbname);
  vmsg("§Y±N¶×¤J %s ª©¸ê®Æ..«öÁä«á»Ý­n¤@ÂI®É¶¡",fbname);
}
