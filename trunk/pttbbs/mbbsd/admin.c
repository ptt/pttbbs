/* $Id: admin.c,v 1.7 2002/04/28 19:35:28 in2 Exp $ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "config.h"
#include "pttstruct.h"
#include "common.h"
#include "perm.h"
#include "modes.h"
#include "proto.h"

extern char *msg_uid;
extern userec_t xuser;
extern char *err_uid;
extern boardheader_t *bcache;

/* 使用者管理 */
int m_user() {
    userec_t muser;
    int id;
    char genbuf[200];
    
    stand_title("使用者設定");
    usercomplete(msg_uid, genbuf);
    if(*genbuf) {
	move(2, 0);
	if((id = getuser(genbuf))) {
	    memcpy(&muser, &xuser, sizeof(muser));
	    user_display(&muser, 1);
	    uinfo_query(&muser, 1, id);
	} else {
	    outs(err_uid);
	    clrtoeol();
	    pressanykey();
	}
    }
    return 0;
}

extern int b_lines;

static int search_key_user(char *passwdfile, int mode) {
    userec_t user;
    int ch;
    int coun = 0;
    FILE *fp1 = fopen(passwdfile, "r");
    char buf[128], key[22], genbuf[8];
    
    clear();
    getdata(0, 0, mode ? "請輸入使用者關鍵字[電話|地址|姓名|上站地點|"
	    "email|小雞id] :" : "請輸入id :", key, sizeof(key), DOECHO);
    while((fread(&user, sizeof(user), 1, fp1)) > 0 && coun < MAX_USERS) {
	if(!(++coun & 15)) {
	    move(1, 0);
	    sprintf(buf, "第 [%d] 筆資料\n", coun);
	    outs(buf);
	    refresh();
	}
	if(!strcasecmp(user.userid, key) ||
	   (mode && (
	       strstr(user.realname, key) || strstr(user.username, key) ||
	       strstr(user.lasthost, key) || strstr(user.email, key) ||
	       strstr(user.address, key) || strstr(user.justify, key) ||
	       strstr(user.mychicken.name, key)))) {
	    move(1, 0);
	    sprintf(buf, "第 [%d] 筆資料\n", coun);
	    outs(buf);
	    refresh();
	    
	    user_display(&user, 1);
	    uinfo_query(&user, 1, coun);
	    outs("\033[44m               空白鍵\033[37m:搜尋下一個"
		 "          \033[33m Q\033[37m: 離開");
	    outs(mode ? "                        \033[m " :
		 "      S: 取用備份資料    \033[m ");
	    while(1) {
	    	while((ch = igetch()) == 0);
	    	if(ch == ' ')
	    	    break;
		if(ch == 'q' || ch == 'Q')
		    return 0;
		if(ch == 's' && !mode) {
		    if((ch = searchuser(user.userid))) {
                        setumoney(ch,user.money);
			passwd_update(ch, &user);
			return 0;
		    } else {
			move(b_lines - 1, 0);
			genbuf[0] = 'n';
			getdata(0, 0,
				"目前的 PASSWD 檔沒有此 ID，新增嗎？[y/N]",
				genbuf, 3, LCECHO);
			if(genbuf[0] == 'n') {
			    outs("目前的PASSWDS檔沒有此id "
				 "請先new一個這個id的帳號");
			} else {
			    int allocid = getnewuserid();
			    
			    if(allocid > MAX_USERS || allocid <= 0) {
				fprintf(stderr, "本站人口已達飽和！\n");
				exit(1);
			    }
			    
			    if(passwd_update(allocid, &user) == -1) {
				fprintf(stderr, "客滿了，再見！\n");
				exit(1);
			    }
			    setuserid(allocid, user.userid);
			    if(!searchuser(user.userid)) {
				fprintf(stderr, "無法建立帳號\n");
				exit(1);
			    }
			    return 0;
			}
		    }
		}
	    }
	}
    }
    
    fclose(fp1);
    return 0;
}

/* 以任意 key 尋找使用者 */
int search_user_bypwd() {
    search_key_user(FN_PASSWD, 1);
    return 0;
}

/* 尋找備份的使用者資料 */
int search_user_bybakpwd() {
    char *choice[] = {
	"PASSWDS.NEW1", "PASSWDS.NEW2", "PASSWDS.NEW3",
	"PASSWDS.NEW4", "PASSWDS.NEW5", "PASSWDS.NEW6",
	"PASSWDS.BAK"
    };
    int ch;
    
    clear();
    move(1, 1);
    outs("請輸入你要用來尋找備份的檔案 或按 'q' 離開\n");
    outs(" [\033[1;31m1\033[m]一天前, [\033[1;31m2\033[m]兩天前, "
	 "[\033[1;31m3\033[m]三天前\n");
    outs(" [\033[1;31m4\033[m]四天前, [\033[1;31m5\033[m]五天前, "
	 "[\033[1;31m6\033[m]六天前\n");
    outs(" [7]備份的\n");
    do {
	move(5, 1);
	outs("選擇 => ");
	ch = igetch();
	if(ch == 'q' || ch == 'Q')
	    return 0;
    } while (ch < '1' || ch > '8');
    ch -= '1';
    search_key_user(choice[ch], 0);
    return 0;
}

static void bperm_msg(boardheader_t *board) {
    prints("\n設定 [%s] 看板之(%s)權限：", board->brdname,
	   board->brdattr & BRD_POSTMASK ? "發表" : "閱\讀");
}

extern char* str_permboard[];

unsigned int setperms(unsigned int pbits, char *pstring[]) {
    register int i;
    char choice[4];
    
    move(4, 0);
    for(i = 0; i < NUMPERMS / 2; i++) {
	prints("%c. %-20s %-15s %c. %-20s %s\n",
	       'A' + i, pstring[i],
	       ((pbits >> i) & 1 ? "ˇ" : "Ｘ"),
	       i < 10 ? 'Q' + i : '0' + i - 10,
	       pstring[i + 16],
	       ((pbits >> (i + 16)) & 1 ? "ˇ" : "Ｘ"));
    }
    clrtobot();
    while(getdata(b_lines - 1, 0, "請按 [A-5] 切換設定，按 [Return] 結束：",
		  choice, sizeof(choice), LCECHO)) {
	i = choice[0] - 'a';
	if(i < 0)
	    i = choice[0] - '0' + 26;
	if(i >= NUMPERMS)
	    bell();
	else {
	    pbits ^= (1 << i);
	    move(i % 16 + 4, i <= 15 ? 24 : 64);
	    prints((pbits >> i) & 1 ? "ˇ" : "Ｘ");
	}
    }
    return pbits;
}

/* 自動設立精華區 */
void setup_man(boardheader_t * board) {
    char genbuf[200];
    
    setapath(genbuf, board->brdname);
    mkdir(genbuf, 0755);
}

extern char *fn_board;
extern char *err_bid;
extern userec_t cuser;
extern char *msg_sure_ny;
extern char* str_permid[];

int m_mod_board(char *bname) {
    boardheader_t bh, newbh;
    int bid;
    char genbuf[256], ans[4];
    
    bid = getbnum(bname);
    if(!bid || !bname[0] || get_record(fn_board, &bh, sizeof(bh), bid) == -1) {
	outs(err_bid);
	pressanykey();
	return -1;
    }
    
    prints("看板名稱：%s\n看板說明：%s\n看板bid：%d\n看板GID：%d\n"
	   "板主名單：%s", bh.brdname, bh.title, bid, bh.gid, bh.BM);
    bperm_msg(&bh);
    
    /* Ptt 這邊斷行會檔到下面 */
    move(9, 0);
    sprintf(genbuf, "看板 (E)設定 (V)違法/解除 %s (D)刪除 [Q]取消？",
	    HAS_PERM(PERM_SYSOP) ?
	    " (B)BVote (S)救回文章" : "");
    getdata(10, 0, genbuf, ans, sizeof(ans), LCECHO);
    
    switch(*ans) {
    case 's':
	if(HAS_PERM(PERM_SYSOP) ) {
	    char actionbuf[512];
	    
	    sprintf(actionbuf, BBSHOME "/bin/buildir boards/%c/%s &",
		    bh.brdname[0], bh.brdname);
	    system(actionbuf);
	}
	break;
    case 'b':
	if(HAS_PERM(PERM_SYSOP)) {
	    char bvotebuf[10];
	    
	    memcpy(&newbh, &bh, sizeof(bh));
	    sprintf(bvotebuf, "%d", newbh.bvote);
	    move(20, 0);
	    prints("看版 %s 原來的 BVote：%d", bh.brdname, bh.bvote);
	    getdata_str(21, 0, "新的 Bvote：", genbuf, 5, LCECHO, bvotebuf);
	    newbh.bvote = atoi(genbuf);
	    substitute_record(fn_board, &newbh, sizeof(newbh), bid);
	    reset_board(bid);
	    log_usies("SetBoardBvote", newbh.brdname);
	    break;
	} else
	    break;
    case 'v':
	memcpy(&newbh, &bh, sizeof(bh));
	outs("看版目前為");
	outs((bh.brdattr & BRD_BAD) ? "違法" : "正常");
	getdata(21, 0, "確定更改？", genbuf, 5, LCECHO);
	if(genbuf[0] == 'y') {
	    if(newbh.brdattr & BRD_BAD)
		newbh.brdattr = newbh.brdattr & (!BRD_BAD);
	    else
		newbh.brdattr = newbh.brdattr | BRD_BAD;
	    substitute_record(fn_board, &newbh, sizeof(newbh), bid);
	    reset_board(bid);
	    log_usies("ViolateLawSet", newbh.brdname);
	}
	break;
    case 'd':
	    getdata_str(9, 0, msg_sure_ny, genbuf, 3, LCECHO, "N");
	    if(genbuf[0] != 'y' || !bname[0])
		outs(MSG_DEL_CANCEL);
	    else {
		strcpy(bname, bh.brdname);
		sprintf(genbuf,
			"/bin/tar zcvf tmp/board_%s.tgz boards/%c/%s man/boards/%c/%s >/dev/null 2>&1;"
			"/bin/rm -fr boards/%c/%s man/boards/%c/%s", 
			bname, bname[0], bname, bname[0],
			bname, bname[0], bname, bname[0], bname);
		system(genbuf);
		memset(&bh, 0, sizeof(bh));
		sprintf(bh.title, "%s 看板 %s 刪除", bname, cuser.userid);
		post_msg("Security", bh.title,"請注意刪除的合法性","[系統安全局]");
		substitute_record(fn_board, &bh, sizeof(bh), bid);
		reset_board(bid);
		log_usies("DelBoard", bh.title);
		outs("刪板完畢");
	    }
	break;
    case 'e':
	move(8, 0);
	outs("直接按 [Return] 不修改該項設定");
	memcpy(&newbh, &bh, sizeof(bh));

	while(getdata(9, 0, "新看板名稱：", genbuf, IDLEN + 1, DOECHO)) {
	    if(getbnum(genbuf)) {
		move(3, 0);
		outs("錯誤! 板名雷同");
	    } else if(!invalid_brdname(genbuf)) {
		strcpy(newbh.brdname, genbuf);
		break;
	    }
	}
	
	do {
	    getdata_str(12, 0, "看板類別：", genbuf, 5, DOECHO, bh.title);
	    if(strlen(genbuf) == 4)
		break;
	} while (1);

	if(strlen(genbuf) >= 4)
	    strncpy(newbh.title, genbuf, 4);
	
	newbh.title[4] = ' ';

	getdata_str(14, 0, "看板主題：", genbuf, BTLEN + 1, DOECHO, 
		    bh.title + 7);
	if(genbuf[0])
	    strcpy(newbh.title + 7, genbuf);
	if(getdata_str(15, 0, "新板主名單：", genbuf, IDLEN * 3 + 3, DOECHO,
		       bh.BM)) {
	    trim(genbuf);
	    strcpy(newbh.BM, genbuf);
	}
	
	if(HAS_PERM(PERM_SYSOP)) {
	    move(1, 0);
	    clrtobot();
	    newbh.brdattr = setperms(newbh.brdattr, str_permboard);
	    move(1, 0);
	    clrtobot();
	}
	
	if(newbh.brdattr & BRD_GROUPBOARD)
	    strncpy(newbh.title + 5, "Σ", 2);
	else if(newbh.brdattr & BRD_NOTRAN)
	    strncpy(newbh.title + 5, "◎", 2);
	else
	    strncpy(newbh.title + 5, "●", 2);
	
	if(HAS_PERM(PERM_SYSOP) && !(newbh.brdattr & BRD_HIDE)) {
	    getdata_str(14, 0, "設定讀寫權限(Y/N)？", ans, sizeof(ans), LCECHO, "N");
	    if(*ans == 'y') {
		getdata_str(15, 0, "限制 [R]閱\讀 (P)發表？", ans, sizeof(ans), LCECHO,
			    "R");
		if(*ans == 'p')
		    newbh.brdattr |= BRD_POSTMASK;
		else
		    newbh.brdattr &= ~BRD_POSTMASK;
		
		move(1, 0);
		clrtobot();
		bperm_msg(&newbh);
		newbh.level = setperms(newbh.level, str_permid);
		clear();
	    }
	}
	
	getdata_str(b_lines - 1, 0, msg_sure_ny, genbuf, 4, LCECHO, "Y");

	if((*genbuf == 'y') && memcmp(&newbh, &bh, sizeof(bh))) {
	    if(strcmp(bh.brdname, newbh.brdname)) {
		char src[60], tar[60];
		
		setbpath(src, bh.brdname);
		setbpath(tar, newbh.brdname);
		Rename(src, tar);
		
		setapath(src, bh.brdname);
		setapath(tar, newbh.brdname);
		Rename(src, tar);
	    }
	    setup_man(&newbh);
	    substitute_record(fn_board, &newbh, sizeof(newbh), bid);
	    reset_board(bid);
	    log_usies("SetBoard", newbh.brdname);
	}
    }
    return 0;
}

extern char *msg_bid;

/* 設定看版 */
int m_board() {
    char bname[32];
    
    stand_title("看板設定");
    make_blist();
    namecomplete(msg_bid, bname);
    if(!*bname)
	return 0;
    m_mod_board(bname);
    return 0;
}

/* 設定系統檔案 */
int x_file() {
    int aborted;
    char ans[4], *fpath;
    
    move(b_lines - 4, 0);
    /* Ptt */
    outs("設定 (1)身份確認信 (4)post注意事項 (5)錯誤登入訊息 (6)註冊範例 (7)通過確認通知\n");
    outs("     (8)email post通知 (9)系統功\能精靈 (A)茶樓 (B)站長名單 (C)email通過確認\n");
    outs("     (D)新使用者需知 (E)身份確認方法 (F)歡迎畫面 (G)進站畫面 "
#ifdef MULTI_WELCOME_LOGIN
"(X)刪除進站畫面"
#endif
"\n");
    getdata(b_lines - 1, 0, "      (H)看板期限 (I)故鄉 (J)出站畫面 (K)生日卡 (L)節日 [Q]取消？", ans, sizeof(ans), LCECHO);
    
    switch(ans[0]) {
    case '1':
	fpath = "etc/confirm";
	break;
    case '4':
	fpath = "etc/post.note";
	break;
    case '5':
	fpath = "etc/goodbye";
	break;
    case '6':
	fpath = "etc/register";
	break;
    case '7':
	fpath = "etc/registered";
	break;
    case '8':
	fpath = "etc/emailpost";
	break;
    case '9':
	fpath = "etc/hint";
	break;
    case 'a':
	fpath = "etc/teashop";
	break;
    case 'b':
	fpath = "etc/sysop";
	break;
    case 'c':
	fpath = "etc/bademail";
	break;
    case 'd':
	fpath = "etc/newuser";
	break;
    case 'e':
	fpath = "etc/justify";
	break;
    case 'f':
	fpath = "etc/Welcome";
	break;
    case 'g':
#ifdef MULTI_WELCOME_LOGIN
	getdata(b_lines - 1, 0, "第幾個進站畫面[0-4]", ans, sizeof(ans), LCECHO);
	if( ans[0] == '1' )     { fpath = "etc/Welcome_login.1"; }
	else if( ans[0] == '2' ){ fpath = "etc/Welcome_login.2"; }
	else if( ans[0] == '3' ){ fpath = "etc/Welcome_login.3"; }
	else if( ans[0] == '4' ){ fpath = "etc/Welcome_login.4"; }
	else                    { fpath = "etc/Welcome_login.0"; }
#else
	fpath = "etc/Welcome_login";
#endif
	break;

#ifdef MULTI_WELCOME_LOGIN
    case 'x':
	getdata(b_lines - 1, 0, "第幾個進站畫面[1-4]", ans, sizeof(ans), LCECHO);
	if( ans[0] == '1' )     { unlink("etc/Welcome_login.1"); outs("ok"); }
	else if( ans[0] == '2' ){ unlink("etc/Welcome_login.2"); outs("ok"); }
	else if( ans[0] == '3' ){ unlink("etc/Welcome_login.3"); outs("ok"); }
	else if( ans[0] == '4' ){ unlink("etc/Welcome_login.4"); outs("ok"); }
	else {outs("所指定的進站畫面無法刪除"); }
	pressanykey();
	return FULLUPDATE;

#endif

    case 'h':
	fpath = "etc/expire.conf";
	break;
    case 'i':
	fpath = "etc/domain_name_query";
	break;
    case 'j':
	fpath = "etc/Logout";
	break;
    case 'k':
	fpath = "etc/Welcome_birth";
	break;
    case 'l':
	fpath = "etc/feast";
	break;
    default:
	return FULLUPDATE;
    }
    aborted = vedit(fpath, NA, NULL);
    prints("\n\n系統檔案[%s]：%s", fpath,
	   (aborted == -1) ? "未改變" : "更新完畢");
    pressanykey();
    return FULLUPDATE;
}

extern int numboards;
extern int class_bid;

int m_newbrd(int recover) {
    boardheader_t newboard;
    char ans[4];
    int bid;
    char genbuf[200];

    stand_title("建立新板");
    memset(&newboard, 0, sizeof(newboard));
    
    newboard.gid = class_bid;
    if (newboard.gid == 0) {
	move(6, 0);
	outs("請先選擇一個類別再開板!");
	pressanykey();
	return -1;
    }
    
    do {
	if(!getdata(3, 0, msg_bid, newboard.brdname,
		    sizeof(newboard.brdname), DOECHO))
	    return -1;
    } while(invalid_brdname(newboard.brdname));

    do {
	getdata(6, 0, "看板類別：", genbuf, 5, DOECHO);
	if(strlen(genbuf) == 4)
	    break;
    } while (1);
    
    if(strlen(genbuf) >= 4)
	strncpy(newboard.title, genbuf, 4);
    
    newboard.title[4] = ' ';
    
    getdata(8, 0, "看板主題：", genbuf, BTLEN + 1, DOECHO);
    if(genbuf[0])
	strcpy(newboard.title + 7, genbuf);
    setbpath(genbuf, newboard.brdname);
    
    if(recover) {
	struct stat sb;
	
	if(stat(genbuf, &sb) == -1 || !(sb.st_mode & S_IFDIR)) {
	    outs("此看板已經存在! 請取不同英文板名");
	    pressanykey();
	    return -1;
	}
    } else if(getbnum(newboard.brdname) > 0 || mkdir(genbuf, 0755) == -1) {
	outs("此看板已經存在! 請取不同英文板名");
	pressanykey();
	return -1;
    }
    
    newboard.brdattr = BRD_NOTRAN;
    
    if(HAS_PERM(PERM_SYSOP)) {
	move(1, 0);
	clrtobot();
	newboard.brdattr = setperms(newboard.brdattr, str_permboard);
	move(1, 0);
	clrtobot();
    }
    getdata(9, 0, "是看板? (N:目錄) (Y/n)：", genbuf, 3, LCECHO);
    if(genbuf[0]=='n')
	newboard.brdattr |= BRD_GROUPBOARD;

    if(newboard.brdattr & BRD_GROUPBOARD)
	strncpy(newboard.title + 5, "Σ", 2);
    else if(newboard.brdattr & BRD_NOTRAN)
	strncpy(newboard.title + 5, "◎", 2);
    else
	strncpy(newboard.title + 5, "●", 2);
    
    newboard.level = 0;
    getdata(11, 0, "板主名單：", newboard.BM, sizeof(newboard.BM), DOECHO);

    if(HAS_PERM(PERM_SYSOP) && !(newboard.brdattr & BRD_HIDE)) {
	getdata_str(14, 0, "設定讀寫權限(Y/N)？", ans, sizeof(ans), LCECHO, "N");
	if(*ans == 'y') {
	    getdata_str(15, 0, "限制 [R]閱\讀 (P)發表？", ans, sizeof(ans), LCECHO, "R");
	    if(*ans == 'p')
		newboard.brdattr |= BRD_POSTMASK;
	    else
		newboard.brdattr &= (~BRD_POSTMASK);
	    
	    move(1, 0);
	    clrtobot();
	    bperm_msg(&newboard);
	    newboard.level = setperms(newboard.level, str_permid);
	    clear();
	}
    }
    
    if((bid = getbnum("")) > 0)
     {
	substitute_record(fn_board, &newboard, sizeof(newboard), bid);
        reset_board(bid);
     }
    else if(append_record(fn_board, (fileheader_t *) & newboard,
			  sizeof(newboard)) == -1) {
	pressanykey();
	return -1;
    }
    else
    {
      addbrd_touchcache();
    }
    setup_man(&newboard);

    outs("\n新板成立");
    post_newboard(newboard.title, newboard.brdname, newboard.BM);
    log_usies("NewBoard", newboard.title);
    pressanykey();
    return 0;
}

static int auto_scan(char fdata[][STRLEN], char ans[]) {
    int good = 0;
    int count = 0;
    int i;
    char temp[10];
    
    if(!strncmp(fdata[2], "小", 2) || strstr(fdata[2], "丫")
	|| strstr(fdata[2], "誰") || strstr(fdata[2], "不")) {
	ans[0] = '0';
	return 1;
    }
    
    strncpy(temp, fdata[2], 2);
    temp[2] = '\0';
    
    /* 疊字 */
    if(!strncmp(temp, &(fdata[2][2]), 2)) {
	ans[0] = '0';
	return 1;
    }

    if(strlen(fdata[2]) >= 6) {
	if(strstr(fdata[2], "陳水扁")) {
	    ans[0] = '0';
	    return 1;
	}
	
	if(strstr("趙錢孫李周吳鄭王", temp))
	    good++;
	else if(strstr("杜顏黃林陳官余辛劉", temp))
	    good++;
	else if(strstr("蘇方吳呂李邵張廖應蘇", temp))
	    good++;
	else if(strstr("徐謝石盧施戴翁唐", temp))
	    good++;
    }
    
    if(!good)
	return 0;

    if(!strcmp(fdata[3], fdata[4]) ||
       !strcmp(fdata[3], fdata[5]) ||
       !strcmp(fdata[4], fdata[5])) {
	ans[0] = '4';
	return 5;
    }
    
    if(strstr(fdata[3], "大")) {
	if (strstr(fdata[3], "台") || strstr(fdata[3], "淡") ||
	    strstr(fdata[3], "交") || strstr(fdata[3], "政") ||
	    strstr(fdata[3], "清") || strstr(fdata[3], "警") ||
	    strstr(fdata[3], "師") || strstr(fdata[3], "銘傳") ||
	    strstr(fdata[3], "中央") || strstr(fdata[3], "成") ||
	    strstr(fdata[3], "輔") || strstr(fdata[3], "東吳"))
	    good++;
    } else if(strstr(fdata[3], "女中"))
	good++;
    
    if(strstr(fdata[4], "地球") || strstr(fdata[4], "宇宙") ||
       strstr(fdata[4], "信箱")) {
	ans[0] = '2';
	return 3;
    }
    
    if(strstr(fdata[4], "市") || strstr(fdata[4], "縣")) {
	if(strstr(fdata[4], "路") || strstr(fdata[4], "街")) {
	    if(strstr(fdata[4], "號"))
		good++;
	}
    }
    
    for(i = 0; fdata[5][i]; i++) {
	if(isdigit(fdata[5][i]))
	    count++;
    }
    
    if(count <= 4) {
	ans[0] = '3';
	return 4;
    } else if(count >= 7)
	good++;

    if(good >= 3) {
	ans[0] = 'y';
	return -1;
    } else
	return 0;
}

/* 處理 Register Form */
int scan_register_form(char *regfile, int automode, int neednum) {
    char genbuf[200];
    static char *logfile = "register.log";
    static char *field[] = {
	"uid", "ident", "name", "career", "addr", "phone", "email", NULL
    };
    static char *finfo[] = {
	"帳號", "身分證號", "真實姓名", "服務單位", "目前住址",
	"連絡電話", "電子郵件信箱", NULL
    };
    static char *reason[] = {
	"輸入真實姓名", "詳填學校科系與年級", "填寫完整的住址資料",
	"詳填連絡電話", "確實填寫註冊申請表", "用中文填寫申請單", NULL
    };
    static char *autoid = "AutoScan";
    userec_t muser;
    FILE *fn, *fout, *freg;
    char fdata[7][STRLEN];
    char fname[STRLEN], buf[STRLEN];
    char ans[4], *ptr, *uid;
    int n, unum;
    int nSelf = 0, nAuto = 0;
    
    uid = cuser.userid;
    sprintf(fname, "%s.tmp", regfile);
    move(2, 0);
    if(dashf(fname)) {
	if(neednum == 0) { /* 自己進 Admin 來審的 */
	    outs("其他 SYSOP 也在審核註冊申請單");
	    pressanykey();
	}
	return -1;
    }
    Rename(regfile, fname);
    if((fn = fopen(fname, "r")) == NULL) {
	prints("系統錯誤，無法讀取註冊資料檔: %s", fname);
	pressanykey();
	return -1;
    }
    if(neednum) { /* 被強迫審的 */
	move(1, 0);
	clrtobot();
	prints("各位具有站長權限的人，註冊單累積超過一百份了，麻煩您幫忙審 %d 份\n", neednum);
	prints("也就是大概二十分之一的數量，當然，您也可以多審\n沒審完之前，系統不會讓你跳出喲！謝謝");
	pressanykey();
    }
    memset(fdata, 0, sizeof(fdata));
    while(fgets(genbuf, STRLEN, fn)) {
	if ((ptr = (char *) strstr(genbuf, ": "))) {
	    *ptr = '\0';
	    for(n = 0; field[n]; n++) {
		if(strcmp(genbuf, field[n]) == 0) {
		    strcpy(fdata[n], ptr + 2);
		    if((ptr = (char *) strchr(fdata[n], '\n')))
			*ptr = '\0';
		}
	    }
	} else if ((unum = getuser(fdata[0])) == 0) {
	    move(2, 0);
	    clrtobot();
	    outs("系統錯誤，查無此人\n\n");
	    for (n = 0; field[n]; n++)
		prints("%s     : %s\n", finfo[n], fdata[n]);
	    pressanykey();
	    neednum--;
	} else {
	    neednum--;
	    memcpy(&muser, &xuser, sizeof(muser));
	    if(automode)
		uid = autoid;
	    
	    if(!automode || !auto_scan(fdata, ans)) {
		uid = cuser.userid;
		
		move(1, 0);
		prints("帳號位置    ：%d\n", unum);
		user_display(&muser, 1);
		move(14, 0);
		prints("\033[1;32m------------- 請站長嚴格審核使用者資料，您還有 %d 份---------------\033[m\n", neednum);
		for(n = 0; field[n]; n++) {
		    if(n >= 2 && n <= 5)
			prints("%d.", n - 2);
		    else
			prints("  ");
		    prints("%-12s：%s\n", finfo[n], fdata[n]);
		}
		if(muser.userlevel & PERM_LOGINOK) {
		    getdata(b_lines - 1, 0, "\033[1;32m此帳號已經完成註冊, "
			    "更新(Y/N/Skip)？\033[m[N] ", ans, sizeof(ans), LCECHO);
		    if(ans[0] != 'y' && ans[0] != 's')
			ans[0] = 'd';
		} else {
		    move(b_lines - 1, 0);
		    prints("是否接受此資料(Y/N/Q/Del/Skip)？[S] ");
		    ans[0] = igetch();
		    if( 'A' <= ans[0] && ans[0] <= 'Z' )
			ans[0] += 32;
		    if( ans[0] != 'y' && ans[0] != 'n' && ans[0] != 'q' &&
			ans[0] != 'd' && !('0' <= ans[0] && ans[0] <= '4') )
			ans[0] = 's';
		    ans[1] = 0;
		}
		nSelf++;
	    } else
		nAuto++;
	    if(neednum > 0 && ans[0] == 'q') {
		move(2, 0);
		clrtobot();
		prints("沒審完不能退出");
		pressanykey();
		ans[0] = 's';
	    }
	    
	    switch(ans[0]) {
	    case 'q':
		if((freg = fopen(regfile, "a"))) {
		    for(n = 0; field[n]; n++)
			fprintf(freg, "%s: %s\n", field[n], fdata[n]);
		    fprintf(freg, "----\n");
		    while(fgets(genbuf, STRLEN, fn))
			fputs(genbuf, freg);
		    fclose(freg);
		}
	    case 'd':
		break;
	    case '0':
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case 'n':
		if(ans[0] == 'n') {
		    for(n = 0; field[n]; n++)
			prints("%s: %s\n", finfo[n], fdata[n]);
		    move(9, 0);
		    prints("請提出退回申請表原因，按 <enter> 取消\n");
		    for (n = 0; reason[n]; n++)
			prints("%d) 請%s\n", n, reason[n]);
		} else
		    buf[0] = ans[0];
		if(ans[0] != 'n' ||
		    getdata(10 + n, 0, "退回原因：", buf, 60, DOECHO))
		    if((buf[0] - '0') >= 0 && (buf[0] - '0') < n) {
			int i;
			fileheader_t mhdr;
			char title[128], buf1[80];
			FILE *fp;
			
			i = buf[0] - '0';
			strcpy(buf, reason[i]);
			sprintf(genbuf, "[退回原因] 請%s", buf);
			
			sethomepath(buf1, muser.userid);
			stampfile(buf1, &mhdr);
			strcpy(mhdr.owner, cuser.userid);
			strncpy(mhdr.title, "[註冊失敗]", TTLEN);
			mhdr.savemode = 0;
			mhdr.filemode = 0;
			sethomedir(title, muser.userid);
			if(append_record(title, &mhdr, sizeof(mhdr)) != -1) {
			    fp = fopen(buf1, "w");
			    fprintf(fp, "%s\n", genbuf);
			    fclose(fp);
			}
			if((fout = fopen(logfile, "a"))) {
			    for(n = 0; field[n]; n++)
				fprintf(fout, "%s: %s\n", field[n], fdata[n]);
			    n = time(NULL);
			    fprintf(fout, "Date: %s\n", Cdate((time_t *) & n));
			    fprintf(fout, "Rejected: %s [%s]\n----\n",
				    uid, buf);
			    fclose(fout);
			}
			break;
		    }
		move(10, 0);
		clrtobot();
		prints("取消退回此註冊申請表");
	    case 's':
		if((freg = fopen(regfile, "a"))) {
		    for(n = 0; field[n]; n++)
			fprintf(freg, "%s: %s\n", field[n], fdata[n]);
		    fprintf(freg, "----\n");
		    fclose(freg);
		}
		break;
	    default:
		prints("以下使用者資料已經更新:\n");
		mail_muser(muser, "[註冊成功\囉]", "etc/registered");
		muser.userlevel |= (PERM_LOGINOK | PERM_POST);
		strcpy(muser.realname, fdata[2]);
		strcpy(muser.address, fdata[4]);
		strcpy(muser.email, fdata[6]);
		sprintf(genbuf, "%s:%s:%s", fdata[5], fdata[3], uid);
		strncpy(muser.justify, genbuf, REGLEN);
		sethomefile(buf, muser.userid, "justify");
		log_file(buf, genbuf);
		passwd_update(unum, &muser);
		
		if((fout = fopen(logfile, "a"))) {
		    for(n = 0; field[n]; n++)
			fprintf(fout, "%s: %s\n", field[n], fdata[n]);
		    n = time(NULL);
		    fprintf(fout, "Date: %s\n", Cdate((time_t *) & n));
		    fprintf(fout, "Approved: %s\n", uid);
		    fprintf(fout, "----\n");
		    fclose(fout);
		}
		
		sethomefile(genbuf, muser.userid, "justify.wait");
		unlink(genbuf);
		break;
	    }
	}
    }
    fclose(fn);
    unlink(fname);

    move(0, 0);
    clrtobot();

    move(5, 0);
    prints("您審了 %d 份註冊單，AutoScan 審了 %d 份", nSelf, nAuto);

/**	DickG: 將審了幾份的相關資料 post 到 Security 板上	***********/
/*    DickG: 因應新的站長上站需審核方案，是故沒有必要留下 record 而關掉
      strftime(buf, 200, "%Y/%m/%d/%H:%M", pt);

      strcpy(xboard, "Security");
      setbpath(xfpath, xboard);
      stampfile(xfpath, &xfile);
      strcpy(xfile.owner, "系統");
      strcpy(xfile.title, "[報告] 審核記錄");
      xfile.savemode = 'S';
      xptr = fopen(xfpath, "w");
      fprintf(xptr, "\n時間：%s 
      %s 審了 %d 份註冊單\n
      AutoScan 審了 %d 份註冊單\n
      共計 %d 份。", 
      buf, cuser.userid, nSelf, nAuto, nSelf+nAuto);
      fclose(xptr);
      setbdir(fname, xboard);
      append_record(fname, &xfile, sizeof(xfile));
      outgo_post(&xfile, xboard);
      touchbtotal(getbnum(xboard));
      cuser.numposts++;        
*/
/*********************************************/
    pressanykey();
    return (0);
}

extern char* fn_register;
extern int t_lines;

int m_register() {
    FILE *fn;
    int x, y, wid, len;
    char ans[4];
    char genbuf[200];
    
    if((fn = fopen(fn_register, "r")) == NULL) {
	outs("目前並無新註冊資料");
	return XEASY;
    }
    
    stand_title("審核使用者註冊資料");
    y = 2;
    x = wid = 0;
    
    while(fgets(genbuf, STRLEN, fn) && x < 65) {
	if(strncmp(genbuf, "uid: ", 5) == 0) {
	    move(y++, x);
	    outs(genbuf + 5);
	    len = strlen(genbuf + 5);
	    if(len > wid)
		wid = len;
	    if(y >= t_lines - 3) {
		y = 2;
		x += wid + 2;
	    }
	}
    }
    fclose(fn);
    getdata(b_lines - 1, 0, "開始審核嗎(Auto/Yes/No)？[N] ", ans, sizeof(ans), LCECHO);
    if(ans[0] == 'a')
	scan_register_form(fn_register, 1, 0);
    else if(ans[0] == 'y')
	scan_register_form(fn_register, 0, 0);
    
    return 0;
}

int cat_register() {
    if(system("cat register.new.tmp >> register.new") == 0 &&
       system("rm -f register.new.tmp") == 0)
	mprints(22, 0, "OK 嚕~~ 繼續去奮鬥吧!!                                                ");
    else
	mprints(22, 0, "沒辦法CAT過去呢 去檢查一下系統吧!!                                    ");
    pressanykey();
    return 0;
}

static void give_id_money(char *user_id, int money, FILE *log_fp, char *mail_title, time_t t) {
    char tt[TTLEN + 1] = {0};
    
    if(deumoney(searchuser(user_id), money) < 0) {
        move(12, 0);
        clrtoeol();
        prints("id:%s money:%d 不對吧!!", user_id, money);
        pressanykey();
    } else {
        fprintf(log_fp, "%ld %s %d", t, user_id, money);
        sprintf(tt, "%s : %d ptt 幣", mail_title, money);
        mail_id(user_id, tt, "~bbs/etc/givemoney.why", "[PTT 銀行]");
    }
}            

int give_money() {
    FILE *fp, *fp2;
    char *ptr, *id, *mn;
    char buf[200] = {0}, tt[TTLEN + 1] = {0}; 
    time_t t = time(NULL);
    struct tm *pt = localtime(&t);
    int to_all = 0, money = 0;
    
    getdata(0, 0, "指定使用者(S) 全站使用者(A) 取消(Q)？[S]", buf, sizeof(buf), LCECHO);
    if(buf[0] == 'q')
	return 1;
    else if( buf[0] == 'a') {
	to_all = 1;
	getdata(1, 0, "發多少錢呢?", buf, 20, DOECHO);
	money = atoi(buf);
	if(money <= 0) {
	    move(2, 0);
	    prints("輸入錯誤!!");
	    pressanykey();
	    return 1;
	}
    } else {
	if(vedit("etc/givemoney.txt", NA, NULL) < 0)
	    return 1;
    }
    
    clear();
    getdata(0, 0, "要發錢了嗎(Y/N)[N]", buf, 3, LCECHO);
    if(buf[0] != 'y')
	return 1;

    if(!(fp2 = fopen("etc/givemoney.log", "a")))
	return 1;
    strftime(buf, 200, "%Y/%m/%d/%H:%M", pt);
    fprintf(fp2, "%s\n", buf);
    
    getdata(1, 0, "紅包袋標題 ：", tt, TTLEN, DOECHO);
    move(2, 0);
    
    prints("編紅包袋內容");
    pressanykey();
    if(vedit("etc/givemoney.why", NA, NULL) < 0)
	return 1;
    
    stand_title("發錢中...");
    if(to_all) {
	extern struct uhash_t *uhash;
	int i, unum;
	for(unum = uhash->number, i=0; i<unum; i++) {
	    if(bad_user_id(uhash->userid[i]))
		continue;
	    id = uhash->userid[i];
	    give_id_money(id, money, fp2, tt, t);
	}
    } else {
	if(!(fp = fopen("etc/givemoney.txt", "r+"))) {
	    fclose(fp2);
	    return 1;
	}
	while(fgets(buf, 255, fp)) {
//  	    clear();
	    if (!(ptr = strchr(buf, ':')))
		continue;
	    *ptr = '\0';
	    id = buf;
	    mn = ptr + 1;
	    give_id_money(id, atoi(mn), fp2, tt, t);
	}
	fclose(fp);
    }
    
    fclose(fp2);
    pressanykey();
    return FULLUPDATE;
}
