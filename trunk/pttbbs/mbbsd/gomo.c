/* $Id: gomo.c,v 1.2 2002/04/28 19:35:29 in2 Exp $ */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include "config.h"
#include "pttstruct.h"
#include "gomo.h"
#include "common.h"
#include "modes.h"
#include "proto.h"

extern int usernum;
extern userinfo_t *currutmp;

char ku[BRDSIZ][BRDSIZ];
static char *chess[] = { "●", "○" };
static int tick, lastcount, mylasttick, hislasttick;

unsigned char  *pat, *adv;

typedef struct {
    char x;
    char y;
} Horder_t;

static Horder_t *v, pool[225];

static void HO_init() {
    memset(pool, 0, sizeof(pool));
    v = pool;
    pat = pat_gomoku;
    adv = adv_gomoku;
    memset(ku, 0, sizeof(ku));
}

static void HO_add(Horder_t *mv) {
    *v++ = *mv;
}

static void HO_undo(Horder_t *mv) {
    char *str = "┌┬┐├┼┤└┴┘";
    int n1, n2, loc;

    *mv = *(--v);
    ku[(int)mv->x][(int)mv->y] = BBLANK;
    BGOTO(mv->x, mv->y);
    n1 = (mv->x == 0) ? 0 : (mv->x == 14) ? 2 : 1;
    n2 = (mv->y == 14)? 0 : (mv->y == 0) ? 2 : 1;
    loc= 2 * ( n2 * 3 + n1);
    prints("%.2s", str + loc);
}

extern userec_t cuser;

static void HO_log(char *user) {
    int i;
    FILE *log;
    char buf[80];
    char buf1[80];
    char title[80];
    Horder_t *ptr = pool;
    extern screenline_t *big_picture;
    fileheader_t mymail;

    sprintf(buf, "home/%c/%s/F.%d", cuser.userid[0], cuser.userid,
	    rand() & 65535);
    log = fopen(buf, "w");
    
    for(i = 1; i < 17; i++)
	fprintf(log, "%.*s\n", big_picture[i].len, big_picture[i].data);
    
    i = 0;
    do {
	fprintf(log, "[%2d]%s ==> %c%d%c", i + 1, chess[i % 2],
	 	'A' + ptr->x, ptr->y + 1, (i % 2) ? '\n' : '\t');
	i++;
    } while( ++ptr < v);
    fclose(log);
    
    sethomepath(buf1, cuser.userid);
    stampfile(buf1, &mymail);
    
    mymail.savemode = 'H';        /* hold-mail flag */
    mymail.filemode = FILE_READ;
    strcpy(mymail.owner, "[備.忘.錄]");
    sprintf(mymail.title, "\033[37;41m棋譜\033[m %s VS %s",
	    cuser.userid, user);
    sethomedir(title, cuser.userid);
    Rename(buf, buf1);
    append_record(title, &mymail, sizeof(mymail));  
    
    unlink(buf);
}

static int countgomo() {
    Horder_t *ptr;
    int i;
    
    ptr = pool;
    i = 0;
    do {
	i++;
    } while(++ptr < v);
    return i;
}

static int chkmv(Horder_t *mv, int color, int limit) {
    char *xtype[] = {"\033[1;31m跳三\033[m", "\033[1;31m活三\033[m",
		     "\033[1;31m死四\033[m", "\033[1;31m跳四\033[m",
		     "\033[1;31m活四\033[m", "\033[1;31m四三\033[m",
		     "\033[1;31m雙三\033[m", "\033[1;31m雙四\033[m",
		     "\033[1;31m雙四\033[m", "\033[1;31m連六\033[m",
		     "\033[1;31m連五\033[m"};
    int rule = getstyle(mv->x, mv->y, color, limit);
    if(rule > 1 && rule < 13) {
	move(15, 40);
	outs(xtype[rule - 2]);
	bell();
    }
    return chkwin(rule, limit);
}

static int gomo_key(int fd, int ch, Horder_t *mv) {
    if( ch >= 'a' && ch <= 'o') {
        char pbuf[4], vx, vy;
	
        *pbuf = ch;
	if(fd)
	    add_io(0, 0);
        oldgetdata(17, 0, "直接指定位置 :", pbuf, sizeof(pbuf), DOECHO);
        if(fd)
	    add_io(fd, 0);
        vx = pbuf[0] - 'a';
        vy = atoi(pbuf + 1) - 1;
        if(vx >= 0 && vx < 15 && vy >= 0 && vy < 15 &&
	   ku[(int)vx][(int)vy] == BBLANK) {
	    mv->x = vx;
	    mv->y = vy;
	    return 1;
        }
    } else {
        switch(ch) {
	case KEY_RIGHT:
            mv->x = (mv->x == BRDSIZ - 1) ? mv->x : mv->x + 1;
            break;
	case KEY_LEFT:
            mv->x = (mv->x == 0 ) ? 0 : mv->x - 1;
            break;
	case KEY_UP:
            mv->y = (mv->y == BRDSIZ - 1) ? mv->y : mv->y + 1;
            break;
	case KEY_DOWN:
            mv->y = (mv->y == 0 ) ? 0 : mv->y - 1;
            break;
	case ' ':
	case '\r':
            if(ku[(int)mv->x][(int)mv->y] == BBLANK)
		return 1;
        }
    }
    return 0;
}

extern userec_t xuser;

static int reload_gomo() {
    passwd_query(usernum, &xuser);
    cuser.five_win = xuser.five_win;
    cuser.five_lose = xuser.five_lose;
    cuser.five_tie = xuser.five_tie;
    return 0;
}   

int gomoku(int fd) {
    Horder_t mv;
    int me, he, win, ch;
    int hewantpass, iwantpass;
    userinfo_t *my = currutmp;
    
    HO_init();
    me = !(my->turn) + 1;
    he = my->turn + 1;
    win = 1;
    tick=time(0) + MAX_TIME;
    lastcount = MAX_TIME;
    setutmpmode(M_FIVE);
    clear();
    
    prints("\033[1;46m  五子棋對戰  \033[45m%30s VS %-30s\033[m",
	   cuser.userid, my->mateid);
    show_file("etc/@five", 1, -1, ONLY_COLOR);
    move(11, 40);
    prints("我是 %s", me == BBLACK ? "先手 ●, 有禁手" : "後手 ○");
    move(16, 40);
    prints("\033[1;33m%s", cuser.userid);
    move(17, 40);
    prints("\033[1;33m%s", my->mateid);
    
    reload_gomo();
    move(16, 60);
    prints("\033[1;31m%d\033[37m勝 \033[34m%d\033[37m敗 \033[36m%d\033[37m和"
	   "\033[m", cuser.five_win, cuser.five_lose, cuser.five_tie);
    
    getuser(my->mateid);  
    move(17, 60);  
    prints("\033[1;31m%d\033[37m勝 \033[34m%d\033[37m敗 \033[36m%d\033[37m"
	   "和\033[m", xuser.five_win, xuser.five_lose, xuser.five_tie);
    
    cuser.five_lose++;
    /* 一進來先加一場敗場, 贏了後再扣回去, 避免快輸了惡意斷線 */
    passwd_update(usernum, &cuser);
    
    add_io(fd, 0);
    
    hewantpass = iwantpass = 0;
    mv.x = mv.y = 7;
    move(18, 40);
    prints("%s時間還剩%d:%02d\n", my->turn ? "你的" : "對方",
	   MAX_TIME / 60, MAX_TIME % 60);      
    for(;;) {
	move(13, 40);
	outs(my->turn ? "輪到自己下了!": "等待對方下子..");
	if(lastcount != tick-time(0)) {
	    lastcount = tick-time(0);
	    move(18, 40);
	    prints("%s時間還剩%d:%02d\n", my->turn ? "你的" : "對方",
		   lastcount / 60, lastcount % 60);
	    if(lastcount <= 0 && my->turn) {
		move(19, 40);
		outs("時間已到, 你輸了");
		my->five_lose++;
		send(fd, '\0', 1, 0);
		break;
	    }
	    if(lastcount <= -5 && !my->turn) {
		move(19, 40);
		outs("對手太久沒下, 你贏了!");
		win = 1;
		cuser.five_lose--;
		cuser.five_win++;
		my->five_win++;
		passwd_update(usernum, &cuser);
		mv.x = mv.y = -2;
		send(fd, &mv, sizeof(Horder_t), 0);
		mv = *(v - 1);		
		break;    		
	    }
	}
	move(14, 40);
	if(hewantpass) {
	    outs("\033[1;32m和棋要求!\033[m");
	    bell();
	} else
	    clrtoeol();
	BGOTOCUR(mv.x, mv.y);
	ch = igetkey();
	if(ch != I_OTHERDATA)
	    iwantpass = 0;
	if(ch == 'q') {
	    if(countgomo() < 10) {
		cuser.five_lose--;		 	
		passwd_update(usernum, &cuser);
	    }
	    send(fd, '\0', 1, 0);
	    break;
	} else if(ch == 'u' && !my->turn && v > pool) {
	    mv.x = mv.y = -1;
	    ch = send(fd, &mv, sizeof(Horder_t), 0);
	    if(ch == sizeof(Horder_t)) {
      		HO_undo(&mv);
		tick = mylasttick;
		my->turn = 1;
		continue;
	    } else 
		break;
	}
	if(ch == 'p') {
	    if(my->turn ) {
		if(iwantpass == 0) {
		    iwantpass = 1;
		    mv.x = mv.y = -2;
		    send(fd, &mv, sizeof(Horder_t), 0);
		    mv = *(v - 1);
		}
		continue;
	    } else if(hewantpass) {
		win = 0;
		cuser.five_lose--;
		cuser.five_tie++;
		my->five_tie++;
		passwd_update(usernum, &cuser);
		mv.x=mv.y=-2;
		send(fd, &mv, sizeof(Horder_t), 0);
		mv = *(v - 1);
		break;
	    }
	}
	
	if(ch == I_OTHERDATA) {
	    ch = recv(fd, &mv, sizeof(Horder_t), 0);
	    if(ch != sizeof(Horder_t)) {
		lastcount=tick-time(0);
		if(lastcount >=0) {
		    win = 1;
		    cuser.five_lose--;
		    if(countgomo() >=10) {
			cuser.five_win++;
			my->five_win++;
		    }
		    passwd_update(usernum, &cuser);
		    outmsg("對方認輸了!!");
		    break;
		} else {
		    win = 0;
		    outmsg("你超過時間未下子, 輸了!");
		    my->five_lose++;
		    break;
		}
	    } else if(mv.x == -2 && mv.y == -2) {
		if(iwantpass == 1) {
		    win = 0;
		    cuser.five_lose--;
		    cuser.five_tie++;
		    my->five_tie++;
		    passwd_update(usernum, &cuser);
		    break;
		} else {
		    hewantpass = 1;
		    mv = *(v - 1);
		    continue;
		}
	    }
	    if(my->turn && mv.x == -1 && mv.y == -1) {
		outmsg("對方悔棋");
		tick = hislasttick;
		HO_undo(&mv);
		my->turn = 0;
		continue;
	    }
	    
	    if(!my->turn) {
		win = chkmv(&mv, he, he == BBLACK);
		HO_add(&mv);
		hislasttick = tick;
		tick = time(0) + MAX_TIME;
		ku[(int)mv.x][(int)mv.y] = he;
		bell();
		BGOTO(mv.x, mv.y);
		outs(chess[he - 1]);
		
		if(win) {
		    outmsg(win == 1 ? "對方贏了!" : "對方禁手");
		    if(win != 1) {
			cuser.five_lose--;
			cuser.five_win++;
			my->five_win++;
			passwd_update(usernum, &cuser);
		    } else
		    	my->five_lose++;
		    win = -win;
		    break;
		}
		my->turn = 1;
	    }
	    continue;
	}
	
	if(my->turn) {
	    if(gomo_key(fd, ch, &mv))
		my->turn = 0;
	    else
		continue;
	    
	    if(!my->turn) {
		HO_add(&mv);
		BGOTO(mv.x, mv.y);
		outs(chess[me - 1]);
		win = chkmv( &mv, me, me == BBLACK);
		ku[(int)mv.x][(int)mv.y] = me;
		mylasttick = tick;
		tick = time(0) + MAX_TIME;   /*倒數*/
		lastcount = MAX_TIME;
		if(send(fd, &mv, sizeof(Horder_t), 0) != sizeof(Horder_t))
		    break;
		if(win) {
		    outmsg(win == 1 ? "我贏囉~~" : "禁手輸了" );
		    if(win == 1) {
			cuser.five_lose--;
			cuser.five_win++;
			my->five_win++;
			passwd_update(usernum, &cuser);
		    } else 
		    	my->five_lose++;
		    break;
		}
		move(15, 40);
		clrtoeol();
	    }
	}
    }
    add_io(0, 0);
    close(fd);
    
    igetch();
    if(v > pool) { 
	char ans[4];
	
	getdata(19 , 0, "要保留本局成棋譜嗎?(y/N)", ans, sizeof(ans), LCECHO);
	if(*ans == 'y')
	    HO_log(my->mateid);
    }
    return 0;
}
