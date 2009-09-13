/* $Id$ */
#include "bbs.h"

#define lockreturn(unmode, state)  if(lockutmpmode(unmode, state)) return
#define lockreturn0(unmode, state) if(lockutmpmode(unmode, state)) return 0
#define lockbreak(unmode, state)   if(lockutmpmode(unmode, state)) break
#define SONGBOOK  "etc/SONGBOOK"
#define OSONGPATH "etc/SONGO"
#define ORDER_SONG_COST   (200)	// how much to order a song

#define MAX_SONGS (MAX_MOVIE-100) // (400) XXX MAX_SONGS should be fewer than MAX_MOVIE.

static void sortsong(void);

static int
do_order_song(void)
{
    char            sender[IDLEN + 1], receiver[IDLEN + 1], buf[200],
		    genbuf[200], filename[256], say[51];
    char            trans_buffer[PATHLEN];
    char            address[45];
    FILE           *fp, *fp1;
    fileheader_t    mail;
    int             nsongs;
    char save_title[STRLEN];

    strlcpy(buf, Cdatedate(&now), sizeof(buf));

    lockreturn0(OSONG, LOCK_MULTI);
    pwcuReload();

    /* Jaky 一人一天點一首 */
    if (!strcmp(buf, Cdatedate(&cuser.lastsong)) && !HasUserPerm(PERM_SYSOP)) {
	move(22, 0);
	vmsg("你今天已經點過囉，明天再點吧....");
	unlockutmpmode();
	return 0;
    }

    while (1) {
	char ans[4];
	move(12, 0);
	clrtobot();
	prints("親愛的 %s 歡迎來到自動點歌系統\n\n", cuser.userid);
	outs(ANSI_COLOR(1) "注意點歌內容請勿涉及謾罵 人身攻擊 猥褻"
	     "公然侮辱 誹謗\n"
	     "若有上述違規情形，站方將保留決定是否公開播放的權利\n"
	     "如不同意請按 (3) 離開。" ANSI_RESET "\n");
	getdata(18, 0, 
#ifdef USE_PFTERM
		"請選擇 " ANSI_COLOR(1) "1)" ANSI_RESET " 開始點歌、"
		ANSI_COLOR(1) "2)" ANSI_RESET " 看歌本、"
		"或是 " ANSI_COLOR(1) "3)" ANSI_RESET " 離開: ",
#else
		"請選擇 1)開始點歌 2)看歌本 3)離開: ",
#endif
		ans, sizeof(ans), DOECHO);

	if (ans[0] == '1')
	    break;
	else if (ans[0] == '2') {
	    a_menu("點歌歌本", SONGBOOK, 0, 0, NULL);
	    clear();
	}
	else if (ans[0] == '3') {
	    vmsg("謝謝光臨 :)");
	    unlockutmpmode();
	    return 0;
	}
    }

    reload_money();
    if (cuser.money < ORDER_SONG_COST) {
	move(22, 0);
	vmsgf("點歌要 %d 元唷!....", ORDER_SONG_COST);
	unlockutmpmode();
	return 0;
    }

    getdata_str(19, 0, "點歌者(可匿名): ", sender, sizeof(sender), DOECHO, cuser.userid);
    getdata(20, 0, "點給(可匿名): ", receiver, sizeof(receiver), DOECHO);

    getdata_str(21, 0, "想要要對他(她)說..:", say,
		sizeof(say), DOECHO, "我愛妳..");
    snprintf(save_title, sizeof(save_title),
	     "%s:%s", sender, say);
    getdata_str(22, 0, "寄到誰的信箱(真實 ID 或 E-mail)?",
		address, sizeof(address), LCECHO, receiver);
    vmsg("接著要選歌囉..進入歌本好好的選一首歌吧..^o^");
    a_menu("點歌歌本", SONGBOOK, 0, 0, trans_buffer);
    if (!trans_buffer[0] || strstr(trans_buffer, "home") ||
	strstr(trans_buffer, "boards") || !(fp = fopen(trans_buffer, "r"))) {
	unlockutmpmode();
	return 0;
    }
#ifdef DEBUG
    vmsg(trans_buffer);
#endif
    strlcpy(filename, OSONGPATH, sizeof(filename));

    stampfile(filename, &mail);

    unlink(filename);

    if (!(fp1 = fopen(filename, "w"))) {
	fclose(fp);
	unlockutmpmode();
	return 0;
    }
    strlcpy(mail.owner, "點歌機", sizeof(mail.owner));
    snprintf(mail.title, sizeof(mail.title), "◇ %s 點給 %s ", sender, receiver);

    while (fgets(buf, sizeof(buf), fp)) {
	char           *po;
	if (!strncmp(buf, "標題: ", 6)) {
	    clear();
	    move(10, 10);
	    outs(buf);
	    pressanykey();
	    fclose(fp);
	    fclose(fp1);
	    unlockutmpmode();
	    return 0;
	}
	while ((po = strstr(buf, "<~Src~>"))) {
	    const char *dot = "";
	    if (is_validuserid(sender) && strcmp(sender, cuser.userid) != 0)
		dot = ".";
	    po[0] = 0;
	    snprintf(genbuf, sizeof(genbuf), "%s%s%s%s", buf, sender, dot, po + 7);
	    strlcpy(buf, genbuf, sizeof(buf));
	}
	while ((po = strstr(buf, "<~Des~>"))) {
	    po[0] = 0;
	    snprintf(genbuf, sizeof(genbuf), "%s%s%s", buf, receiver, po + 7);
	    strlcpy(buf, genbuf, sizeof(buf));
	}
	while ((po = strstr(buf, "<~Say~>"))) {
	    po[0] = 0;
	    snprintf(genbuf, sizeof(genbuf), "%s%s%s", buf, say, po + 7);
	    strlcpy(buf, genbuf, sizeof(buf));
	}
	fputs(buf, fp1);
    }
    fclose(fp1);
    fclose(fp);

    log_filef("etc/osong.log",  LOG_CREAT, "id: %-12s ◇ %s 點給 %s : \"%s\", 轉寄至 %s\n", cuser.userid, sender, receiver, say, address);

    if (append_record(OSONGPATH "/" FN_DIR, &mail, sizeof(mail)) != -1) {
	pwcuSetLastSongTime(now);
	/* Jaky 超過 MAX_MOVIE 首歌就開始砍 */
	// XXX 載入的順序會長得像是:
	// 3. ◆ <系統> 動態看板   SYSOP [01/23/08]
	// 4. ◆ <點歌> 動態看板   Ptt   [08/26/09]
	// 5. ◆ <廣告> 動態看板   SYSOP [08/22/09]
	// 6. ◆ <看板> 動態看板   SYSOP [04/16/09]
	// 由於點歌部份算是早載入的，不能直接用 MAX_MOVIE 不然後面都沒得玩。
	nsongs = get_num_records(OSONGPATH "/" FN_DIR, sizeof(mail));
	if (nsongs > MAX_SONGS) {
	    // XXX race condition
	    delete_range(OSONGPATH "/" FN_DIR, 1, nsongs - MAX_SONGS);
	}
	snprintf(genbuf, sizeof(genbuf), "%s says \"%s\" to %s.", 
		sender, say, receiver);
	log_usies("OSONG", genbuf);
	vice(ORDER_SONG_COST, "點歌");
    }
    snprintf(save_title, sizeof(save_title), "%s:%s", sender, say);
    hold_mail(filename, receiver, save_title);

    if (address[0]) {
	bsmtp(filename, save_title, address, NULL);
    }
    clear();
    outs(
	 "\n\n  恭喜您點歌完成囉...\n"
	 "  一小時內動態看板會自動重新更新，\n"
	 "  大家就可以看到您點的歌囉！\n\n"
	 "  點歌有任何問題可以到 " BN_NOTE " 板的精華區找答案，\n"
	 "  也可在 " BN_NOTE " 板精華區看到自己的點歌記錄。\n"
	 "  有任何寶貴的意見也歡迎到 " BN_NOTE " 看板留話，\n"
	 "  讓親切的板主為您服務。\n");
    pressanykey();
    sortsong();
    topsong();

    unlockutmpmode();
    return 1;
}

int
ordersong(void)
{
    do_order_song();
    return 0;
}

// topsong

#define QCAST     int (*)(const void *, const void *)

typedef struct songcmp_t {
    char            name[100];
    char            cname[100];
    int             count;
}               songcmp_t;


static int
count_cmp(songcmp_t * b, songcmp_t * a)
{
    return (a->count - b->count);
}

int
topsong(void)
{
    more(FN_TOPSONG, YEA);
    return 0;
}


static void
sortsong(void)
{
    FILE           *fo, *fp = fopen(BBSHOME "/" FN_USSONG, "r");
    songcmp_t       songs[MAX_SONGS + 1];
    int             n;
    char            buf[256], cbuf[256];
    int totalcount = 0;

    memset(songs, 0, sizeof(songs));
    if (!fp)
	return;
    if (!(fo = fopen(FN_TOPSONG, "w"))) {
	fclose(fp);
	return;
    }
    totalcount = 0;
    /* XXX: 除了前 MAX_SONGS 首, 剩下不會排序 */
    while (fgets(buf, 200, fp)) {
	chomp(buf);
	strip_blank(cbuf, buf);
	if (!cbuf[0] || !isprint2((int)cbuf[0]))
	    continue;

	for (n = 0; n < MAX_SONGS && songs[n].name[0]; n++)
	    if (!strcmp(songs[n].cname, cbuf))
		break;
	strlcpy(songs[n].name, buf, sizeof(songs[n].name));
	strlcpy(songs[n].cname, cbuf, sizeof(songs[n].cname));
	songs[n].count++;
	totalcount++;
    }
    qsort(songs, MAX_SONGS, sizeof(songcmp_t), (QCAST) count_cmp);
    fprintf(fo,
	    "    " ANSI_COLOR(36) "──" ANSI_COLOR(37) "名次" ANSI_COLOR(36) "──────" ANSI_COLOR(37) 
	    "歌名" ANSI_COLOR(36) "───────────" ANSI_COLOR(37) "次數" ANSI_COLOR(36)
	    "──" ANSI_COLOR(32) "共%d次" ANSI_COLOR(36) "──" ANSI_RESET "\n", totalcount);
    for (n = 0; n < 100 && songs[n].name[0]; n++) {
	fprintf(fo, "      %5d. %-38.38s %4d " ANSI_COLOR(32) "[%.2f]" ANSI_RESET "\n", n + 1,
		songs[n].name, songs[n].count,
		(float)songs[n].count / totalcount);
    }
    fclose(fp);
    fclose(fo);
}
