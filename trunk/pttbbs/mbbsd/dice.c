/* $Id: dice.c,v 1.1 2002/03/07 15:13:48 in2 Exp $ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "config.h"
#include "pttstruct.h"
#include "common.h"
#include "modes.h"
#include "proto.h"

#define DICE_TXT   BBSHOME "/etc/dice.txt"
#define DICE_DATA  BBSHOME "/etc/dice.data"
#define DICE_WIN   BBSHOME "/etc/windice.log"
#define DICE_LOST  BBSHOME "/etc/lostdice.log"

#define B_MAX    500
#define B_MIN    10
#define B_COMMON 1
#define B_TIMES  5
#define B_THIRD  3

extern int usernum;
static int flag[100], value[100];

typedef struct dicedata_t {
    int mybet;
    int mymoney;
} dicedata_t;

static void set_bingo(int bet[]) {
    int i, j = 0, k = 0, m = 0;
    
    for(i = 0; i < 3; i++)
	for(j = 2; j > i; j--)
	    if(bet[j] < bet[j - 1]) {
		m = bet[j];
		bet[j] = bet[j - 1];
		bet[j - 1]=m;
	    }
    
    for(i = 0; i < 100; i++)
	flag[i] = 0;
    
    for(i = 0; i < 3; i++)
	flag[bet[i]]++;
    j = bet[0] + bet[1] + bet[2];

    if((abs(bet[1] - bet[0]) == 1 && abs(bet[2] - bet[0]) == 2) ||
       (abs(bet[2] - bet[0]) == 1 && abs(bet[1] - bet[0]) == 2))
	flag[66] = B_TIMES;
    
    if(j < 10){
	flag[7] = B_COMMON;
	for(i = 0; i < 3; i++)
	    if(bet[i] == 4)
		flag[74] = B_TIMES;
    } else if(j > 11) {
	flag[8] = B_COMMON;
	for(i = 0; i < 3; i++)
	    if(bet[i] == 3)
		flag[83]=B_TIMES;
    } else
	flag[11] = B_THIRD;
    
    for(i = 0; i < 3; i++)
	for(j = i; j < 3; j++) {
	    m = bet[i];
	    k = bet[j];
	    if(m != k)
		flag[m * 10 + k] = B_TIMES;
	}
}

static int bingo(int mybet) {
    return flag[mybet];
}

int IsNum(char *a, int n) {
    int i;

    for(i = 0; i < n; i++)
	if (a[i] > '9' || a[i] < '0' )
	    return 0;
    return 1;
}

int IsSNum(char *a) {
    int i;
    
    for(i = 0; a[i]; i++)
	if(a[i] > '9' || a[i] < '0')
	    return 0;
    return 1;
}

static void show_data(void) {
    move(0, 0);
    prints("\033[31m       ┌───────────────────────"
	   "──────────┐\033[m\n");
    prints("\033[45;37m倍率一\033[m\033[31m │ \033[33m[1]押一點 [2]押二點 "
	   "[3]押三點 [4]押四點 [5]押五點 [6]押六點    \033[31m  │\033[m\n");
    prints("\033[31m       │ \033[33m[7]押小   [8]押大                    "
	   "                          \033[31m  │\033[m\n");
    prints("\033[31m       │                                              "
	   "                    │\033[m\n");
    prints("\033[45;37m賠率三\033[m\033[31m │ \033[33m[11]押中(總點數等於11"
	   "或10)                                     \033[31m  │\033[m\n");
    prints("\033[31m       │                                              "
	   "                    │\033[m\n");
    prints("\033[45;37m賠率五\033[m\033[31m │ \033[33m[74]押小且四點 [83]押"
	   "大且三點 [66]押連號                       \033[31m  │\033[m\n");
    prints("\033[31m       │                                              "
	   "                    │\033[m\n");
    prints("\033[31m       │ \033[33m[12]押一二點 [13]押一三點 [14]押一四點"
	   " [15]押一五點 [16]押一六點\033[31m │\033[m\n");
    prints("\033[31m       │ \033[33m[23]押二三點 [24]押二四點 [25]押二五點"
	   " [26]押二六點 [34]押三四點\033[31m │\033[m\n");
    prints("\033[31m       │ \033[33m[35]押三五點 [36]押三六點 [45]押四五點"
	   " [46]押四六點 [56]押五六點\033[31m │\033[m\n");
    prints("\033[31m       └────────────────────────"
	   "─────────┘\033[m\n");
}

static void show_count(int index, int money) {
    int i = 0, count = 2, j, k;
    
    value[index] += money;
    move(14,0);
    clrtoline(18);
    for(i = 1, j = 13; i <= 8; i++, count += 12) {
	if(i == 6) {
	    j = 14;
	    count = 2;
	}
	move(j,count);
	prints("[%2d]:%d ", i, value[i]);
    }
    
    count = 2;
    i = 15;
    for(j = 1; j <= 5; j++)
	for(k = j + 1; k <= 6; k++, count += 12) {
	    if(j == 2 && k == 4) {
		i = 16;
		count = 2;
	    } else if(j==4 && k==5) {
		i = 17;
		count = 2;
	    }
	    move(i,count);
	    prints("[%d%d]:%d ", j, k, value[j * 10 + k]);
	}
    
    move(18,2);
    prints("[11]:%d",value[11]);
    move(18,14);
    prints("[66]:%d",value[66]);
    move(18,26);
    prints("[74]:%d",value[74]);
    move(18,38);
    prints("[83]:%d",value[83]);
}

static int check_index(int index) {
    int i,tp[] = {1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 13, 14, 15, 16, 23, 24, 25,
		  26, 34, 35, 36, 45, 46, 56, 66, 74, 83};
    if(index < 0 || index > 100)
	return 0;
    for(i = 0; i < 27; i++)
	if(index == tp[i])
	    return 1;
    return 0;
}

extern userec_t cuser;

static int del(int total, dicedata_t *table) {
    int index, money;
    char data[10];
    int i;
    
    while(1) {
	do {
	    move(22,0);
	    clrtoeol();
	    getdata(21, 0, "輸入退選的數字(打q離開): ", data, 3, LCECHO);
	    if(data[0] == 'q' || data[0] == 'Q')
		return 0;
	} while(!IsNum(data,strlen(data)));
	
	index = atoi(data);
	for(i = 0; i < total; i++) {
	    if(table[i].mybet == index){
		do {
		    getdata(21, 0, "多少錢: ", data, 10, LCECHO);
		} while(!IsNum(data,strlen(data)));
		money = atoi(data);
		if(money>table[i].mymoney) {
		    move(22,0);
		    clrtoeol();
		    prints("不夠扣啦");
		    i--;
		    continue;
		}
		demoney(money);
		move(19,0);
		clrtoeol();
		prints("你現在有 %u Ptt$歐", cuser.money);
		table[i].mymoney -= money;
		show_count(index, -money);
		break;
	    }
	}
    }
    return 0;
}

static int IsLegal(char *data) {
    int money = atoi(data);
    if(IsNum(data,strlen(data)) && money<=B_MAX && money>=B_MIN)
	return money;
    return 0;
}

static void show_output(int bet[]) {
    int i, j = 10;
    
    move(12,0);
    clrtoline(17);
    /* 暫時降啦 因為那各clrtoline怪怪的 */
    for(i = 13; i <= 18; i++) {
	move(i,0);
	prints("                               ");
    }
    move(12,0);
    prints("\033[1;31m        ┌──────────────────────"
	   "─┐\033[m\n\n\n\n\n\n");
    prints("\033[1;31m        └──────────────────────"
	   "─┘\033[m");
    for(i = 0; i < 3; i++, j += 25) {
	switch(bet[i]) {
	case 1:
	    move(13, j);prints("\033[37m╭────╮\033[m");
	    move(14, j);prints("\033[37m│        │\033[m");
	    move(15, j);prints("\033[37m│   ●   │\033[m");
	    move(16, j);prints("\033[37m│        │\033[m");
	    move(17, j);prints("\033[37m╰────╯\033[m");
	    break;
	case 2:
	    move(13, j);prints("\033[37m╭────╮\033[m");
	    move(14, j);prints("\033[37m│      ●│\033[m");
	    move(15, j);prints("\033[37m│        │\033[m");
	    move(16, j);prints("\033[37m│●      │\033[m");
	    move(17, j);prints("\033[37m╰────╯\033[m");
	    break;
	case 3:
	    move(13, j);prints("\033[37m╭────╮\033[m");
	    move(14, j);prints("\033[37m│      ●│\033[m");
	    move(15, j);prints("\033[37m│   ●   │\033[m");
	    move(16, j);prints("\033[37m│●      │\033[m");
	    move(17, j);prints("\033[37m╰────╯\033[m");
	    break;
	case 4:
	    move(13, j);prints("\033[37m╭────╮\033[m");
	    move(14, j);prints("\033[37m│●    ●│\033[m");
	    move(15, j);prints("\033[37m│        │\033[m");
	    move(16, j);prints("\033[37m│●    ●│\033[m");
	    move(17, j);prints("\033[37m╰────╯\033[m");
	    break;
	case 5:
	    move(13, j);prints("\033[37m╭────╮\033[m");
	    move(14, j);prints("\033[37m│●    ●│\033[m");
	    move(15, j);prints("\033[37m│   ●   │\033[m");
	    move(16, j);prints("\033[37m│●    ●│\033[m");
	    move(17, j);prints("\033[37m╰────╯\033[m");
	    break;
	case 6:
	    move(13, j);prints("\033[37m╭────╮\033[m");
	    move(14, j);prints("\033[37m│●    ●│\033[m");
	    move(15, j);prints("\033[37m│●    ●│\033[m");
	    move(16, j);prints("\033[37m│●    ●│\033[m");
	    move(17, j);prints("\033[37m╰────╯\033[m");
	    break;
	}
    }
}

#define lockreturn0(unmode, state) if(lockutmpmode(unmode, state)) return 0

int dice_main(void) {
    char input[10],data[256], ch;
    dicedata_t table[256];
    int bet[3], index, money = 0, i, ya = 0, j, total, sig = 0;
    FILE *winfp/* , *lostfp */;
    
    more(DICE_TXT, NA);
    reload_money();
    if(cuser.money < 10){
	move(19,0);
	prints("\033[1;37m超過十元再來玩吧~~\033[m");
	pressanykey();
	return 0;
    }
    
    lockreturn0(DICE, LOCK_MULTI);
    winfp = fopen(DICE_WIN,"a");
    /*lostfp = fopen(DICE_LOST,"a");*/
    if(!winfp /*|| !lostfp*/)
	return 0;

    do {
	total = 0; i = 0;
	ch = 'y';
	clear();
	show_data();
	for(j = 0; j < 3; j++)
	    bet[j] = rand() % 6 + 1;
	
	for(j = 0; j < 100; j++)
	    value[j] = 0;

	while(1) {
	    move(19,0);
	    prints("\033[1;32m你現在有\033[1;31m %u \033[1;32mPtt$歐\033[m",
		   cuser.money);
	    getdata(20, 0, "\033[1;37m數字:加選 d:退選 s:開始或離開\033[m: ",
		    input, 5, LCECHO);
	    reload_money();
	    if(input[0] != 's' && input[0] != 'd' && cuser.money < 10) {
		move(21, 0);
		clrtoeol();
		prints("\033[1;37m超過十元才能賭~\033[m");
		continue;
	    }
	    if(input[0] == 'd' || input[0] == 'D') {
		del(i, table);
		continue;
	    }
	    if(input[0] == 's' || input[0] == 'S')
		break;
	    
	    if(!IsNum(input,strlen(input)))
		continue;
	    
	    index=atoi(input);
	    if(check_index(index) == 0)
		continue;
/*輸入錢的loop*/
	    while(1) {
		if(cuser.money < 10)
		    break;
		getdata(21, 0, "\033[1;32m賭多少錢呢\033[1;37m(大於10 小於500)"
			"\033[m: ", input, 9, LCECHO);
		if(!(money = IsLegal(input))||input[0] == '0')
		    continue;
		reload_money(); 
		if(money > cuser.money)
		    continue;
		for(j = 0, sig = 0; j < i; j++)
		    if(table[j].mybet == index) {
			if(table[j].mymoney == B_MAX)
			    sig = 2;
			else if(table[j].mymoney+money>B_MAX) {
			    sig = 1;
			    break;
			} else {
			    vice(money,"骰子");
			    table[j].mymoney += money;
			    j = -1;
			    break;
			}
		    }
		if(sig == 2)
		    break;
		if(sig == 1)
		    continue;
		if(j != -1) {
		    bzero((char*)&table[i], sizeof(dicedata_t));
		    table[i].mybet = index;
		    table[i++].mymoney = money;
		    vice(money,"骰子");
		}
		break;
	    }
	    reload_money(); 
	    move(19,0);
	    prints("\033[1;32m你現在有 \033[1;31m%u\033[1;32m Ptt$歐",
		   cuser.money);
	    if(sig != 2)
		show_count(index, money);
	}
	
	if(i == 0) {
	    fclose(winfp);
	    /*fclose(lostfp);*/
	    unlockutmpmode();
	    return 0;
	}
	
	show_output(bet);
	set_bingo(bet);

	for(j = 0; j < i; j++) {
	    if(table[j].mymoney <= 0)
		continue;
	    ya = bingo(table[j].mybet);
	    if(ya == 0) {
		/*sprintf(data, "%-15s 輸了 %-8d $", cuser.userid,
			table[j].mymoney);
		fprintf(lostfp, "%s\n", data);*/
		continue;
	    }
	    demoney(table[j].mymoney * ya + table[j].mymoney);
	    total += table[j].mymoney * ya;
		if (table[j].mymoney * ya > 500){   /* 超過500塊錢才做log 減少io */
	    	sprintf(data, "%-15s 押%-2d選項%-8d塊錢 中了%d倍 淨賺:%-8d\n",
		    	cuser.userid,table[j].mybet,
		   	    table[j].mymoney, ya, table[j].mymoney * ya);
	    	fputs(data,winfp);
	    }
	    ya = 0;
	}

	if(total > 0) {
	    move(21,0);
	    prints("\033[1;32m你贏了 \033[1;31m%d\033[1;32m Ptt$ 唷~~"
		   "                    \033[m", total);
	} else {
	    move(21,0);
	    clrtoeol();
	    prints("\033[1;32m真可惜 下次再來碰碰運氣吧\033[m");
	}
	
	move(19,0);
	clrtoeol();
	prints("\033[1;32m你現在有 \033[1;31m%u\033[1;32m Ptt$歐\033[m",
	       cuser.money);
	
	getdata(23, 0, "\033[1;32m繼續奮鬥[\033[1;37my/n\033[1;32m]\033[m: ",
		input, 2, LCECHO);
    } while(input[0] != 'n' && input[0] != 'N');
    fclose(winfp);
    /*fclose(lostfp);*/
    unlockutmpmode();
    return 0;
}
