/* $Id: bbcall.c,v 1.7 2002/07/21 09:26:02 in2 Exp $ */
#include "bbs.h"

#define SERVER_0941     "www.chips.com.tw"
#define SERVER_0943     "www.pager.com.tw"
#define SERVER_0948     "www.fitel.net.tw"
#define SERVER_0947     "www.hoyard.com.tw"
#define SERVER_0945     "203.73.181.254"

#define CGI_0948        "/cgi-bin/Webpage.dll"
#define CGI_0941        "/cgi-bin/paging1.pl"
#define CGI_0947        "/scripts/fp_page1.dll"
#define CGI_0945        "/Scripts/fiss/PageForm.exe"
#define CGI_0943        "/tpn/tpnasp/dowebcall.asp"

#define CGI_RAILWAY     "http://www.railway.gov.tw/cgi-bin/timetk.cgi"

#define REFER_0943      "http://www.pager.com.tw/tpn/webcall/webcall.asp"
#define REFER_0948      "http://www.fitel.net.tw/html/svc03.htm"
#define REFER_0941      "http://www.chips.com.tw:9100/WEB2P/page_1.htm"
#define REFER_0947      "http://web1.hoyard.com.tw/freeway/freewayi.html"
#define REFER_0945      "http://203.73.181.254/call.HTM"

static void
pager_msg_encode(char *field, char *buf)
{
    char           *cc = field;
    unsigned char  *p;

    for (p = (unsigned char *)buf; *p; p++) {
	if ((*p >= '0' && *p <= '9') ||
	    (*p >= 'A' && *p <= 'Z') ||
	    (*p >= 'a' && *p <= 'z') ||
	    *p == ' ')
	    *cc++ = *p == ' ' ? '+' : (char)*p;
	else {
	    sprintf(cc, "%%%02X", (int)*p);
	    cc += 3;
	}
    }
    *cc = 0;
}

static void
Gettime(int flag, int *Year, int *Month, int *Day, int *Hour,
	int *Minute)
{
    char            ans[5];

    do {
	getdata(10, 0, "年[20-]:", ans, 3, LCECHO);
	*Year = atoi(ans);
    } while (*Year < 00 || *Year > 02);
    do {
	getdata(10, 15, "月[1-12]:", ans, 3, LCECHO);
    } while (!IsSNum(ans) || (*Month = atoi(ans)) > 12 || *Month < 1);
    do {
	getdata(10, 30, "日[1-31]:", ans, 3, LCECHO);
    } while (!IsSNum(ans) || (*Day = atoi(ans)) > 31 || *Day < 1);
    do {
	getdata(10, 45, "時[0-23]:", ans, 3, LCECHO);
    } while (!IsSNum(ans) || (*Hour = atoi(ans)) > 23 || *Hour < 0);
    do {
	getdata(10, 60, "分[0-59]:", ans, 3, LCECHO);
    } while (!IsSNum(ans) || (*Minute = atoi(ans)) > 59 || *Minute < 0);
    if (flag == 1)
	*Year -= 11;
}

#define hpressanykey(a) {move(22, 0); prints(a); pressanykey();}

static int
Connect(char *s, char *server)
{
    FILE           *fp = fopen(BBSHOME "/log/bbcall.log", "a");
    int             sockfd;
    char            result[2048];
    struct sockaddr_in serv_addr;
    struct hostent *hp;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
	return 0;

    memset((char *)&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;

    if ((hp = gethostbyname(server)) == NULL)
	return 0;

    memcpy(&serv_addr.sin_addr, hp->h_addr, hp->h_length);

    if (!strcmp(server, SERVER_0941))
	serv_addr.sin_port = htons(9100);
    else
	serv_addr.sin_port = htons(80);

    if (connect(sockfd, (struct sockaddr *) & serv_addr, sizeof serv_addr)) {
	hpressanykey("無法與伺服器取得連結，傳呼失敗");
	return 0;
    } else {
	mprints(20, 0, "\033[1;33m伺服器已經連接上，請稍後"
		".....................\033[m");
	refresh();
    }

    write(sockfd, s, strlen(s));
    shutdown(sockfd, 1);

    while (read(sockfd, result, sizeof(result)) > 0) {
	fprintf(fp, "%s\n", result);
	fflush(fp);
	if (strstr(result, "正確") ||
	    strstr(result, "等待") ||
	    strstr(result, "再度") ||
	    strstr(result, "完 成 回 應") ||
	    strstr(result, "預約中") ||
	    strstr(result, "傳送中")) {
	    close(sockfd);
	    hpressanykey("順利送出傳呼");
	    return 0;
	}
	memset(result, 0, sizeof(result));
    }
    fclose(fp);
    close(sockfd);
    hpressanykey("無法順利送出傳呼");
    return 0;
}

#define PARA \
"Connection: Keep-Alive\r\n"\
"User-Agent: Lynx/2.6  libwww-FM/2.14\r\n"\
"Content-type: application/x-www-form-urlencoded\r\n"\
"Accept: text/html, text/plain, application/x-wais-source, "\
"application/html, */*;q=0.001\r\n"\
"Accept-Encoding: gzip\r\n"\
"Accept-Language: en\r\n"\
"Accept-Charset: iso-8859-1,*,utf-8\r\n"

static void halpha0943(char* CoId) {
    char tmpbuf[64],ans[2];
    char ID[8];
    char Msg[64], atrn[512], sendform[1024];
    int Year = 99, Month = 1, Day = 15, Hour = 13, Minute = 8;

    sprintf(tmpbuf, "\033[1;37m請輸入您要傳呼的號碼\033[m : %s-", CoId);
    if(!getdata(7,0, tmpbuf, ID, sizeof(ID), LCECHO) ||
       !getdata(8,0, "\033[1;37m請輸入傳呼訊息\033[m：", tmpbuf, 63, LCECHO)) {
        hpressanykey("放棄傳呼");
        return;
    }
    pager_msg_encode(Msg,tmpbuf);
    getdata(9, 0, "\033[1;37m如果你要馬上送請按 '1' "
            "如果要定時送請按 '2': \033[m", ans, sizeof(ans), LCECHO);

    if(ans[0] != '1')
        Gettime(0, &Year, &Month, &Day, &Hour, &Minute);

    sprintf(atrn, "CoId=%s&ID=%s&Year=19%02d&Month=%02d&Day=%02d"
            "&Hour=%02d&Minute=%02d&Msg=%s",
            CoId, ID,Year,Month,Day,Hour,Minute,Msg);
    sprintf(sendform, "POST %s HTTP/1.0\nReferer: "
            "%s\n%sContent-length:%d\n\n%s",
            CGI_0943, REFER_0943, PARA, strlen(atrn), atrn);
    Connect(sendform, SERVER_0943);
    return ;
}

static void hcall0941() {
    char ans[2];
    char PAGER_NO[8], TRAN_MSG[18], TIME[8];
    char trn[512], sendform[512];
    int  year = 98, month = 12, day = 4, hour = 13, min = 8;

    if(!getdata(7, 0, "\033[1;37 請您輸入您要傳呼的號碼 : 0941- \033[m",
                PAGER_NO, sizeof(PAGER_NO), LCECHO)  ||
       !getdata(8, 0, "\033[1;37m請輸入傳呼訊息\033[m：", trn, 17, LCECHO)) {
        hpressanykey("放棄傳呼");
        return;
    }
    pager_msg_encode(TRAN_MSG,trn);
    getdata(9,0, "\033[1;37m如果你要馬上送請按 '1' "
            "如果要定時送請按 '2': \033[m", ans, sizeof(ans), LCECHO);
    if(ans[0] != '1') {
        strlcpy(TIME, "DELAY", sizeof(TIME));
        Gettime(0, &year, &month, &day, &hour, &min);
    } else
        strlcpy(TIME, "NOW", sizeof(TIME));
    sprintf(trn,"PAGER_NO=%s&TRAN_MSG=%s&MSG_TYPE=NUMERIC&%s=1"
            "&year=19%02d&month=%02d&day=%02d&hour=%02d&min=%02d",
            PAGER_NO, TRAN_MSG, TIME,year,month,day,hour,min);

    sprintf(sendform, "POST %s HTTP/1.0\nReferer: %s\n%s"
            "Content-length:%d\n\n%s",
            CGI_0941, REFER_0941, PARA, strlen(trn), trn);

    Connect(sendform, SERVER_0941);
    return ;
}

static void hcall0948() {
    int  year = 87, month = 12, day = 19, hour = 12, min = 0, ya = 0;
    char svc_no[8], message[64], trn[256], sendform[512], ans[3];

    move(7,0);
    clrtoeol();

    if(!getdata(7, 0, "\033[1;37m請輸入您要傳呼的號碼\033[m：0948-",
                svc_no, sizeof(svc_no), LCECHO) ||
       !getdata(8, 0, "\033[1;37m請輸入傳呼訊息\033[m：", trn, 61, LCECHO)) {
        hpressanykey("放棄傳呼");
        return;
    }
    pager_msg_encode(message, trn);
    getdata(9, 0, "\033[1;37m如果你要馬上送請按 '1' "
            "如果要定時送請按 '2'\033[m: ", ans, sizeof(ans), LCECHO);
    if(ans[0] != '1') {
        Gettime(1, &year, &month, &day, &hour, &min);
        ya = 1;
    }

    sprintf(trn, "MfcISAPICommand=SinglePage&svc_no=%s&reminder=%d"
            "&year=%02d&month=%02d&day=%02d&hour=%02d&min=%02d&message=%s",
            svc_no, ya, year, month, day, hour, min, message);

    sprintf(sendform, "GET %s?%s Http/1.0\n\n", CGI_0948, trn);

    Connect(sendform, SERVER_0948);
    return;
}

int main_bbcall() {
    char ch[2];

    clear();
    move(0, 30);
    prints("\033[1;37;45m 逼逼摳機 \033[m");
    move(3, 0);
    prints("\033[1;31m     ┌────────────────────"
           "─────────┐\033[m\n");
    prints("\033[1;33m        (1)0941        (2)0943        (3)0946  "
           "       (4)0948     \033[m\n");
    prints("\033[1;31m     └────────────────────"
           "─────────┘\033[m\n");
    getdata(7, 8, "\033[1;37m你的選擇? [1-4]\033[m", ch, sizeof(ch), LCECHO);

    switch(ch[0]) {
    case '1':
        hcall0941();
        break;
    case '2':
        halpha0943("0943");
        break;
    case '3':
        halpha0943("0946");
        break;
    case '4':
        hcall0948();
        break;
    }
    return 0;
}





