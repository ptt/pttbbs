/* $Id$ */

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

// test
#define ACCOUNT_MAX_LINE        16
#define ADJUST_M        6	/* adjust back 5 minutes */

void
reset_garbage(void)
{
    if (SHM == NULL) {
	attach_SHM();
	if (SHM->Ptouchtime == 0)
	    SHM->Ptouchtime = 1;
    }
    /*
     * ¤£¾ã­Óreload? for(n=0;n<=SHM->last_film;n++) printf("\n**%d**\n %s
     * \n",n,SHM->notes[n]);
     */
    SHM->Puptime = 0;
    reload_pttcache();

    printf("\n°ÊºA¬ÝªO¼Æ[%d]\n", SHM->last_film);
    /*
     * for(n=0; n<MAX_MOVIE_SECTION; n++) printf("sec%d=> °_ÂI:%d
     * ¤U¦¸­n´«ªº:%d\n ",n,SHM->n_notes[n], SHM->next_refresh[n]);
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
        sprintf(buf, "log/%s", sym);
        unlink(buf);
        symlink(genbuf, buf);
    }
    /*
     * printf("keep record:[%s][%s][%s][%s]\n",fpath, board, title,genbuf);
     */
    strlcpy(fhdr.title, title, sizeof(fhdr.title));
    strlcpy(fhdr.owner, "[¾ú¥v¦Ñ®v]", sizeof(fhdr.owner));
    setbfile(genbuf, board, FN_DIR);
    append_record(genbuf, &fhdr, sizeof(fhdr));
    /* XXX: bid of cache.c's getbnum starts from 1 */
    if ((bid = getbnum(board)) > 0)
	touchbtotal(bid);
}


static void
my_outs(FILE *fp, char *buf, char mode)
{
    static char     state = '0';

    if (state != mode)
	fprintf(fp, "[3%cm", state = mode);
    if (buf[0]) {
	fputs(buf, fp);
	buf[0] = 0;
    }
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
parse_usies(const char *fn, struct tm *ptime, int act[27])
{
    const char const act_file[] = ".act";
    char buf[256], *p;
    FILE *fp;

    // load parsed result
    if ((ptime->tm_hour != 0) && (fp = fopen(act_file, "r"))) {
	fread(act, sizeof(act), 1, fp);
	fclose(fp);
    }

    // parse "usies"
    if ((fp = fopen(fn, "r")) == NULL) {
	printf("cann't open usies\n");
	return 1;
    }

    if (act[26])
	fseek(fp, act[26], 0);

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
	fwrite(act, sizeof(act), 1, fp);
	fclose(fp);
    }

    return 0;
}

static int
output_today(struct tm *ptime, int act[27], int peak_hour_login, int day_login)
{
    char buf[256];
    int item = peak_hour_login / ACCOUNT_MAX_LINE + 1;
    int per_hour_unit = 100;
    int i, j;
    FILE *fp;

    if ((fp = fopen("etc/today", "w")) == NULL) {
	printf("can't open etc/today\n");
	return 1;
    }

    fprintf(fp, "\t\t\t" ANSI_COLOR(1;33;46) "¨C¤p®É¤W¯¸¤H¦¸²Î­p [%02d/%02d/%02d] " ANSI_COLOR(40) "\n\n", 
	    ptime->tm_year % 100, ptime->tm_mon + 1, ptime->tm_mday);

    for (i = ACCOUNT_MAX_LINE + 1; i > 0; i--) {
	strcpy(buf, "   ");

	for (j = 0; j < 24; j++) {
	    int hour_count = act[j];
	    int max = item * i;
	    if (hour_count && (hour_count < max) && (max <= hour_count + item)) {
		my_outs(fp, buf, '3');
		fprintf(fp, "%-3d", hour_count / per_hour_unit);
	    } else if (max <= hour_count) {
		my_outs(fp, buf, '4');
		fprintf(fp, "¢i ");
	    } else
		strcat(buf, "   ");
	}

	fprintf(fp, "\n");
    }

    fprintf(fp, "   " ANSI_COLOR(32)
	    "0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23\n\n"
	    "\t      " ANSI_COLOR(34) "³æ¦ì: " ANSI_COLOR(37) "%d" ANSI_COLOR(34) " ¤H", per_hour_unit);
    fprintf(fp, "  Á`¦@¤W¯¸¤H¦¸¡G" ANSI_COLOR(37) "%-7d" ANSI_COLOR(34) "¥­§¡¨Ï¥Î¤H¼Æ¡G" ANSI_COLOR(37) "%d\n",
	    day_login, day_login / 24);
    fclose(fp);

    return 0;
}

static int
update_history(struct tm *ptime, int peak_hour, int peak_hour_login, int day_login)
{
    int max_hour_login = 0, max_day_login = 0, max_reg = 0, max_online = 0;
    int peak_online;
    FILE *fp, *fp1;
    time_t t;
    struct tm tm;

    if ((fp = fopen("etc/history.data", "r+")) == NULL)
	return 1;

    /* ³Ì¦h¦P®É¤W½u */
    if (fscanf(fp, "%d %d %d %d", &max_day_login, &max_hour_login, &max_reg, &max_online) != 4)
	goto out;

    if ((fp1 = fopen("etc/history", "a")) == NULL)
	goto out;

    resolve_fcache();
    peak_online = SHM->max_user;
    printf("¦¹®É¬q³Ì¦h¦P®É¤W½u:%d ¹L¥h:%d\n", peak_online, max_online);

    if (peak_online > max_online) {
	localtime4_r(&SHM->max_time, &tm);
	fprintf(fp1, "¡· ¡i%02d/%02d/%02d %02d:%02d¡j"
		ANSI_COLOR(32) "¦P®É¦b§{¤º¤H¼Æ" ANSI_RESET "­º¦¸¹F¨ì " ANSI_COLOR(1;36) "%d" ANSI_RESET " ¤H¦¸\n",
		tm.tm_mon + 1, tm.tm_mday, tm.tm_year % 100, tm.tm_hour, tm.tm_min, peak_online);
	max_online = peak_online;
    }

    t = mktime(ptime) + ADJUST_M * 60;
    localtime_r(&t, &tm);

    if (tm.tm_hour == 0) {
	if (peak_hour_login > max_hour_login) {
	    fprintf(fp1, "¡º ¡i%02d/%02d/%02d %02d¡j   "
		    ANSI_COLOR(1;32) "³æ¤@¤p®É¤W½u¤H¦¸" ANSI_RESET "­º¦¸¹F¨ì " ANSI_COLOR(1;35) "%d" ANSI_RESET " ¤H¦¸\n",
		    ptime->tm_mon + 1, ptime->tm_mday, ptime->tm_year % 100, peak_hour, peak_hour_login);
	    max_hour_login = peak_hour_login;
	}
	if (day_login > max_day_login) {
	    fprintf(fp1, "¡» ¡i%02d/%02d/%02d¡j      "
		    ANSI_COLOR(1;32) "³æ¤é¤W½u¤H¦¸" ANSI_RESET "­º¦¸¹F¨ì " ANSI_COLOR(1;33) "%d" ANSI_RESET " ¤H¦¸\n",
		    ptime->tm_mon + 1, ptime->tm_mday, ptime->tm_year % 100, day_login);
	    max_day_login = day_login;
	}
	if (SHM->number > max_reg + max_reg / 10) {
	    fprintf(fp1, "¡¹ ¡i%02d/%02d/%02d¡j      "
		    ANSI_COLOR(1;32) "Á`µù¥U¤H¼Æ" ANSI_RESET "´£¤É¨ì " ANSI_COLOR(1;31) "%d" ANSI_RESET " ¤H\n",
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
    const char const wday_str[] = "UMTWRFS";
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

	if (buf[0] != '#' && sscanf(buf, "%d %c%c", &mo, &a, &b) == 3 && isdigit(a)) {
	    if (isdigit(b)) {
		int da = 10 * (a - '0') + (b - '0');
		if (ptime->tm_mday == da && ptime->tm_mon + 1 == mo) {
		    i = 1;
		    fprintf(fp1, "%-14.14s", &buf[6]);
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
	fprintf(fp1, "¥»¤é¸`¤é¼x¨D¤¤");

out:
    fclose(fp1);

    return 0;
}

static int
update_holiday(struct tm *ptime)
{
    int i = 0, cnt = 0;
    char buf[128], *p;
    FILE *fp;

    if ((fp = fopen("etc/today_is", "r")) == NULL)
	return 1;

    for (i = 0; i < ptime->tm_hour; i++) {
	if (!fgets(buf, sizeof(buf), fp)) {
	    cnt = i;
	    i = i - 1 + (ptime->tm_hour - i) / cnt * cnt;
	    rewind(fp);
	}
    }

    fclose(fp);

    if ((p = strchr(buf, '\n')))
	*p = 0;

    memset(SHM->today_is, 0, sizeof(SHM->today_is));
    strlcpy(SHM->today_is, buf, sizeof(SHM->today_is));

    return 0;
}

int 
main(int argc, char **argv)
{
    int             i;
    int             mo, da;
    int             peak_hour_login, peak_hour, day_login;
    const char const log_file[] = "usies";
    char            buf[256], buf1[256];
    FILE           *fp;
    int             act[27];	/* ¦¸¼Æ/²Ö­p®É¶¡/pointer */
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

    printf("¦¸¼Æ/²Ö­p®É¶¡\n");
    parse_usies(log_file, &tm_adjusted, act);

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
    printf("¤W¯¸¤H¦¸²Î­p\n");
    output_today(&tm_adjusted, act, peak_hour_login, day_login);
    /* -------------------------------------------------------------- */

    printf("¾ú¥v¨Æ¥ó³B²z\n");
    /* Ptt ¾ú¥v¨Æ¥ó³B²z */
    update_history(&tm_adjusted, peak_hour, peak_hour_login, day_login);

    if (tm_now.tm_hour == 0) {
	system("/bin/cp etc/today etc/yesterday");
	/* system("rm -f note.dat"); */

	keeplog("etc/today", BN_RECORD, "¤W¯¸¤H¦¸²Î­p", NULL);
	keeplog(FN_MONEY, BN_SECURITY, "¥»¤éª÷¿ú©¹¨Ó°O¿ý", NULL);
	keeplog("etc/illegal_money", BN_SECURITY, "¥»¤é¹HªkÁÈ¿ú°O¿ý", NULL);
	keeplog("etc/osong.log", BN_SECURITY, "¥»¤éÂIºq°O¿ý", NULL);
	keeplog("etc/chicken", BN_RECORD, "Âû³õ³ø§i", NULL);

	// Re-output today's user stats, because keeplog removes it
	output_today(&tm_adjusted, act, peak_hour_login, day_login);

	snprintf(buf, sizeof(buf), "[¤½¦w³ø§i] ¨Ï¥ÎªÌ¤W½uºÊ±± [%02d/%02d:%02d]",
		tm_now.tm_mon + 1, tm_now.tm_mday, tm_now.tm_hour);
	keeplog("usies", "Security", buf, "usies");
	printf("[¤½¦w³ø§i] ¨Ï¥ÎªÌ¤W½uºÊ±±\n");

	sprintf(buf, "-%02d%02d%02d",
		tm_adjusted.tm_year % 100, tm_adjusted.tm_mon + 1, tm_adjusted.tm_mday);

	printf("À£ÁY¨Ï¥ÎªÌ¤W½uºÊ±±\n");
	gzip(log_file, "usies", buf);
	gzip("innd/bbslog", "innbbsd", buf);
	gzip("etc/mailog", "mailog", buf);

	/* Ptt ¸`¤é³B²z */
	printf("¸`¤é³B²z\n");
	update_holiday_list(&tm_now);

	/* Ptt Åwªïµe­±³B²z */
	printf("Åwªïµe­±³B²z\n");

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
	    keeplog("etc/week", BN_RECORD, "¥»¶g¼öªù¸ÜÃD", NULL);

	if (tm_now.tm_mday == 1)
	    keeplog("etc/month", BN_RECORD, "¥»¤ë¼öªù¸ÜÃD", NULL);

	if (tm_now.tm_yday == 1)
	    keeplog("etc/year", BN_RECORD, "¦~«×¼öªù¸ÜÃD", NULL);
    } else if (tm_now.tm_hour == 3 && tm_now.tm_wday == 6) {
	const char const fn1[] = "tmp";
	const char const fn2[] = "suicide";

	rename(fn1, fn2);
	mkdir(fn1, 0755);
	sprintf(buf, "tar cfz adm/%s-%02d%02d%02d.tgz %s",
		fn2, tm_now.tm_year % 100, tm_now.tm_mon + 1, tm_now.tm_mday, fn2);
	system(buf);
	sprintf(buf, "/bin/rm -fr %s", fn2);
	system(buf);
    }

    /* Ptt reset Ptt's share memory */
    printf("­«³]cache »Pfcache\n");

    update_holiday(&tm_now);

    SHM->Puptime = 0;
    resolve_fcache();
    reset_garbage();
    printf("°ÊºA¬ÝªO¸ê°T: last_usong=%d, last_film=%d\n", 
	    SHM->last_usong, SHM->last_film);

    printf("­pºâ¶i¯¸µe­±¼Æ: ");
    for (i = 0; i < 5; ++i) {
	sprintf(buf, "etc/Welcome_login.%d", i);
	if (access(buf, 0) < 0)
	    break;
    }
    printf("%d\n", SHM->GV2.e.nWelcomes = i);
    return 0;
}
