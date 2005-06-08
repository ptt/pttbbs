/* $Id$ */
#include "bbs.h"

#define REFER "etc/dicts"

static void
addword(const char *database,char word[])
{
    char            buf[150], a[3];
    FILE           *fp = fopen(database, "r+");

    if (fp == NULL) {
	vmsg("database error");
	return;
    }
    fgets(buf, 130, fp);
    fseek(fp, 0, 2);
    if (HAVE_PERM(PERM_LOGINOK)) {
	clear();
	move(4, 0);
	outs(" " ANSI_COLOR(31) "警告" ANSI_RESET ":若蓄意填寫假資料將" ANSI_COLOR(36) "砍id" ANSI_RESET "處份\n");
	prints("\n輸入範例\n:" ANSI_COLOR(33) "%s" ANSI_RESET, buf);
	outs("\n請依上列範例輸入一行資料(直接enter放棄)\n");
	getdata(10, 0, ":", buf, 65, DOECHO);
	if (buf[0]) {
	    getdata(13, 0, "確定新增?(Y/n)", a, sizeof(a), LCECHO);
	    if (a[0] != 'n')
		fprintf(fp, "%-65s[%s]\n", buf, cuser.userid);
	}
    }
    fclose(fp);
    clear();
}

static int
choose_dict(char *dict,int dictlen,char *database,int databaselen)
{
#define MAX_DICT 10
    int             n,c;
    FILE           *fp;
    char            buf[MAX_DICT][21], data[MAX_DICT][21], cho[10];

    move(12, 0);
    clrtobot();
    outs("                        "
	 "● " ANSI_COLOR(45;33) "字典唷 ◇ 要查哪一本？" ANSI_RESET " ●");

    if ((fp = fopen(REFER, "r"))) {
	for(n=0; n<MAX_DICT && fscanf(fp,"%s %s",buf[n],data[n])==2; n++) { // XXX check buffer size
	    prints("\n                     "
		    "(" ANSI_COLOR(36) "%d" ANSI_RESET ") %-20s大字典", n + 1, buf[n]);
	}
	fclose(fp);

	getdata(22, 14, "          ★ 請選擇，[Enter]離開：", cho, 3, LCECHO);
	c=atoi(cho);

	if (c >= 1 && c <= n) {
	    strlcpy(dict, buf[c-1], dictlen);
	    strlcpy(database, data[c-1], databaselen);
	    return 1;
	} else
	    return 0;
    }
    return 0;
}

int
use_dict(char *dict,char *database)
{
    FILE           *fp;
    char            lang[150], word[80] = "";
    char            j, f, buf[120], sys[] = "|" ANSI_COLOR(31) "e" ANSI_RESET ":編輯字典";
    int             i = 0;

    setutmpmode(DICT);
    if (!HAS_PERM(PERM_SYSOP))
	sys[0] = 0;

    clear();

    snprintf(buf, sizeof(buf),
	     ANSI_COLOR(45) "                           ●" ANSI_COLOR(1;44;33) ""
	     "  %-14s" ANSI_COLOR(3;45) " ●                              ", dict);
    strlcpy(&buf[100], ANSI_RESET "\n", sizeof(buf) - 100);
    for (;;) {
	move(0, 0);
	prints("  請輸入關鍵字串(%s) 或指令(h,t,a)\n", dict);
	prints("[" ANSI_COLOR(32) "<關鍵字>" ANSI_RESET "|" ANSI_COLOR(32) "h" ANSI_RESET ":help|" ANSI_COLOR(32) ""
		 "t" ANSI_RESET ":所有資料|" ANSI_COLOR(32) "a" ANSI_RESET ":新增資料%s]\n:", sys);
	getdata(2, 0, ":", word, 18, DOECHO);
	outs("資料搜尋中請稍候....");
	str_lower(word, word);
	if (word[0] == 0)
	    return 0;
	clear();
	move(4, 0);
	outs(buf);
	if (strlen(word) == 1) {
	    if (word[0] == 'a') {
		clear();
		move(4, 0);
		outs(buf);
		addword(database,word);
		continue;
	    } else if (word[0] == 't')
		word[0] = 0;
	    else if (word[0] == 'h') {
		more("etc/dict.hlp", YEA);
		clear();
		continue;
	    } else if (word[0] == 'e' && HAS_PERM(PERM_SYSOP)) {
		vedit(database, NA, NULL);
		clear();
		continue;
	    } else {
		outs("字串太短,請輸入多一點關鍵字");
		continue;
	    }
	}
	i = 0;
	if ((fp = fopen(database, "r"))) {
	    while (fgets(lang, sizeof(lang), fp) != NULL) {
		if (lang[65] == '[') {
		    lang[65] = 0;
		    f = 1;
		} else
		    f = 0;
		if (strcasestr(lang, word)) {
		    if (f == 1)
			lang[65] = '[';
		    outs(lang);
		    i++;
		    if (!((i + 1) % 17)) {
			move(23, 0);
			outs(ANSI_COLOR(45) "                               "
			   "任意鍵繼續  Q:離開                             "
			     ANSI_RESET " ");
			j = igetch();
			if (j == 'q')
			    break;
			else {
			    clear();
			    move(4, 0);
			    outs(buf);
			}
		    }
		}
	    }
	    fclose(fp);
	}
	if (i == 0) {
	    getdata(5, 0, "沒這個資料耶,新增嗎?(y/N)", lang, 3, LCECHO);
	    if (lang[0] == 'y') {
		clear();
		move(4, 0);
		outs(buf);
		addword(database,word);
	    }
	}
    }
}

int
x_dict(void)
{
    char dict[41], database[41];
    if (choose_dict(dict,sizeof(dict),database,sizeof(database)))
	use_dict(dict,database);
    return 0;
}
