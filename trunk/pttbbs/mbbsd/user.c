/* $Id: user.c,v 1.2 2002/03/09 17:27:57 in2 Exp $ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include "config.h"
#include "pttstruct.h"
#include "common.h"
#include "perm.h"
#include "modes.h"
#include "proto.h"

extern int numboards;
extern boardheader_t *bcache;
extern char *loginview_file[NUMVIEWFILE][2];
extern int b_lines;             /* Screen bottom line number: t_lines-1 */
extern time_t login_start_time;
extern char *msg_uid;
extern int usernum;
extern char *msg_sure_ny;
extern userinfo_t *currutmp;
extern int showansi;
extern char reset_color[];
extern char *fn_proverb;
extern char *fn_plans;
extern char *msg_del_ok;
extern char *fn_register;
extern char *msg_nobody;
extern userec_t cuser;
extern userec_t xuser;

static char *sex[8] = {
    MSG_BIG_BOY, MSG_BIG_GIRL, MSG_LITTLE_BOY, MSG_LITTLE_GIRL,
    MSG_MAN, MSG_WOMAN, MSG_PLANT, MSG_MIME
};

int u_loginview() {
    int i;
    unsigned int pbits = cuser.loginview;
    char choice[5];
    
    clear();
    move(4,0);
    for(i = 0; i < NUMVIEWFILE ; i++)
	prints("    %c. %-20s %-15s \n", 'A' + i,
	       loginview_file[i][1],((pbits >> i) & 1 ? "ˇ" : "Ｘ"));
    
    clrtobot();
    while(getdata(b_lines - 1, 0, "請按 [A-N] 切換設定，按 [Return] 結束：",
		  choice, 3, LCECHO)) {
	i = choice[0] - 'a';
	if(i >= NUMVIEWFILE || i < 0)
	    bell();
	else {
	    pbits ^= (1 << i);
	    move( i + 4 , 28 );
	    prints((pbits >> i) & 1 ? "ˇ" : "Ｘ");
	}
    }
    
    if(pbits != cuser.loginview) {
	cuser.loginview = pbits ;
	passwd_update(usernum, &cuser);
    }
    return 0;
}

void user_display(userec_t *u, int real) {
    int diff = 0;
    char genbuf[200];
    
    clrtobot();
    prints(
	   "        \033[30;41m┴┬┴┬┴┬\033[m  \033[1;30;45m    使 用 者"
	   " 資 料        "
	   "     \033[m  \033[30;41m┴┬┴┬┴┬\033[m\n");
    prints("                代號暱稱: %s(%s)\n"
	   "                真實姓名: %s\n"
	   "                居住住址: %s\n"
	   "                電子信箱: %s\n"
	   "                性    別: %s\n"
	   "                銀行帳戶: %ld 銀兩\n",
	   u->userid, u->username, u->realname, u->address, u->email,
	   sex[u->sex % 8], u->money);
    
    sethomedir(genbuf, u->userid);
    prints("                私人信箱: %d 封  (購買信箱: %d 封)\n"
	   "                身分證號: %s\n"
	   "                手機號碼: %010d\n"
	   "                生    日: %02i/%02i/%02i\n"
	   "                小雞名字: %s\n",
	   get_num_records(genbuf, sizeof(fileheader_t)),
	   u->exmailbox, u->ident, u->mobile,
	   u->month, u->day, u->year % 100, u->mychicken.name);
    prints("                註冊日期: %s", ctime(&u->firstlogin));
    prints("                前次光臨: %s", ctime(&u->lastlogin));
    prints("                前次點歌: %s", ctime(&u->lastsong));
    prints("                上站文章: %d 次 / %d 篇\n",
	   u->numlogins, u->numposts);

    if(real) {
	strcpy(genbuf, "bTCPRp#@XWBA#VSM0123456789ABCDEF");
	for(diff = 0; diff < 32; diff++)
	    if(!(u->userlevel & (1 << diff)))
		genbuf[diff] = '-';
	prints("                認證資料: %s\n"
	       "                user權限: %s\n",
	       u->justify, genbuf);
    } else {
	diff = (time(0) - login_start_time) / 60;
	prints("                停留期間: %d 小時 %2d 分\n",
	       diff / 60, diff % 60);
    }
    
    /* Thor: 想看看這個 user 是那些版的版主 */
    if(u->userlevel >= PERM_BM) {
	int i;
	boardheader_t *bhdr;
	
	outs("                擔任板主: ");
	
	for(i = 0, bhdr = bcache; i < numboards; i++, bhdr++) {
	    if(is_uBM(bhdr->BM,u->userid)) {
		outs(bhdr->brdname);
		outc(' ');
	    }
	}
	outc('\n');
    }
    outs("        \033[30;41m┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴"
	 "┬┴┬┴┬┴┬\033[m");
    
    outs((u->userlevel & PERM_LOGINOK) ?
	 "\n您的註冊程序已經完成，歡迎加入本站" :
	 "\n如果要提昇權限，請參考本站公佈欄辦理註冊");
    
#ifdef NEWUSER_LIMIT
    if((u->lastlogin - u->firstlogin < 3 * 86400) && !HAS_PERM(PERM_POST))
	outs("\n新手上路，三天後開放權限");
#endif
}

void mail_violatelaw(char* crime, char* police, char* reason, char* result){
    char genbuf[200];
    fileheader_t fhdr;
    time_t now;
    FILE *fp;            
    sprintf(genbuf, "home/%c/%s", crime[0], crime);
    stampfile(genbuf, &fhdr);
    if(!(fp = fopen(genbuf,"w")))
        return;
    now = time(NULL);
    fprintf(fp, "作者: [Ptt法院]\n"
	    "標題: [報告] 違法判決報告\n"
	    "時間: %s\n"
	    "\033[1;32m%s\033[m判決：\n     \033[1;32m%s\033[m"
	    "因\033[1;35m%s\033[m行為，\n違反本站站規，處以\033[1;35m%s\033[m，特此通知"
	    "\n請到 PttLaw 查詢相關法規資訊，並到 Play-Pay-ViolateLaw 繳交罰單",
	    ctime(&now), police, crime, reason, result);
    fclose(fp);
    sprintf(fhdr.title, "[報告] 違法判決報告");
    strcpy(fhdr.owner, "[Ptt法院]");
    sprintf(genbuf, "home/%c/%s/.DIR", crime[0], crime);
    append_record(genbuf, &fhdr, sizeof(fhdr));
}

static void violate_law(userec_t *u, int unum){
    char ans[4], ans2[4];
    char reason[128];
    move(1,0);
    clrtobot();
    move(2,0);
    prints("(1)Cross-post (2)亂發廣告信 (3)亂發連鎖信\n");
    prints("(4)騷擾站上使用者 (8)其他以罰單處置行為\n(9)砍 id 行為\n");
    getdata(5, 0, "(0)結束",
            ans, 3, DOECHO);
    switch(ans[0]){
    case '1':
	sprintf(reason, "%s", "Cross-post");
	break;
    case '2':
	sprintf(reason, "%s", "亂發廣告信");
	break;
    case '3':
	sprintf(reason, "%s", "亂發連鎖信");
	break;
    case '4':
        while(!getdata(7, 0, "請輸入被檢舉理由以示負責：", reason, 50, DOECHO));
        strcat(reason, "[騷擾站上使用者]");     
        break;
    case '8':   
    case '9':
        while(!getdata(6, 0, "請輸入理由以示負責：", reason, 50, DOECHO));        
        break;
    default:
	return;
    }
    getdata(7, 0, msg_sure_ny, ans2, 3, LCECHO);
    if(*ans2 != 'y')
	return;      
    if (ans[0]=='9'){
	char src[STRLEN], dst[STRLEN];
	sprintf(src, "home/%c/%s", u->userid[0], u->userid);
	sprintf(dst, "tmp/%s", u->userid);
	Rename(src, dst);
	log_usies("KILL", u->userid);
	post_violatelaw(u->userid, cuser.userid, reason, "砍除 ID");       
	u->userid[0] = '\0';
	setuserid(unum, u->userid);
	passwd_update(unum, u);
    }
    else{
        u->userlevel |= PERM_VIOLATELAW;
        u->vl_count ++;
	passwd_update(unum, u);
        post_violatelaw(u->userid, cuser.userid, reason, "罰單處份");
        mail_violatelaw(u->userid, cuser.userid, reason, "罰單處份");
    }                 
    pressanykey();
}

extern char* str_permid[];

void uinfo_query(userec_t *u, int real, int unum) {
    userec_t x;
    register int i = 0, fail, mail_changed;
    char ans[4], buf[STRLEN], *p;
    char genbuf[200], reason[50];
    unsigned long int money = 0;
    fileheader_t fhdr;
    int flag = 0, temp = 0, money_change = 0;
    time_t now;
    
    FILE *fp;
    
    fail = mail_changed = 0;
    
    memcpy(&x, u, sizeof(userec_t));
    getdata(b_lines - 1, 0, real ?
	    "(1)改資料(2)設密碼(3)設權限(4)砍帳號(5)改ID"
	    "(6)殺/復活寵物(7)審判 [0]結束 " :
	    "請選擇 (1)修改資料 (2)設定密碼 ==> [0]結束 ",
	    ans, 3, DOECHO);
    
    if(ans[0] > '2' && !real)
	ans[0] = '0';
    
    if(ans[0] == '1' || ans[0] == '3') {
	clear();
	i = 2;
	move(i++, 0);
	outs(msg_uid);
	outs(x.userid);
    }
    
    switch(ans[0]) {
    case '7':
	violate_law(&x, unum);
	return;
    case '1':
	move(0, 0);
	outs("請逐項修改。");
	
	getdata_buf(i++, 0," 暱 稱  ：",x.username, 24, DOECHO);
	if(real) {
	    getdata_buf(i++, 0, "真實姓名：", x.realname, 20, DOECHO);
	    getdata_buf(i++, 0, "身分證號：", x.ident, 11, DOECHO);
	    getdata_buf(i++, 0, "居住地址：", x.address, 50, DOECHO);
	}
	sprintf(buf, "%010d", x.mobile);
	getdata_buf(i++, 0, "手機號碼：", buf, 11, LCECHO);
        x.mobile=atoi(buf);
	getdata_str(i++, 0, "電子信箱[變動要重新認證]：", buf, 50, DOECHO,
		    x.email);
	if(strcmp(buf,x.email) && strchr(buf, '@')) {
	    strcpy(x.email,buf);
	    mail_changed = 1 - real;
	}
	
	sprintf(genbuf, "%i", (u->sex + 1) % 8);
	getdata_str(i++, 0, "性別 (1)葛格 (2)姐接 (3)底迪 (4)美眉 (5)薯叔 "
		    "(6)阿姨 (7)植物 (8)礦物：",
		    buf, 3, DOECHO,genbuf);
	if(buf[0] >= '1' && buf[0] <= '8')
	    x.sex = (buf[0] - '1') % 8;
	else
	    x.sex = u->sex % 8;
	
	while(1) {
	    int len;
	    
	    sprintf(genbuf, "%02i/%02i/%02i",
		    u->month, u->day, u->year % 100);
	    len = getdata_str(i, 0, "生日 月月/日日/西元：", buf, 9,
			      DOECHO,genbuf);
	    if(len && len != 8)
		continue;
	    if(!len) {
		x.month = u->month;
		x.day = u->day;
		x.year = u->year;
	    } else if(len == 8) {
		x.month = (buf[0] - '0') * 10 + (buf[1] - '0');
		x.day   = (buf[3] - '0') * 10 + (buf[4] - '0');
		x.year  = (buf[6] - '0') * 10 + (buf[7] - '0');
	    } else
		continue;
	    if(!real && (x.month > 12 || x.month < 1 || x.day > 31 ||
			 x.day < 1 || x.year > 90 || x.year < 40))
		continue;
	    i++;
	    break;
	}
	if(real) {
	    unsigned long int l;
	    if(HAS_PERM(PERM_BBSADM)) {
		sprintf(genbuf, "%d", x.money);
		if(getdata_str(i++, 0,"銀行帳戶：", buf, 10, DOECHO,genbuf))
		    if((l = atol(buf)) >= 0) {
			if(l != x.money) {
			    money_change = 1;
			    money = x.money;
			    x.money = l;
			}
		    }
	    }
	    sprintf(genbuf, "%d", x.exmailbox);
	    if(getdata_str(i++, 0,"購買信箱數：", buf, 4, DOECHO,genbuf))
		if((l = atol(buf)) >= 0)
		    x.exmailbox = (int)l;
	    
	    getdata_buf(i++, 0, "認證資料：", x.justify, 44, DOECHO);
	    getdata_buf(i++, 0, "最近光臨機器：", x.lasthost, 16, DOECHO);
	    
	    sprintf(genbuf, "%d", x.numlogins);
	    if(getdata_str(i++, 0,"上線次數：", buf, 10, DOECHO,genbuf))
		if((fail = atoi(buf)) >= 0)
		    x.numlogins = fail;
	    
	    sprintf(genbuf,"%d", u->numposts);
	    if(getdata_str(i++, 0, "文章數目：", buf, 10, DOECHO,genbuf))
		if((fail = atoi(buf)) >= 0)
		    x.numposts = fail;
	    sprintf(genbuf, "%d", u->vl_count);
	    if (getdata_str(i++, 0, "違法記錄：", buf, 10, DOECHO, genbuf))
		if ((fail = atoi(buf)) >= 0)
		    x.vl_count = fail;
	    
	    sprintf(genbuf, "%d/%d/%d", u->five_win, u->five_lose,
		    u->five_tie);
	    if(getdata_str(i++, 0, "五子棋戰績 勝/敗/和：", buf, 16, DOECHO,
			   genbuf))
		while(1) {
		    p = strtok(buf, "/\r\n");
		    if(!p) break;
		    x.five_win = atoi(p);
		    p = strtok(NULL, "/\r\n");
		    if(!p) break;
		    x.five_lose = atoi(p);
		    p = strtok(NULL, "/\r\n");
		    if(!p) break;
		    x.five_tie = atoi(p);
		    break;
		}
	    sprintf(genbuf, "%d/%d/%d", u->chc_win, u->chc_lose, u->chc_tie);
	    if(getdata_str(i++, 0, "象棋戰績 勝/敗/和：", buf, 16, DOECHO,
			   genbuf))
		while(1) {
		    p = strtok(buf, "/\r\n");
		    if(!p) break;
		    x.chc_win = atoi(p);
		    p = strtok(NULL, "/\r\n");
		    if(!p) break;
		    x.chc_lose = atoi(p);
		    p = strtok(NULL, "/\r\n");
		    if(!p) break;
		    x.chc_tie = atoi(p);
		    break;
		}
	    fail = 0;
	}
	break;
	
    case '2':
	i = 19;
	if(!real) {
	    if(!getdata(i++, 0, "請輸入原密碼：", buf, PASSLEN, NOECHO) ||
	       !checkpasswd(u->passwd, buf)) {
		outs("\n\n您輸入的密碼不正確\n");
		fail++;
		break;
	    }
	}
	else{
	    char witness[3][32];
	    time_t now = time(NULL);
	    for(i=0;i<3;i++){
		if(!getdata(19+i, 0, "請輸入協助證明之使用者：", witness[i], 32, DOECHO)){
		    outs("\n不輸入則無法更改\n");
		    fail++;
		    break;
		}
		else if (!getuser(witness[i])){
		    outs("\n查無此使用者\n");
		    fail++;
		    break;
		}
		else if (now - xuser.firstlogin < 6*30*24*60*60){
		    outs("\n註冊未超過半年，請重新輸入\n");
		    i--;
		}
	    }
	    if (i < 3)
		break;
	    else
		i = 20;
	}    
	
	if(!getdata(i++, 0, "請設定新密碼：", buf, PASSLEN, NOECHO)) {
	    outs("\n\n密碼設定取消, 繼續使用舊密碼\n");
	    fail++;
	    break;
	}
	strncpy(genbuf, buf, PASSLEN);
	
	getdata(i++, 0, "請檢查新密碼：", buf, PASSLEN, NOECHO);
	if(strncmp(buf, genbuf, PASSLEN)) {
	    outs("\n\n新密碼確認失敗, 無法設定新密碼\n");
	    fail++;
	    break;
	}
	buf[8] = '\0';
	strncpy(x.passwd, genpasswd(buf), PASSLEN);
	if (real)
	    x.userlevel &= (!PERM_LOGINOK);
	break;
	
    case '3':
	i = setperms(x.userlevel, str_permid);
	if(i == x.userlevel)
	    fail++;
	else {
	    flag=1;
	    temp=x.userlevel;
	    x.userlevel = i;
	}
	break;
	
    case '4':
	i = QUIT;
	break;
	
    case '5':
	if (getdata_str(b_lines - 3, 0, "新的使用者代號：", genbuf, IDLEN + 1,
			DOECHO,x.userid)) {
	    if(searchuser(genbuf)) {
		outs("錯誤! 已經有同樣 ID 的使用者");
		fail++;
	    } else
		strcpy(x.userid, genbuf);
	}
	break;
    case '6':
	if(x.mychicken.name[0])
	    x.mychicken.name[0] = 0;
	else
	    strcpy(x.mychicken.name,"[死]");
	break;
    default:
	return;
    }

    if(fail) {
	pressanykey();
	return;
    }
    
    getdata(b_lines - 1, 0, msg_sure_ny, ans, 3, LCECHO);
    if(*ans == 'y') {
	if(flag)
	    post_change_perm(temp,i,cuser.userid,x.userid);
	if(strcmp(u->userid, x.userid)) {
	    char src[STRLEN], dst[STRLEN];
	    
	    sethomepath(src, u->userid);
	    sethomepath(dst, x.userid);
	    Rename(src, dst);
	    setuserid(unum, x.userid);
	}
	memcpy(u, &x, sizeof(x));
	if(mail_changed) {	
#ifdef EMAIL_JUSTIFY
	    x.userlevel &= ~PERM_LOGINOK;
	    mail_justify();
#endif
	}
	
	if(i == QUIT) {
	    char src[STRLEN], dst[STRLEN];
	    
	    sprintf(src, "home/%c/%s", x.userid[0], x.userid);
	    sprintf(dst, "tmp/%s", x.userid);
	    if(Rename(src, dst)) {
		sprintf(genbuf, "/bin/rm -fr %s >/dev/null 2>&1", src);
/* do not remove
   system(genbuf);
*/
	    }	
	    log_usies("KILL", x.userid);
	    x.userid[0] = '\0';
	    setuserid(unum, x.userid);
	} else
	    log_usies("SetUser", x.userid);
        if(money_change)
              	setumoney(unum,x.money);
	passwd_update(unum, &x);
	now = time(0);
	if(money_change) {
	    strcpy(genbuf, "boards/S/Security");
	    stampfile(genbuf, &fhdr);	
	    if(!(fp = fopen(genbuf,"w")))
		return;
	    
	    now = time(NULL);
	    fprintf(fp, "作者: [系統安全局] 看板: Security\n"
		    "標題: [公安報告] 站長修改金錢報告\n"
		    "時間: %s\n"
		    "   站長\033[1;32m%s\033[m把\033[1;32m%s\033[m"
		    "的錢從\033[1;35m%ld\033[m改成\033[1;35m%d\033[m",
		    ctime(&now), cuser.userid, x.userid, money, x.money);
	    
	    clrtobot ();
	    clear();
	    while(!getdata(5, 0, "請輸入理由以示負責：", reason, 60, DOECHO));
	    
	    fprintf(fp, "\n   \033[1;37m站長%s修改錢理由是：%s\033[m",
		    cuser.userid, reason);
	    fclose(fp);
	    sprintf(fhdr.title, "[公安報告] 站長%s修改%s錢報告", cuser.userid,
		    x.userid);
	    strcpy(fhdr.owner, "[系統安全局]");
	    append_record("boards/S/Security/.DIR", &fhdr, sizeof(fhdr));
	}
    }
}

int u_info() {
    move(2, 0);
    user_display(&cuser, 0);
    uinfo_query(&cuser, 0, usernum);
    strcpy(currutmp->realname, cuser.realname);
    strcpy(currutmp->username, cuser.username);
    return 0;
}

int u_ansi() {
    showansi ^= 1;
    cuser.uflag ^= COLOR_FLAG;
    outs(reset_color);
    return 0;
}

int u_cloak() {
    outs((currutmp->invisible ^= 1) ? MSG_CLOAKED : MSG_UNCLOAK);
    return XEASY;
}

int u_switchproverb() {
/*  char *state[4]={"用功\型","安逸型","自定型","SHUTUP"}; */
    char buf[100];

    cuser.proverb =(cuser.proverb +1) %4;
    setuserfile(buf,fn_proverb);
    if(cuser.proverb==2 && dashd(buf)) {
	FILE *fp = fopen(buf,"a");
	
	fprintf(fp,"座右銘狀態為[自定型]要記得設座右銘的內容唷!!");
	fclose(fp);
    }
    passwd_update(usernum, &cuser);
    return 0;
}

int u_editproverb() {
    char buf[100];
  
    setutmpmode(PROVERB);
    setuserfile(buf,fn_proverb);
    move(1,0);
    clrtobot();
    outs("\n\n 請一行一行依序鍵入想系統提醒你的內容,\n"
	 " 儲存後記得把狀態設為 [自定型] 才有作用\n"
	 " 座右銘最多100條");
    pressanykey();
    vedit(buf,NA, NULL);
    return 0;
}

void showplans(char *uid) {
    char genbuf[200];
    
    sethomefile(genbuf, uid, fn_plans);
    if(!show_file(genbuf, 7, MAX_QUERYLINES, ONLY_COLOR))
	prints("《個人名片》%s 目前沒有名片", uid);
}

int showsignature(char *fname) {
    FILE *fp;
    char buf[256];
    int i, j;
    char ch;

    clear();
    move(2, 0);
    setuserfile(fname, "sig.0");
    j = strlen(fname) - 1;

    for(ch = '1'; ch <= '9'; ch++) {
	fname[j] = ch;
	if((fp = fopen(fname, "r"))) {
	    prints("\033[36m【 簽名檔.%c 】\033[m\n", ch);
	    for(i = 0; i++ < MAX_SIGLINES && fgets(buf, 256, fp); outs(buf));
	    fclose(fp);
	}
    }
    return j;
}

int u_editsig() {
    int aborted;
    char ans[4];
    int j;
    char genbuf[200];
    
    j = showsignature(genbuf);
    
    getdata(0, 0, "簽名檔 (E)編輯 (D)刪除 (Q)取消？[Q] ", ans, 4, LCECHO);
    
    aborted = 0;
    if(ans[0] == 'd')
	aborted = 1;
    if(ans[0] == 'e')
	aborted = 2;
    
    if(aborted) {
	if(!getdata(1, 0, "請選擇簽名檔(1-9)？[1] ", ans, 4, DOECHO))
	    ans[0] = '1';
	if(ans[0] >= '1' && ans[0] <= '9') {
	    genbuf[j] = ans[0];
	    if(aborted == 1) {
		unlink(genbuf);
		outs(msg_del_ok);
	    } else {
		setutmpmode(EDITSIG);
		aborted = vedit(genbuf, NA, NULL);
		if(aborted != -1)
		    outs("簽名檔更新完畢");
	    }
	}
	pressanykey();
    }
    return 0;
}

int u_editplan() {
    char genbuf[200];
    
    getdata(b_lines - 1, 0, "名片 (D)刪除 (E)編輯 [Q]取消？[Q] ",
	    genbuf, 3, LCECHO);
    
    if(genbuf[0] == 'e') {
	int aborted;
	
	setutmpmode(EDITPLAN);
	setuserfile(genbuf, fn_plans);
	aborted = vedit(genbuf, NA, NULL);
	if(aborted != -1)
	    outs("名片更新完畢");
	pressanykey();
	return 0;
    } else if(genbuf[0] == 'd') {
	setuserfile(genbuf, fn_plans);
	unlink(genbuf);
	outmsg("名片刪除完畢");
    }
    return 0;
}

int u_editcalendar() {
    char genbuf[200];
    
    getdata(b_lines - 1, 0, "行事曆 (D)刪除 (E)編輯 [Q]取消？[Q] ",
	    genbuf, 3, LCECHO);
    
    if(genbuf[0] == 'e') {
	int aborted;
	
	setutmpmode(EDITPLAN);
	setcalfile(genbuf, cuser.userid);
	aborted = vedit(genbuf, NA, NULL);
	if(aborted != -1)
	    outs("行事曆更新完畢");
	pressanykey();
	return 0;
    } else if(genbuf[0] == 'd') {
	setcalfile(genbuf, cuser.userid);
	unlink(genbuf);
	outmsg("行事曆刪除完畢");
    }
    return 0;
}

/* 使用者填寫註冊表格 */
static void getfield(int line, char *info, char *desc, char *buf, int len) {
    char prompt[STRLEN];
    char genbuf[200];

    sprintf(genbuf, "原先設定：%-30.30s (%s)", buf, info);
    move(line, 2);
    outs(genbuf);
    sprintf(prompt, "%s：", desc);
    if(getdata_str(line + 1, 2, prompt, genbuf, len, DOECHO, buf))
	strcpy(buf, genbuf);
    move(line, 2);
    prints("%s：%s", desc, buf);
    clrtoeol();
}

static int removespace(char* s){
    int i, index;

    for(i=0, index=0;s[i];i++){
	if (s[i] != ' ')
	    s[index++] = s[i];
    }
    s[index] = '\0';
    return index;
}

int u_register() {
    char rname[20], addr[50], ident[11], mobile[20];
    char phone[20], career[40], email[50],birthday[9],sex_is[2],year,mon,day;
    char ans[3], *ptr;
    FILE *fn;
    time_t now;
    char genbuf[200];
    
    if(cuser.userlevel & PERM_LOGINOK) {
	outs("您的身份確認已經完成，不需填寫申請表");
	return XEASY;
    }
    if((fn = fopen(fn_register, "r"))) {
	while(fgets(genbuf, STRLEN, fn)) {
	    if((ptr = strchr(genbuf, '\n')))
		*ptr = '\0';
	    if(strncmp(genbuf, "uid: ", 5) == 0 &&
	       strcmp(genbuf + 5, cuser.userid) == 0) {
		fclose(fn);
		outs("您的註冊申請單尚在處理中，請耐心等候");
		return XEASY;
	    }
	}
	fclose(fn);
    }
    
    getdata(b_lines - 1, 0, "您確定要填寫註冊單嗎(Y/N)？[N] ", ans, 3, LCECHO);
    if(ans[0] != 'y')
	return FULLUPDATE;
    
    move(2, 0);
    clrtobot();
    strcpy(ident, cuser.ident);
    strcpy(rname, cuser.realname);
    strcpy(addr, cuser.address);
    strcpy(email, cuser.email);
    sprintf(mobile,"0%09d",cuser.mobile);
    sprintf(birthday, "%02i/%02i/%02i",
	    cuser.month, cuser.day, cuser.year % 100);
    sex_is[0]=(cuser.sex % 8)+'1';sex_is[1]=0;
    career[0] = phone[0] = '\0';
    while(1) {
	clear();
	move(1, 0);
	prints("%s(%s) 您好，請據實填寫以下的資料:",
	       cuser.userid, cuser.username);
        do{
	    getfield(3, "D120908396", "身分證號", ident, 11);
        }while(removespace(ident)<10 || !isalpha(ident[0]));
        do{
	    getfield(5, "請用中文", "真實姓名", rname, 20);
        }while(!removespace(rname) || isalpha(rname[0]));
        do{ 
	    getfield(7, "學校系級或單位職稱", "服務單位", career, 40);
        }while(!removespace(career));
        do{
	    getfield(9, "包括寢室或門牌號碼", "目前住址", addr, 50);
        }while(!(addr[0]));
        do{
	    getfield(11, "包括長途撥號區域碼", "連絡電話", phone, 20);
        }while(!removespace(phone));
	getfield(13, "只輸入數字 如:0912345678", "手機號碼", mobile, 20);
	while(1) {
	    int len;
	    
	    getfield(15,"月月/日日/西元 如:09/27/76","生日",birthday,9);
	    len = strlen(birthday);
	    if(!len) {
		sprintf(birthday, "%02i/%02i/%02i",
			cuser.month, cuser.day, cuser.year % 100);
		mon = cuser.month;
		day = cuser.day;
		year = cuser.year;
	    } else if(len == 8) {
		mon = (birthday[0] - '0') * 10 + (birthday[1] - '0');
		day = (birthday[3] - '0') * 10 + (birthday[4] - '0');
		year = (birthday[6] - '0') * 10 + (birthday[7] - '0');
	    } else
		continue;
	    if(mon > 12 || mon < 1 || day > 31 || day < 1 || year > 90 ||
	       year < 40)
		continue;
	    break;
	}
	getfield(17, "1.葛格 2.姐接 ", "性別", sex_is, 2);
	getfield(19, "身分認證用", "E-Mail Address", email, 50);
	
	getdata(b_lines - 1, 0, "以上資料是否正確(Y/N)？(Q)取消註冊 [N] ",
		ans, 3, LCECHO);
	if(ans[0] == 'q')
	    return 0;
	if(ans[0] == 'y')
	    break;
    }
    strcpy(cuser.ident, ident);
    strcpy(cuser.realname, rname);
    strcpy(cuser.address, addr);
    strcpy(cuser.email, email);
    cuser.mobile = atoi(mobile);
    cuser.sex= (sex_is[0] - '1') % 8;
    cuser.month = mon;
    cuser.day = day;
    cuser.year = year;
    if((fn = fopen(fn_register, "a"))) {
	now = time(NULL);
	trim(career);
	trim(addr);
	trim(phone);
	fprintf(fn, "num: %d, %s", usernum, ctime(&now));
	fprintf(fn, "uid: %s\n", cuser.userid);
	fprintf(fn, "ident: %s\n", ident);
	fprintf(fn, "name: %s\n", rname);
	fprintf(fn, "career: %s\n", career);
	fprintf(fn, "addr: %s\n", addr);
	fprintf(fn, "phone: %s\n", phone);
	fprintf(fn, "mobile: %s\n", mobile);
	fprintf(fn, "email: %s\n", email);
	fprintf(fn, "----\n");
	fclose(fn);
    }
    clear();
    move(9,3);
    prints("最後Post一篇\033[32m自我介紹文章\033[m給大家吧，"
	   "告訴所有老骨頭\033[31m我來啦^$。\\n\n\n\n");
    pressanykey();
    cuser.userlevel |= PERM_POST;
    brc_initial("WhoAmI");
    set_board();
    do_post();
    cuser.userlevel &= ~PERM_POST;
    return 0;
}

/* 列出所有註冊使用者 */
extern struct uhash_t *uhash;
static int usercounter, totalusers, showrealname;
static ushort u_list_special;

static int u_list_CB(userec_t *uentp) {
    static int i;
    char permstr[8], *ptr;
    register int level;
    
    if(uentp == NULL) {
	move(2, 0);
	clrtoeol();
	prints("\033[7m  使用者代號   %-25s   上站  文章  %s  "
	       "最近光臨日期     \033[0m\n",
	       showrealname ? "真實姓名" : "綽號暱稱",
	       HAS_PERM(PERM_SEEULEVELS) ? "等級" : "");
	i = 3;
	return 0;
    }
    
    if(bad_user_id(uentp->userid))
	return 0;
    
    if((uentp->userlevel & ~(u_list_special)) == 0)
	return 0;
    
    if(i == b_lines) {
	prints("\033[34;46m  已顯示 %d/%d 人(%d%%)  \033[31;47m  "
	       "(Space)\033[30m 看下一頁  \033[31m(Q)\033[30m 離開  \033[m",
	       usercounter, totalusers, usercounter * 100 / totalusers);
	i = igetch();
	if(i == 'q' || i == 'Q')
	    return QUIT;
	i = 3;
    }
    if(i == 3) {
	move(3, 0);
	clrtobot();
    }

    level = uentp->userlevel;
    strcpy(permstr, "----");
    if(level & PERM_SYSOP)
	permstr[0] = 'S';
    else if(level & PERM_ACCOUNTS)
	permstr[0] = 'A';
    else if(level & PERM_DENYPOST)
	permstr[0] = 'p';

    if(level & (PERM_BOARD))
	permstr[1] = 'B';
    else if(level & (PERM_BM))
	permstr[1] = 'b';

    if(level & (PERM_XEMPT))
	permstr[2] = 'X';
    else if(level & (PERM_LOGINOK))
	permstr[2] = 'R';

    if(level & (PERM_CLOAK | PERM_SEECLOAK))
	permstr[3] = 'C';
    
    ptr = (char *)Cdate(&uentp->lastlogin);
    ptr[18] = '\0';
    prints("%-14s %-27.27s%5d %5d  %s  %s\n",
	   uentp->userid,
	   showrealname ? uentp->realname : uentp->username,
	   uentp->numlogins, uentp->numposts,
	   HAS_PERM(PERM_SEEULEVELS) ? permstr : "", ptr);
    usercounter++;
    i++;
    return 0;
}

int u_list() {
    char genbuf[3];
    
    setutmpmode(LAUSERS);
    showrealname = u_list_special = usercounter = 0;
    totalusers = uhash->number;
    if(HAS_PERM(PERM_SEEULEVELS)) {
	getdata(b_lines - 1, 0, "觀看 [1]特殊等級 (2)全部？",
		genbuf, 3, DOECHO);
	if(genbuf[0] != '2')
	    u_list_special = PERM_BASIC | PERM_CHAT | PERM_PAGE | PERM_POST | PERM_LOGINOK | PERM_BM;
    }
    if(HAS_PERM(PERM_CHATROOM) || HAS_PERM(PERM_SYSOP)) {
	getdata(b_lines - 1, 0, "顯示 [1]真實姓名 (2)暱稱？",
		genbuf, 3, DOECHO);
	if(genbuf[0] != '2')
	    showrealname = 1;
    }
    u_list_CB(NULL);
    if(passwd_apply(u_list_CB) == -1) {
	outs(msg_nobody);
	return XEASY;
    }
    move(b_lines, 0);
    clrtoeol();
    prints("\033[34;46m  已顯示 %d/%d 的使用者(系統容量無上限)  "
	   "\033[31;47m  (請按任意鍵繼續)  \033[m", usercounter, totalusers);
    egetch();
    return 0;
}
