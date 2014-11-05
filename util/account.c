
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

#include <cmsys.h>
#include <cmbbs.h>
#include <common.h>
#include <ansi.h>
#include <var.h>

#define ADJUST_M        6	/* adjust back 5 minutes */
#define ACT_ELEMENTS    27

const char act_file[] = ".act";

void
reset_garbage(void)
{
    if (SHM == NULL) {
	attach_SHM();
	if (SHM->Ptouchtime == 0)
	    SHM->Ptouchtime = 1;
    }
    /*
     * 不整個reload? for(n=0;n<=SHM->last_film;n++) printf("\n**%d**\n %s
     * \n",n,SHM->notes[n]);
     */
    SHM->Puptime = 0;
    reload_pttcache();

    printf("\n動態看板數[%d]\n", SHM->last_film);
    /*
     * for(n=0; n<MAX_MOVIE_SECTION; n++) printf("sec%d=> 起點:%d
     * 下次要換的:%d\n ",n,SHM->n_notes[n], SHM->next_refresh[n]);
     * printf("\n");
     */
}

static void
keeplog(const char *fpath, const char *board, const char *title, const char *sym)
{
    fileheader_t    fhdr;
    int             bid;
    char            genbuf[PATHLEN], buf[256];

    if (!board)
	board = BN_RECORD;

    setbpath(genbuf, board);
    stampfile(genbuf, &fhdr);
    Rename(fpath, genbuf);

    if (sym) {
	char genbuf2[PATHLEN];
	strcpy(genbuf2, "../");
	strlcat(genbuf2, genbuf, sizeof(genbuf2));
        sprintf(buf, "log/%s", sym);
        unlink(buf);
        symlink(genbuf2, buf);
    }
    /*
     * printf("keep record:[%s][%s][%s][%s]\n",fpath, board, title,genbuf);
     */
    strlcpy(fhdr.title, title, sizeof(fhdr.title));
    strlcpy(fhdr.owner, "[歷史老師]", sizeof(fhdr.owner));
    setbfile(genbuf, board, FN_DIR);
    append_record(genbuf, &fhdr, sizeof(fhdr));
    /* XXX: bid of cache.c's getbnum starts from 1 */
    if ((bid = getbnum(board)) > 0)
	touchbtotal(bid);
}


// moves "source" to "target""suffix", then gzips it
static void 
gzip(const char *source, const char *target, const char *suffix)
{
    char buf[PATHLEN];
    snprintf(buf, sizeof(buf), "gzip -f9n adm/%s%s", target, suffix);
    Rename(source, &buf[14]);
    system(buf);
}

static int
parse_usies(const char *fn, int *act)
{
    char buf[256], *p;
    FILE *fp;

    // load parsed result
    if ((fp = fopen(act_file, "r")) != NULL) {
	fread(act, sizeof(act[0]), ACT_ELEMENTS, fp);
	fclose(fp);
    }

    // parse "usies"
    if ((fp = fopen(fn, "r")) == NULL) {
	printf("cann't open usies\n");
	return 1;
    }

    if (act[26]) {
        if (dashs("usies") >= act[26]) {
            fseek(fp, act[26], SEEK_SET);
        } else {
            // sorry, file is changed. let's re-parse.
            printf("%s (%ld) corrupted (act: %d).\n", act_file, dashs(act_file),
                   act[26]);
            memset(act, 0, sizeof(act[0]) * ACT_ELEMENTS);
            rewind(fp);
        }
    }

    while (fgets(buf, sizeof(buf), fp)) {
	int hour;
	buf[11 + 2] = 0;
	hour = atoi(buf + 11);
	if (hour < 0 || hour > 23) {
	    continue;
	}
	//"09/06/1999 17:44:58 Mon "
	// 012345678901234567890123
	if (strstr(buf + 20, "ENTER")) {
	    act[hour]++;
	    continue;
	}
	if ((p = strstr(buf + 40, "Stay:"))) {
	    if ((hour = atoi(p + 5))) {
		act[24] += hour;
		act[25]++;
	    }
	    continue;
	}
    }
    act[26] = ftell(fp);
    fclose(fp);

    // write parsed result
    if ((fp = fopen(act_file, "w"))) {
	fwrite(act, sizeof(act[0]), ACT_ELEMENTS, fp);
	fclose(fp);
    }

    return 0;
}

static int
output_today(struct tm *ptime, int *act, int peak_hour_login, int day_login)
{
    char buf[256];
    int i;
    FILE *fp;

    // each bar: 01 [BAR] ... number\n
    int number_len = sprintf(buf, " %d\n", peak_hour_login);
    int bar_max_width = 79 - 3 - number_len;

    // assume we want to make avg in middle,
    int avg_len = ((day_login - peak_hour_login) / 23) + 1;
    int bar_unit = avg_len / (bar_max_width / 2 / 2);

    if (bar_unit < 1)
        bar_unit = 1;

    printf("peak: %d, day_login: %d\n", peak_hour_login, day_login);
    printf("avg_len=%d, bar_unit=%d\n", avg_len, bar_unit);

    if ((fp = fopen("etc/today", "w")) == NULL) {
	printf("can't open etc/today\n");
	return 1;
    }

    fprintf(fp, "\t\t\t" ANSI_COLOR(1;33;46)
            "每小時上站人次統計 [%04d/%02d/%02d]" ANSI_RESET "\n\n", 
	    ptime->tm_year + 1900, ptime->tm_mon + 1, ptime->tm_mday);

    for (i = 0; i < 24; i++) {
        int hour_count = act[i];
        int bars = hour_count / bar_unit + 1;
        fprintf(fp, ANSI_COLOR(1;32) "%02d %s" , i,
                i % 2 ? ANSI_COLOR(36) : ANSI_COLOR(34));

        // render the bar
        if (bars*2 > bar_max_width)
            bars = bar_max_width/2;
        while (bars-- > 0)
            fputs("█", fp);

        // print number and newline
        fputs(ANSI_COLOR(33), fp);
        fprintf(fp, " %d" ANSI_RESET "\n", hour_count);
    }

    fprintf(fp, ANSI_COLOR(1;36) "\n  總共上站人次：" ANSI_COLOR(37) "%-7d"
            ANSI_COLOR(36) " 平均使用人數：" ANSI_COLOR(37) "%d\n",
	    day_login, day_login / 24);
    fclose(fp);

    return 0;
}

static int
update_history(struct tm *ptime, int peak_hour, int peak_hour_login, int day_login)
{
    int max_hour_login = 0, max_day_login = 0, max_reg = 0, max_online = 0;
    int peak_online = 0;
    FILE *fp, *fp1;
    time_t t;
    struct tm tm;

    if ((fp = fopen("etc/history.data", "r+")) == NULL)
	return 1;

    /* 最多同時上線 */
    if (fscanf(fp, "%d %d %d %d", &max_day_login, &max_hour_login, &max_reg, &max_online) != 4)
	goto out;

    if ((fp1 = fopen("etc/history", "a")) == NULL)
	goto out;

    resolve_fcache();
    peak_online = SHM->max_user;
    printf("此時段最多同時上線:%d 過去:%d\n", peak_online, max_online);

    if (peak_online > max_online) {
	localtime4_r(&SHM->max_time, &tm);
	fprintf(fp1, "◎ 【%02d/%02d/%02d %02d:%02d】"
		ANSI_COLOR(32) "同時在坊內人數" ANSI_RESET "首次達到 " ANSI_COLOR(1;36) "%d" ANSI_RESET " 人次\n",
		tm.tm_mon + 1, tm.tm_mday, tm.tm_year % 100, tm.tm_hour, tm.tm_min, peak_online);
	max_online = peak_online;
    }

    t = mktime(ptime) + ADJUST_M * 60;
    localtime_r(&t, &tm);

    if (tm.tm_hour == 0) {
	if (peak_hour_login > max_hour_login) {
	    fprintf(fp1, "◇ 【%02d/%02d/%02d %02d】   "
		    ANSI_COLOR(1;32) "單一小時上線人次" ANSI_RESET "首次達到 " ANSI_COLOR(1;35) "%d" ANSI_RESET " 人次\n",
		    ptime->tm_mon + 1, ptime->tm_mday, ptime->tm_year % 100, peak_hour, peak_hour_login);
	    max_hour_login = peak_hour_login;
	}
	if (day_login > max_day_login) {
	    fprintf(fp1, "◆ 【%02d/%02d/%02d】      "
		    ANSI_COLOR(1;32) "單日上線人次" ANSI_RESET "首次達到 " ANSI_COLOR(1;33) "%d" ANSI_RESET " 人次\n",
		    ptime->tm_mon + 1, ptime->tm_mday, ptime->tm_year % 100, day_login);
	    max_day_login = day_login;
	}
	if (SHM->number > max_reg + max_reg / 10) {
	    fprintf(fp1, "★ 【%02d/%02d/%02d】      "
		    ANSI_COLOR(1;32) "總註冊人數" ANSI_RESET "提升到 " ANSI_COLOR(1;31) "%d" ANSI_RESET " 人\n",
		    ptime->tm_mon + 1, ptime->tm_mday, ptime->tm_year % 100, SHM->number);
	    max_reg = SHM->number;
	}
    }

    fclose(fp1);

out:
    rewind(fp);
    fprintf(fp, "%d %d %d %d", max_day_login, max_hour_login, max_reg, peak_online);
    fclose(fp);

    return 0;
}

static int
update_holiday_list(struct tm *ptime)
{
    const char wday_str[] = "UMTWRFS";
    char buf[256];
    int i = 0;
    FILE *fp, *fp1;

    if ((fp1 = fopen("etc/today_is", "w")) == NULL)
	return 1;

    if ((fp = fopen("etc/feast", "r")) == NULL)
	goto none;

    while (fgets(buf, sizeof(buf), fp)) {
	int mo;
	char a, b;

        // buf has new line.
        chomp(buf);
        trim(buf);

	if (buf[0] != '#' && sscanf(buf, "%d %c%c", &mo, &a, &b) == 3 && isdigit(a)) {
	    if (isdigit(b)) {
		int da = 10 * (a - '0') + (b - '0');
		if (ptime->tm_mday == da && ptime->tm_mon + 1 == mo) {
		    i = 1;
		    fprintf(fp1, "%-14.14s", &buf[6]);
                    break;
		}
	    } else if (a - '0' <= 4) {
		int wday = 0;
		b = toupper(b);
		while (wday < 7 && b != *(wday_str + wday))
		    wday++;
		if (ptime->tm_mon + 1 == mo && ptime->tm_wday == wday &&
			(ptime->tm_mday - 1) / 7 + 1 == (a - '0')) {
		    i = 1;
		    fprintf(fp1, "%-14.14s", &buf[6]);
                    break;
		}
	    }
	}
    }
    fclose(fp);

none:
    if (i > 0)
	goto out;

    if ((fp = fopen("etc/today_boring", "r"))) {
	while (fgets(buf, sizeof(buf), fp))
	    if (strlen(buf) > 3)
		fprintf(fp1, "%s", buf);
	fclose(fp);
    } else
	fprintf(fp1, "本日節日徵求中");

out:
    fclose(fp1);

    return 0;
}

static int
update_holiday(struct tm *ptime)
{
    int i = 0;
    char buf[128];
    FILE *fp;

    if ((fp = fopen("etc/today_is", "r")) == NULL)
	return 1;

    // I'm not sure what this is supposed to (check w/wens),
    // but since this is buggy now, let's make it as "rotate through".
    buf[0] = 0;
    for (i = 0; i <= ptime->tm_hour; i++) {
	if (!fgets(buf, sizeof(buf), fp)) {
            rewind(fp);
            fgets(buf, sizeof(buf), fp);
	}
    }

    fclose(fp);
    chomp(buf);

    memset(SHM->today_is, 0, sizeof(SHM->today_is));
    strlcpy(SHM->today_is, buf, sizeof(SHM->today_is));

    return 0;
}

int
main(/*int argc, char **argv*/)
{
    int             i;
    int             mo, da;
    int             peak_hour_login, peak_hour, day_login;
    const char log_file[] = "usies";
    char            buf[256], buf1[256];
    FILE           *fp;
    int             act[ACT_ELEMENTS];	/* 次數/累計時間/pointer */
    time_t          now;
    struct tm       tm_now, tm_adjusted;

    attach_SHM();
    nice(10);
    chdir(BBSHOME);

    now = time(NULL);
    localtime_r(&now, &tm_now);
    now -= ADJUST_M * 60;
    localtime_r(&now, &tm_adjusted);

    memset(act, 0, sizeof(act));

    printf("次數/累計時間\n");
    parse_usies(log_file, act);

    peak_hour_login = peak_hour = 0;
    day_login = 0;
    for (i = 0; i < 24; i++) {
	day_login += act[i];
	if (act[i] > peak_hour_login) {
	    peak_hour_login = act[i];
	    peak_hour = i;
	}
    }

    /* -------------------------------------------------------------- */
    printf("上站人次統計\n");
    output_today(&tm_adjusted, act, peak_hour_login, day_login);
    /* -------------------------------------------------------------- */

    printf("歷史事件處理\n");
    /* Ptt 歷史事件處理 */
    update_history(&tm_adjusted, peak_hour, peak_hour_login, day_login);

    if (tm_now.tm_hour == 0) {
        Copy("etc/today", "etc/yesterday");
	/* system("rm -f note.dat"); */

	keeplog("etc/today", BN_RECORD, "上站人次統計", NULL);
	keeplog(FN_MONEY, BN_SECURITY, "本日金錢往來記錄", NULL);
	keeplog("etc/illegal_money", BN_SECURITY, "本日違法賺錢記錄", NULL);
	keeplog("etc/osong.log", BN_SECURITY, "本日點歌記錄", NULL);
	keeplog("etc/chicken", BN_RECORD, "雞場報告", NULL);

        // Restore etc/yesterday because keeplog removes it.
        Copy("etc/yesterday", "etc/today");

	snprintf(buf, sizeof(buf), "[安全報告] 使用者上線監控 [%02d/%02d:%02d]",
		tm_now.tm_mon + 1, tm_now.tm_mday, tm_now.tm_hour);
	keeplog("usies", "Security", buf, "usies");
        // usies is removed - so we have to also delete .act
        remove(act_file);
	printf("[安全報告] 使用者上線監控\n");

	sprintf(buf, "-%02d%02d%02d",
		tm_adjusted.tm_year % 100, tm_adjusted.tm_mon + 1, tm_adjusted.tm_mday);

	printf("壓縮使用者上線監控\n");
	gzip(log_file, "usies", buf);
	gzip("etc/mailog", "mailog", buf);

	/* Ptt 節日處理 */
	printf("節日處理\n");
	update_holiday_list(&tm_now);

	/* Ptt 歡迎畫面處理 */
	printf("歡迎畫面處理\n");

	if ((fp = fopen("etc/Welcome.date", "r"))) {
	    char            temp[50];
	    while (fscanf(fp, "%d %d %s\n", &mo, &da, buf1) == 3) {
		if (tm_now.tm_mday == da && tm_now.tm_mon + 1 == mo) {
		    strcpy(temp, buf1);
		    sprintf(buf1, "cp -f etc/Welcomes/%s etc/Welcome", temp);
		    system(buf1);
		    break;
		}
	    }
	    fclose(fp);
	}

	if (tm_now.tm_wday == 0)
	    keeplog("etc/week", BN_RECORD, "本週熱門話題", NULL);

	if (tm_now.tm_mday == 1)
	    keeplog("etc/month", BN_RECORD, "本月熱門話題", NULL);

	if (tm_now.tm_yday == 1)
	    keeplog("etc/year", BN_RECORD, "年度熱門話題", NULL);
    } else if (tm_now.tm_hour == 3 && tm_now.tm_wday == 6) {
	const char fn1[] = "tmp";
	const char fn2[] = "suicide";

	rename(fn1, fn2);
	Mkdir(fn1);
	sprintf(buf, "tar cfz adm/%s-%02d%02d%02d.tgz %s",
		fn2, tm_now.tm_year % 100, tm_now.tm_mon + 1, tm_now.tm_mday, fn2);
	system(buf);
	sprintf(buf, "/bin/rm -fr %s", fn2);
	system(buf);
    }

    /* Ptt reset Ptt's share memory */
    printf("重設cache 與fcache\n");

    update_holiday(&tm_now);

    SHM->Puptime = 0;
    resolve_fcache();
    reset_garbage();
    printf("動態看板資訊: last_usong=%d, last_film=%d\n", 
	    SHM->last_usong, SHM->last_film);

    printf("計算進站畫面數: ");
    for (i = 0; i < 5; ++i) {
	sprintf(buf, "etc/Welcome_login.%d", i);
	if (access(buf, 0) < 0)
	    break;
    }
    printf("%d\n", SHM->GV2.e.nWelcomes = i);
    return 0;
}
