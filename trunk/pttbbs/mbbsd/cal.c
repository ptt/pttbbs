/* $Id: cal.c,v 1.13 2002/06/04 13:08:33 in2 Exp $ */
#include "bbs.h"

/* 防堵 Multi play */
static int count_multiplay(int unmode) {
    register int i, j;
    register userinfo_t *uentp;

    for(i = j = 0; i < USHM_SIZE; i++) {
	uentp = &(utmpshm->uinfo[i]);
	if(uentp->uid == usernum)
	    if(uentp->lockmode == unmode)
		j++;
    }
    return j;
}

int lockutmpmode(int unmode, int state) {
    int errorno = 0;
    
    if(currutmp->lockmode)
	errorno = 1;
    else if(count_multiplay(unmode))
	errorno = 2;
    
    if(errorno && !(state == LOCK_THIS && errorno == LOCK_MULTI)) {
	clear();
	move(10,20);
	if(errorno == 1)
	    prints("請先離開 %s 才能再 %s ",
		   ModeTypeTable[currutmp->lockmode],
		   ModeTypeTable[unmode]);
	else 
	    prints("抱歉! 您已有其他線相同的ID正在%s",
		   ModeTypeTable[unmode]);
	pressanykey();
	return errorno;
    }
    
    setutmpmode(unmode);
    currutmp->lockmode = unmode;
    return 0;
}

int unlockutmpmode() {
    currutmp->lockmode = 0;
    return 0;
}

/* 使用錢的函數 */
#define VICE_NEW   "vice.new"

/* Heat:發票 */
int vice(int money, char* item) {
    char buf[128];
    unsigned int viceserial=(currutmp->lastact%1000000)*100+rand()%100;
    FILE *fp;
    demoney(-money);
    sprintf(buf, BBSHOME"/home/%c/%s/%s",
	    cuser.userid[0], cuser.userid, VICE_NEW);
    fp = fopen(buf, "a");
    if(!fp )
	{return 0;}
   
    fprintf(fp, "%08d\n", viceserial);
    fclose(fp);
    sprintf(buf, "%s 花了%d$ 編號[%08d]",item, money, viceserial);
    mail_id(cuser.userid, buf, "etc/vice.txt", "Ptt經濟部"); 
    return 0;
}

#define lockreturn(unmode, state) if(lockutmpmode(unmode, state)) return 
#define lockreturn0(unmode, state) if(lockutmpmode(unmode, state)) return 0
#define lockbreak(unmode, state) if(lockutmpmode(unmode, state)) break
#define SONGBOOK  "etc/SONGBOOK"
#define OSONGPATH "etc/SONGO"

static int osong(char *defaultid) {
    char destid[IDLEN + 1],buf[200],genbuf[200],filename[256],say[51];
    char receiver[45],ano[2];
    FILE *fp,*fp1;// *fp2;
    fileheader_t mail;
    int nsongs;
    
    strcpy(buf, Cdatedate(&now));
    
    lockreturn0(OSONG, LOCK_MULTI);
    
    /* Jaky 一人一天點一首 */
    if(!strcmp(buf, Cdatedate(&cuser.lastsong)) && !HAS_PERM(PERM_SYSOP)) {
	move(22,0);
	outs("你今天已經點過囉，明天再點吧....");
	refresh();
	pressanykey();

	unlockutmpmode();
	return 0;
    }

    if(cuser.money < 200) {
	move(22, 0);
	outs("點歌要200銀唷!....");
	refresh();
	pressanykey();
	unlockutmpmode();
	return 0;
    }
    move(12, 0);
    clrtobot();
    sprintf(buf, "親愛的 %s 歡迎來到歐桑自動點歌系統\n", cuser.userid);
    outs(buf);
    trans_buffer[0] = 0;
    if(!defaultid){
	  getdata(13, 0, "要點給誰呢:[可直接按 Enter 先選歌]",
		  destid, sizeof(destid), DOECHO);
	  while (!destid[0]){
	     a_menu("點歌歌本", SONGBOOK,0 );
	     clear();
	     getdata(13, 0, "要點給誰呢:[可按 Enter 重新選歌]",
		     destid, sizeof(destid), DOECHO);
	  }
	}
    else
	strcpy(destid,defaultid);

    /* Heat:點歌者匿名功能 */    
    getdata(14,0, "要匿名嗎?[y/n]:", ano, sizeof(ano), DOECHO);
     
    if(!destid[0]) {
	unlockutmpmode();
	return 0;
    }
    
    getdata_str(14, 0, "想要要對他(她)說..:", say,
		sizeof(say), DOECHO, "我愛妳..");
    sprintf(save_title, "%s:%s", (ano[0]=='y')?"匿名者":cuser.userid, say);
    getdata_str(16, 0, "寄到誰的信箱(可用E-mail)?",
		receiver, sizeof(receiver), LCECHO, destid);
    
    if (!trans_buffer[0]){
       outs("\n接著要選歌囉..進入歌本好好的選一首歌吧..^o^");
       pressanykey();    
       a_menu("點歌歌本", SONGBOOK,0 );
    }
    if(!trans_buffer[0] ||  strstr(trans_buffer, "home") ||
       strstr(trans_buffer, "boards") || !(fp = fopen(trans_buffer, "r"))) {
	unlockutmpmode();
	return 0;
    }
    
    strcpy(filename, OSONGPATH);
    
    stampfile(filename, &mail);
    
    unlink(filename);
    
    if(!(fp1 = fopen(filename, "w"))) {
	fclose(fp);
	unlockutmpmode();
	return 0;
    }
    
    strcpy(mail.owner, "點歌機");
    sprintf(mail.title, "◇ %s 點給 %s ", (ano[0]=='y')?"匿名者":cuser.userid, destid);
    
    while(fgets(buf, 200, fp)) {
	char *po;
	if(!strncmp(buf, "標題: ", 6)) {
	    clear();
	    move(10,10);prints("%s", buf);
	    pressanykey();
	    fclose(fp);
	    unlockutmpmode();
	    return 0;
	}
	while((po = strstr(buf, "<~Src~>"))) {
	    po[0] = 0;
	    sprintf(genbuf,"%s%s%s",buf,(ano[0]=='y')?"匿名者":cuser.userid,po+7);
	    strcpy(buf,genbuf);
        }
	while((po = strstr(buf, "<~Des~>"))) {
	    po[0] = 0;
	    sprintf(genbuf,"%s%s%s",buf,destid,po+7);
	    strcpy(buf,genbuf);
        }
	while((po = strstr(buf, "<~Say~>"))) {
	    po[0] = 0;
	    sprintf(genbuf,"%s%s%s",buf,say,po+7);
	    strcpy(buf,genbuf);
        }
	fputs(buf,fp1);
    }
    fclose(fp1);
    fclose(fp);

//    do_append(OSONGMAIL "/.DIR", &mail2, sizeof(mail2));
    
    if(do_append(OSONGPATH "/.DIR", &mail, sizeof(mail)) != -1) {
	  cuser.lastsong = now;
	/* Jaky 超過 500 首歌就開始砍 */
          nsongs=get_num_records(OSONGPATH "/.DIR", sizeof(mail));
	  if (nsongs > 500){
	    delete_range(OSONGPATH "/.DIR", 1, nsongs-500);
	  }
	/* 把第一首拿掉 */
       vice(200, "點歌");
    }
    sprintf(save_title, "%s:%s", (ano[0]=='y')?"匿名者":cuser.userid, say);
    hold_mail(filename, destid);

    if(receiver[0]) {
#ifndef USE_BSMTP
	bbs_sendmail(filename, save_title, receiver);
#else
	bsmtp(filename, save_title, receiver,0);
#endif
    }
    clear();
    outs(
	"\n\n  恭喜您點歌完成囉..\n"
	"  一小時內動態看板會自動重新更新\n"
	"  大家就可以看到您點的歌囉\n\n"
	"  點歌有任何問題可以到Note板的精華區找答案\n"
	"  也可在Note板精華區看到自己的點歌記錄\n"
	"  有任何保貴的意見也歡迎到Note板留話\n"
	"  讓親切的板主為您服務\n");
    pressanykey();
    sortsong();
    topsong();

    unlockutmpmode();
    return 1;
}

int ordersong() {
    osong(NULL);
    return 0;
}

static int inmailbox(int m) {
    passwd_query(usernum, &xuser);
    cuser.exmailbox = xuser.exmailbox + m;
    passwd_update(usernum, &cuser);
    return cuser.exmailbox;
}


#if !HAVE_FREECLOAK
/* 花錢選單 */
int p_cloak() {
    char buf[4];
    getdata(b_lines-1, 0,
	    currutmp->invisible ? "確定要現身?[y/N]" : "確定要隱身?[y/N]",
	    buf, sizeof(buf), LCECHO);
    if(buf[0] != 'y')
	return 0;
    if(cuser.money >= 19) {
	vice(19, "cloak");
	currutmp->invisible %= 2;
	outs((currutmp->invisible ^= 1) ? MSG_CLOAKED : MSG_UNCLOAK);
	refresh();
	safe_sleep(1);
    }
    return 0;
}
#endif

int p_from() {
    char ans[4];

    getdata(b_lines-2, 0, "確定要改故鄉?[y/N]", ans, sizeof(ans), LCECHO);
    if(ans[0] != 'y')
	return 0;
    reload_money();
    if(cuser.money < 49)
	return 0;
    if(getdata_buf(b_lines-1, 0, "請輸入新故鄉:",
		   currutmp->from, sizeof(currutmp->from), DOECHO)) {
	vice(49,"home");
	currutmp->from_alias=0;
    }
    return 0;
}

int p_exmail() {
    char ans[4],buf[200];
    int n;

    if(cuser.exmailbox >= MAX_EXKEEPMAIL) {
	sprintf(buf,"容量最多增加 %d 封，不能再買了。", MAX_EXKEEPMAIL);
	outs(buf);
	refresh();
	return 0;
    }
    sprintf(buf,"您曾增購 %d 封容量，還要再買多少?",
	    cuser.exmailbox);
    
    getdata_str(b_lines-2, 0, buf, ans, sizeof(ans), LCECHO, "10");
    
    n = atoi(ans);
    if(!ans[0] || !n)
	return 0;
    if(n + cuser.exmailbox > MAX_EXKEEPMAIL)
	n = MAX_EXKEEPMAIL - cuser.exmailbox;
    reload_money();
    if(cuser.money < n * 1000)
	return 0;
    vice(n * 1000, "mail");
    inmailbox(n);
    return 0;
}

void mail_redenvelop(char* from, char* to, int money, char mode){
    char genbuf[200];
    fileheader_t fhdr;
    FILE* fp;
    sprintf(genbuf, "home/%c/%s", to[0], to);
    stampfile(genbuf, &fhdr);
    if (!(fp = fopen(genbuf, "w")))
        return;
    fprintf(fp, "作者: %s\n"
                "標題: 招財進寶\n"
                "時間: %s\n"
                "\033[1;33m親愛的 %s ：\n\n\033[m"
                "\033[1;31m    我包給你一個 %d 元的大紅包喔 ^_^\n\n"
                "    禮輕情意重，請笑納...... ^_^\033[m\n"
                , from, ctime(&now), to, money);
    fclose(fp);
    sprintf(fhdr.title, "招財進寶");
    strcpy(fhdr.owner, from);

    if (mode == 'y')
       vedit(genbuf, NA, NULL);
    sprintf(genbuf, "home/%c/%s/.DIR", to[0], to);       
    append_record(genbuf, &fhdr, sizeof(fhdr));
}

/* 計算贈與稅 */
int give_tax(int money)
{
	int i, tax = 0;
	static int tax_bound[] = { 1000000, 100000, 10000, 1000, 0};
	static double tax_rate[] = { 0.4, 0.3, 0.2, 0.1, 0.08 };
	for( i = 0; i <= 4 ; i++ ) 
		if ( money > tax_bound[i] ) {
			tax += (money - tax_bound[i]) * tax_rate[i];
			money -= (money - tax_bound[i]);
		}
	return (tax <= 0) ? 1 : tax;
}

int p_give() {
    int money, tax;
    char id[IDLEN + 1], genbuf[90];
    
    move(1,0);
    usercomplete("這位幸運兒的id:", id);
    if(!id[0] || !strcmp(cuser.userid,id) ||
       !getdata(2, 0, "要給多少錢:", genbuf, 7, LCECHO))
	return 0;
    money = atoi(genbuf);
    reload_money();
    if(money > 0 && cuser.money >= money ) {
	tax = give_tax(money);
	if ( money - tax <= 0 ) return 0; /* 繳完稅就沒錢給了 */
        deumoney(searchuser(id), money - tax);
	demoney(-money);
	sprintf(genbuf,"%s\t給%s\t%d\t%s", cuser.userid, id, money - tax,
		ctime(&now));
	log_file(FN_MONEY, genbuf);
	genbuf[0] = 'n';
	getdata(3, 0, "要自行書寫紅包袋嗎？[y/N]", genbuf, 2, LCECHO);
	mail_redenvelop(cuser.userid, id, money - tax, genbuf[0]);
    }
    return 0;
}

int p_sysinfo() {
    char buf[100];
    long int total,used;
    float p;
    
    move(b_lines-1,0);
    clrtoeol();
    cpuload(buf);
    outs("CPU 負荷 : ");
    outs(buf);
    
    p = swapused(&total,&used);
    sprintf(buf, " 虛擬記憶體: %.1f%%(全部:%ldMB 用掉:%ldMB)\n",
	    p*100, total >> 20, used >> 20);
    outs(buf);
    pressanykey();
    return 0;
}

/* 小計算機 */
static void ccount(float *a, float b, int cmode) {
    switch(cmode) {
    case 0:
    case 1:
    case 2:
        *a += b;
        break;
    case 3:
        *a -= b;
        break;
    case 4:
        *a *= b;
        break;
    case 5:
        *a /= b;
        break;
    }
}

int cal() {
    float a = 0;
    char flo = 0, ch = 0;
    char mode[6] = {' ','=','+','-','*','/'} , cmode = 0;
    char buf[100] = "[            0] [ ] ", b[20] = "0";
    
    move(b_lines - 1, 0);
    clrtoeol();
    outs(buf);
    move(b_lines, 0);
    clrtoeol();
    outs("\033[44m 小計算機  \033[31;47m      (0123456789+-*/=) "
	 "\033[30m輸入     \033[31m  "
	 "(Q)\033[30m 離開                   \033[m");
    while(1) {
	ch = igetch();
	switch(ch) {
	case '\r':
            ch = '=';
	case '=':
	case '+':
	case '-':
	case '*':
	case '/':
            ccount(&a, atof(b), cmode);
            flo = 0;
            b[0] = '0';
            b[1] = 0;
            move(b_lines - 1, 0);
            sprintf(buf, "[%13.2f] [%c] ", a, ch);
            outs(buf);
            break;
	case '.':
            if(!flo)
		flo = 1;
            else
		break;
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	case '0':
	    if(strlen(b) > 13)
		break;
	    if(flo || b[0] != '0')
		sprintf(b,"%s%c",b,ch);
	    else
		b[0]=ch;
	    move(b_lines - 1, 0);
	    sprintf(buf, "[%13s] [%c]", b, mode[(int)cmode]);
	    outs(buf);
	    break;
	case 'q':
	    return 0;
	}
	
	switch(ch) {
	case '=':
	    a = 0;
	    cmode = 0;
	    break;
	case '+':
	    cmode = 2;
	    break;
	case '-':
	    cmode = 3;
	    break;
	case '*':
	    cmode = 4;
	    break;
	case '/':
	    cmode = 5;
	    break;
	}
    }
}
