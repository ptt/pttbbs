/* $Id: merge.c 2060 2004-06-11 17:18:06Z Ptt $ */
#define _XOPEN_SOURCE
#define _ISOC99_SOURCE
/* this is a interface provided when we merge BBS */ 
#include "bbs.h"
#include "fpg.h"

int
m_sob()
{
    char genbuf[256], buf[256], userid[25], passbuf[24], msg[2048]="";
    int count=0, i, isimported=0, corrected;
    FILE *fp;
    sobuserec man;
    time_t d;

    clear();
    move(1,0);

    outs(
 "   ½Ðª`·N ³o¬O¥uµ¹¶§¥ú¨FÅy¨Ï¥ÎªÌ!\n"
 "      Åý¨FÅyªº¨Ï¥ÎªÌÂà²¾­Ó¤H¸ê²£¥H¤Î­«­n«H¥Î¸ê®Æ, ¨É¦³¥­µ¥¦w¥þªºÀô¹Ò.\n"
 "      ¦pªG±z¤£»Ý­n, ½Ðª½Â÷¶}.\n"
 "    -----------------------------------------------------------------\n"
 "    ¯S§O¥mÀ{:\n"
 "      ¬°¤F±b¸¹¦w¥þ,±z¥u¦³³sÄò¤Q¦¸±K½X¿ù»~ªº¾÷·|,½Ð¤p¤ß¿é¤J.\n"
 "      ³sÄò¦¸¿ù»~±zªºÅÜ¨­¥\\¯à´N·|³Q¶}»@³æ¨Ãª½±µ³qª¾¯¸ªø.\n"
 "      ½Ð¤£­n¦bÅÜ¨­¹Lµ{¤¤¤£¥¿±`Â_½u, ¨è·NÂ_½uÅÜ¥bÃ~¤H¯¸ªø¤£±Ï­ò.\n"
	);

   if(getkey("¬O§_­nÄ~Äò?(y/N)")!='y') return 0;
   if(search_ulistn(usernum,2)) 
        {vmsg("½Ðµn¥X¨ä¥Lµøµ¡, ¥H§KÅÜ¨­¥¢±Ñ"); return 0;}
   do
   {
    if(!getdata(10,0, "      ¨FÅyªºID [¤j¤p¼g­n§¹¥þ¥¿½T]:", userid, 20,
	       DOECHO)) return 0;
    if(bad_user_id(userid)) continue;
    sprintf(genbuf, "sob/passwd/%c/%s.inf",userid[0], userid);
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
    if(!getdata(11,0, "      ¨FÅyªº±K½X:", passbuf, sizeof(passbuf), 
		  NOECHO)) return 0;
    if(++count>=10)
    {
          cuser.userlevel |= PERM_VIOLATELAW;
          cuser.vl_count++;
	  passwd_update(usernum, &cuser);
          post_violatelaw(cuser.userid, "[PTTÄµ¹î]", "´ú¸Õ±b¸¹¿ù»~¤Q¦¸",
		          "¹HªkÆ[¹î");
          mail_violatelaw(cuser.userid, "[PTTÄµ¹î]", "´ú¸Õ±b¸¹¿ù»~¤Q¦¸",
		          "¹HªkÆ[¹î");

          return 0;
    }
    if(!(corrected = checkpasswd(man.passwd, passbuf)))
       vmsg("±K½X¿ù¿ùù»~"); 
   } while(!corrected);
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

   reload_money(); 

   sprintf(buf, 
           "±zªº¨FÅyÆxÄMÁ³ %10d ´«ºâ¦¨ Ptt ¹ô¬° %9d (¶×²v 22:1), \n"
           "    ¨FÅy¨©´ß¦³ %10d ´«ºâ¬° Ptt ¹ô¬° %9d (¶×²v 222105:1), \n"
           "    ­ì¦³P¹ô  %10d ¶×¤J«á¦@¦³ %d\n",
            man.goldmoney, man.goldmoney/22, 
            man.silvermoney, man.silvermoney/222105,
            cuser.money, cuser.money + man.goldmoney/22 +
             man.silvermoney/222105);
   demoney(man.goldmoney/22 + man.silvermoney/222105 );
   strcat(msg, buf); 
#endif

     i =  cuser.exmailbox + man.exmailbox + man.exmailboxk/2000;
     if (i > MAX_EXKEEPMAIL) i = MAX_EXKEEPMAIL;
     sprintf(buf, "±zªº¨FÅy«H½c¦³ %d (%dk), ­ì¦³ %d ¶×¤J«á¦@¦³ %d\n", 
	    man.exmailbox, man.exmailboxk, cuser.exmailbox, i);
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
     cuser.firstlogin = d;

     if(cuser.numlogins < man.numlogins) i = man.numlogins;
     else i = cuser.numlogins;

     sprintf(buf, "¨FÅy¶i¯¸¦¸¼Æ %d ¦¹±b¸¹ %d ±N¨ú %d \n", man.numlogins,
	   cuser.numlogins, i);
     strcat(msg,buf);
     cuser.numlogins = i;

     if(cuser.numposts < man.numposts ) i = man.numposts;
     else i = cuser.numposts;
     sprintf(buf, "¨FÅy¤å³¹¦¸¼Æ %d ¦¹±b¸¹ %d ±N¨ú %d\n", 
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
   sprintf(buf, "tar zxvf %c/%s.tar.gz>/dev/null",
	   userid[0], userid);
   chdir("sob/home");
   system(buf);
   chdir(BBSHOME);

   if (getans("¬O§_¶×¤J­Ó¤H«H½c? (Y/n)")!='n')
    {
	sethomedir(buf, cuser.userid);
	sprintf(genbuf, "sob/home/%c/%s/.DIR",
		userid[0], userid);
	merge_dir(buf, genbuf, 1);
        strcat(msg, "¶×¤J­Ó¤H«H½c\n");
    }
   if(getans("¬O§_¶×¤J­Ó¤H«H½cºëµØ°Ï(­Ó¤H§@«~¶°)? (·|ÂÐ»\\²{¦³³]©w) (y/N)")=='y')
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
        strcat(msg, "¶×¤J­Ó¤H«H½cºëµØ°Ï(­Ó¤H§@«~¶°)\n");
        sprintf(buf,"home/%c/%s/man/gem", cuser.userid[0], cuser.userid);
        if(dashd(buf))
         {
          strcat(fh.title, "¡» ­Ó¤H§@«~¶°");
          strcat(fh.filename, "gem");
          sprintf(fh.owner, cuser.userid);
          sprintf(buf, "home/%c/%s/man/.DIR", cuser.userid[0], cuser.userid);
          append_record(buf, &fh, sizeof(fh));
         }
   }
   if(getans("¬O§_¶×¤J¦n¤Í¦W³æ? (·|ÂÐ»\\²{¦³³]©w, ID¥i¯à¬O¤£¦P¤H)? (y/N)")=='y')
   {
       sethomefile(genbuf, cuser.userid, "overrides");
       sprintf(buf, "sob/home/%c/%s/overrides",userid[0],userid);
       Copy(buf, genbuf);
       strcat(buf, genbuf);
       friend_load(FRIEND_OVERRIDE);
       strcat(msg, "¶×¤J¦n¤Í¦W³æ\n");
   }
   sprintf(buf, "±b¸¹¶×¤J³ø§i %s -> %s ", userid, cuser.userid);
   post_msg("Security", buf, msg, "[¨t²Î¦w¥þ§½]");

   vmsg("®¥³ß±z§¹¦¨±b¸¹ÅÜ¨­..");
   return 0;
}

void
m_sob_brd(char *bname, char *fromdir)
{
  char fbname[25], buf[256];
  fileheader_t fh;

  fromdir[0]=0;
  do{

     if(!getdata(20,0, "SOBªºªO¦W [­^¤å¤j¤p¼g­n§¹¥þ¥¿½T]:", fbname, 20,
	        DOECHO)) return;
  }
  while((invalid_brdname(fbname)&1));

  sprintf(buf, "sob/man/%s.tar.gz", fbname);
  if(!dashf(buf))
  {
       vmsg("µL¦¹¬ÝªO");
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
  sprintf(fh.title, "¡» %s ºëµØ°Ï", fbname);
  sprintf(fh.filename, fbname);
  sprintf(fh.owner, cuser.userid);
  sprintf(buf, "man/boards/%c/%s/.DIR", bname[0], bname);
  append_record(buf, &fh, sizeof(fh));
  sprintf(fromdir, "sob/boards/%s/.DIR", fbname);
  vmsg("§Y±N¶×¤J %s ª©¸ê®Æ..«öÁä«á»Ý­n¤@ÂI®É¶¡",fbname);
}
