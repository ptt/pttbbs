/* $Id: account.c,v 1.8 2003/07/05 07:58:09 in2 Exp $ */
#include "bbs.h"

#define MAX_LINE        16
#define ADJUST_M        6	/* adjust back 5 minutes */

void
reset_garbage()
{
    if (SHM == NULL)
    {
	attach_SHM();
	if (SHM->Ptouchtime == 0)
	    SHM->Ptouchtime = 1;
    }

/* ¤£¾ã­Óreload?
   for(n=0;n<=SHM->max_film;n++)
   printf("\n**%d**\n %s \n",n,SHM->notes[n]);
 */
    SHM->Puptime = 0;
    reload_pttcache();

    printf("\n°ÊºA¬ÝªO¼Æ[%d]\n", SHM->max_film);
/*
  for(n=0; n<MAX_MOVIE_SECTION; n++)
  printf("sec%d=> °_ÂI:%d ¤U¦¸­n´«ªº:%d\n ",n,SHM->n_notes[n],
  SHM->next_refresh[n]);
  printf("\n");         
*/
}

void
keeplog(fpath, board, title)
    char *fpath;
    char *board;
    char *title;
{
    fileheader_t fhdr;
    int bid;
    char genbuf[256], buf[256];

    if (!board)
	board = "Record";


    sprintf(genbuf, "boards/%c/%s", board[0], board);
    stampfile(genbuf, &fhdr);
    sprintf(buf, "mv %s %s", fpath, genbuf);
    system(buf);
/*
  printf("keep record:[%s][%s][%s][%s]\n",fpath, board, title,genbuf);
*/
    strcpy(fhdr.title, title);
    strcpy(fhdr.owner, "[¾ú¥v¦Ñ®v]");
    sprintf(genbuf, "boards/%c/%s/.DIR", board[0], board);
    append_record(genbuf, &fhdr, sizeof(fhdr));
    if((bid = getbnum(board)) > 0)touchbtotal(bid);

}


static void
my_outs(fp, buf, mode)
    FILE *fp;
    char buf[], mode;
{
    static char state = '0';

    if (state != mode)
	fprintf(fp, "[3%cm", state = mode);
    if (buf[0])
    {
	fprintf(fp, buf);
	buf[0] = 0;
    }
}


void gzip(source, target, stamp)
    char *source, *target, *stamp;
{
    char buf[128];
    sprintf(buf, "gzip -f9n adm/%s%s", target, stamp);
    rename(source, &buf[14]);
    system(buf);
}

int main() {
    int hour, max, item, total, i, j, mo, da, max_user = 0, max_login = 0,
	max_reg = 0, mahour = 0, k;
    char *act_file = ".act";
    char *log_file = "usies";
    char buf[256], buf1[256], *p;
    FILE *fp, *fp1;
    int act[27];		/* ¦¸¼Æ/²Ö­p®É¶¡/pointer */
    time_t now;
    struct tm *ptime;

    nice(10);
    chdir(BBSHOME);
    now = time(NULL) - ADJUST_M * 60;	/* back to ancent */
    ptime = localtime(&now);

    memset(act, 0, sizeof(act));
    printf("¦¸¼Æ/²Ö­p®É¶¡\n");
    if ((ptime->tm_hour != 0) && (fp = fopen(act_file, "r")))
    {
	fread(act, sizeof(act), 1, fp);
	fclose(fp);
    }
    if ((fp = fopen(log_file, "r")) == NULL)
    {
	printf("cann't open usies\n");
	return 1;
    }
    if (act[26])
	fseek(fp, act[26], 0);
    while (fgets(buf, 256, fp))
    {
	buf[11+2]=0;
	hour = atoi(buf + 11);
	if (hour < 0 || hour > 23)
	{
	    continue;
	}
//"09/06/1999 17:44:58 Mon "
// 012345678901234567890123
	if (strstr(buf + 20, "ENTER"))
	{
	    act[hour]++;
	    continue;
	}
	if ((p = (char *) strstr(buf + 40, "Stay:")))
	{
	    if((hour = atoi(p + 5))) {
		act[24] += hour;
		act[25]++;
	    }
	    continue;
	}
    }
    act[26] = ftell(fp);
    fclose(fp);
    for (i = max = total = 0; i < 24; i++)
    {
	total += act[i];
	if (act[i] > max)
	{
	    max_user = max = act[i];
	    mahour = i;
	}
    }
    item = max / MAX_LINE + 1;

    if (!ptime->tm_hour)
    {
	keeplog("etc/today", "Record", "¤W¯¸¤H¦¸²Î­p");
	keeplog("etc/money", "Security", "¥»¤éª÷¿ú©¹¨Ó°O¿ý");
	keeplog("etc/illegal_money", "Security", "¥»¤é¹HªkÁÈ¿ú°O¿ý");
	keeplog("etc/chicken", "Record", "Âû³õ³ø§i");
    }

    printf("¤W¯¸¤H¦¸²Î­p\n");
    if ((fp = fopen("etc/today", "w")) == NULL)
    {
	printf("cann't open etc/today\n");
	return 1;
    }
    fprintf(fp, "\t\t\t[1;33;46m ¨C¤p®É¤W¯¸¤H¦¸²Î­p [%02d/%02d/%02d] [40m\n\n", ptime->tm_year % 100, ptime->tm_mon + 1, ptime->tm_mday);
    for (i = MAX_LINE + 1; i > 0; i--)
    {
	strcpy(buf, "   ");
	for (j = 0; j < 24; j++)
	{
	    max = item * i;
	    hour = act[j];
	    if (hour && (max > hour) && (max - item <= hour))
	    {
		my_outs(fp, buf, '3');
		fprintf(fp, "%-3d", hour / 10);
	    }
	    else if (max <= hour)
	    {
		my_outs(fp, buf, '4');
		fprintf(fp, "¢i ");
	    }
	    else
		strcat(buf, "   ");
	}
	fprintf(fp, "\n");
    }
    fprintf(fp, "   [32m"
	    "0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23\n\n"
	    "\t      [34m³æ¦ì: [37m10[34m ¤H");
    fprintf(fp, "  Á`¦@¤W¯¸¤H¦¸¡G[37m%-7d[34m¥­§¡¨Ï¥Î¤H¼Æ¡G[37m%d\n", total, total / 24);
    fclose(fp);

    if((fp = fopen(act_file, "w"))) {
	fwrite(act, sizeof(act), 1, fp);
	fclose(fp);
    }

/* -------------------------------------------------------------- */

    sprintf(buf, "-%02d%02d%02d",
	    ptime->tm_year % 100, ptime->tm_mon + 1, ptime->tm_mday);

    now += ADJUST_M * 60;	/* back to future */


    printf("¾ú¥v¨Æ¥ó³B²z\n");
/* Ptt ¾ú¥v¨Æ¥ó³B²z */
    if((fp = fopen("etc/history.data", "r"))) { /*³Ì¦h¦P®É¤W½u */
	if (fscanf(fp, "%d %d %d %d", &max_login, &max, &max_reg, &k))
	{
	    int a;
	    resolve_fcache();
	    printf("¦¹®É¬q³Ì¦h¦P®É¤W½u:%d ¹L¥h:%d\n", a = SHM->max_user, k);
	    fclose(fp);
	    if (a > k)
	    {
		ptime = localtime(&SHM->max_time);
		if((fp1 = fopen("etc/history", "a")))
		{
		    fprintf(fp1,
			    "¡· ¡i%02d/%02d/%02d %02d:%02d¡j"
			    "[32m¦P®É¦b§{¤º¤H¼Æ[m­º¦¸¹F¨ì [1;36m%d[m ¤H¦¸\n",
			    ptime->tm_mon + 1, ptime->tm_mday, ptime->tm_year % 100,
			    ptime->tm_hour, ptime->tm_min, a);
		    fclose(fp1);
		}
		if((fp = fopen("etc/history.data", "w")))
		{
		    fprintf(fp, "%d %d %d %d", max_login, max, max_reg, a);
		    fclose(fp);
		}
	    }
	}
	else
	    fclose(fp);
    }
    ptime = localtime(&now);

    if (ptime->tm_hour)
    {
	/* rotate one line in today_is */
	puts("¦h­Ó¸`¤é³B²z");
	if((fp1 = fopen("etc/today_is", "r"))) {
	    char tod[100][20];
	    
	    i = 0;
	    while(i < 100 && fgets(tod[i], sizeof(tod[0]), fp1))
		i++;
	    fclose(fp1);
	    
	    fp1 = fopen("etc/today_is", "w");
	    for(j = 0; j < i; j++)
		fputs(tod[j + 1 < i ? j + 1 : 0], fp1);
	    fclose(fp1);
	}
    }


    if (!ptime->tm_hour)
    {
	keeplog(".note", "Record", "¤ß±¡¯d¨¥ª©");
	system("/bin/cp etc/today etc/yesterday");
/*    system("rm -f note.dat"); */
/* Ptt */
	sprintf(buf1, "[¤½¦w³ø§i] ¨Ï¥ÎªÌ¤W½uºÊ±± [%02d/%02d:%02d]"
		,ptime->tm_mon + 1, ptime->tm_mday, ptime->tm_hour);
	keeplog("usies", "Security", buf1);
	printf("[¤½¦w³ø§i] ¨Ï¥ÎªÌ¤W½uºÊ±±\n");
	gzip(log_file, "usies", buf);
	printf("À£ÁY¨Ï¥ÎªÌ¤W½uºÊ±±\n");
/* Ptt ¾ú¥v¨Æ¥ó³B²z */
	now = time(NULL) - ADJUST_M * 60;	/* back to ancent */
	ptime = localtime(&now);

	attach_SHM();
	if((fp = fopen("etc/history.data", "r")))
	{			/* ³æ¤é³Ì¦h¦¸¤H¦¸,¦P®É¤W½u,µù¥U */
	    if (fscanf(fp, "%d %d %d %d", &max_login, &max, &max_reg, &k))
	    {
		fp1 = fopen("etc/history", "r+");
		fseek(fp1, 0, 2);
		if (max_user > max)
		{
		    fprintf(fp1, "¡º ¡i%02d/%02d/%02d %02d¡j   "
			    "[1;32m³æ¤@¤p®É¤W½u¤H¦¸[m­º¦¸¹F¨ì [1;35m%d[m ¤H¦¸ \n"
			    ,ptime->tm_mon + 1, ptime->tm_mday, ptime->tm_year % 100, mahour, max_user);
		    max = max_user;
		}
		if (total > max_login)
		{
		    fprintf(fp1, "¡» ¡i%02d/%02d/%02d¡j      "
			    "[1;32m³æ¤é¤W½u¤H¦¸[m­º¦¸¹F¨ì[1;33m %d[m ¤H¦¸   \n"
			    ,ptime->tm_mon + 1, ptime->tm_mday, ptime->tm_year % 100, total);
		    max_login = total;
		}

		if (SHM->number > max_reg + max_reg / 10)
		{
		    fprintf(fp1, "¡¹ ¡i%02d/%02d/%02d¡j      "
			    "[1;32mÁ`µù¥U¤H¼Æ[m´£¤É¨ì[1;31m %d[m ¤H   \n"
			    ,ptime->tm_mon + 1, ptime->tm_mday, ptime->tm_year % 100, SHM->number);
		    max_reg = SHM->number;
		}

		fclose(fp1);
	    }
	    fclose(fp);
	    fp = fopen("etc/history.data", "w");
	    fprintf(fp, "%d %d %d %d", max_login, max, max_reg, k);
	    fclose(fp);
	}
	now += ADJUST_M * 60;	/* back to future */
	ptime = localtime(&now);

	/* Ptt ¸`¤é³B²z */
	printf("¸`¤é³B²z\n");
	if((fp1 = fopen("etc/today_is", "w"))) {
	    i = 0;
	    if((fp = fopen("etc/feast", "r"))) {
		while(fgets(buf1, sizeof(buf1), fp)) {
		    if(buf[0] != '#' &&
		       sscanf(buf1, "%d %d ", &mo, &da) == 2) {
			if(ptime->tm_mday == da && ptime->tm_mon + 1 == mo) {
			    i = 1;
			    fprintf(fp1, "%-14.14s", &buf1[6]);
			}
		    }
		}
		fclose(fp);
	    }
	    printf("¸`¤é³B²z1\n");
	    if(i == 0) {
		if((fp = fopen("etc/today_boring", "r"))) {
		    while(fgets(buf1, sizeof(buf1), fp))
			if(strlen(buf) > 3)
			    fprintf(fp1, "%s", buf1);
		    fclose(fp);
		} else
		    fprintf(fp1, "¥»¤é¸`¤é¼x¨D¤¤");
	    }
	    fclose(fp1);
	}
	
	/* Ptt Åwªïµe­±³B²z */
	printf("Åwªïµe­±³B²z\n");

	if((fp = fopen("etc/Welcome.date", "r")))
	{
	    char temp[50];
	    while (fscanf(fp, "%d %d %s\n", &mo, &da, buf1) != EOF)
	    {
		if (ptime->tm_mday == da && ptime->tm_mon + 1 == mo)
		{
		    strcpy(temp, buf1);
		    sprintf(buf1, "cp -f etc/Welcomes/%s etc/Welcome", temp);
		    system(buf1);
		    break;
		}
	    }
	    fclose(fp);
	}
	printf("Åwªïµe­±³B²z\n");
	if (ptime->tm_wday == 0)
	{
	    keeplog("etc/week", "Record", "¥»¶g¼öªù¸ÜÃD");

	    gzip("bbslog", "bntplink", buf);
	    gzip("innd/bbslog", "innbbsd", buf);
	    gzip("etc/mailog", "mailog", buf);
	}

	if (ptime->tm_mday == 1)
	    keeplog("etc/month", "Record", "¥»¤ë¼öªù¸ÜÃD");

	if (ptime->tm_yday == 1)
	    keeplog("etc/year", "Record", "¦~«×¼öªù¸ÜÃD");
    }
    else if (ptime->tm_hour == 3 && ptime->tm_wday == 6)
    {
	char *fn1 = "tmp";
	char *fn2 = "suicide";
	rename(fn1, fn2);
	mkdir(fn1, 0755);
	sprintf(buf, "tar cfz adm/%s-%02d%02d%02d.tgz %s",
		fn2, ptime->tm_year % 100, ptime->tm_mon + 1, ptime->tm_mday, fn2);
	system(buf);
	sprintf(buf, "/bin/rm -fr %s", fn2);
	system(buf);
    }
/* Ptt reset Ptt's share memory */
    printf("­«³]Pttcache »Pfcache\n");

    SHM->Puptime = 0;
    resolve_fcache();
    reset_garbage();

    printf("­pºâ¶i¯¸µe­±¼Æ: ");
    for( i = 0 ; i < 5 ; ++i ){
	sprintf(buf, "etc/Welcome_login.%d", i);
	if( access(buf, 0) < 0 )
	    break;
    }
    printf("%d\n", SHM->GV2.e.nWelcomes = i);
    return 0;
}
