/* $Id$ */
#include "bbs.h"

#define MAX_VOTE_PAGE	5

static char     *STR_bv_control = "control";	/* щ布ら戳 匡兜 */
static char     *STR_bv_desc = "desc";	/* щ布ヘ */
static char     *STR_bv_ballots = "ballots";
static char     *STR_bv_flags = "flags";
static char     *STR_bv_comments = "comments";	/* щ布某 */
static char     *STR_bv_limited = "limited";	/* ╬щ布 */
static char     *STR_bv_title = "vtitle";

static char     STR_bv_results[] = "results";

static char     STR_new_control[] = "control0\0";	/* щ布ら戳 匡兜 */
static char     STR_new_desc[] = "desc0\0";	/* щ布ヘ */
static char     STR_new_ballots[] = "ballots0\0";
static char     STR_new_flags[] = "flags0\0";
static char     STR_new_comments[] = "comments0\0";	/* щ布某 */
static char     STR_new_limited[] = "limited0\0";	/* ╬щ布 */
static char     STR_new_title[] = "vtitle0\0";

#if 1 // backward compatible

static void
convert_to_newversion(FILE *fp, char *file, char *ballots)
{
    char buf[256], buf2[256];
    short blah;
    int count = -1, tmp, fd, fdw;
    FILE *fpw;

    assert(fp);
    flock(fileno(fp), LOCK_EX);
    rewind(fp);
    fgets(buf, sizeof(buf), fp);
    if (index(buf, ',')) {
	rewind(fp);
	flock(fileno(fp), LOCK_UN);
	return;
    }
    sscanf(buf, " %d", &tmp);

    if ((fd = open(ballots, O_RDONLY)) != -1) {
	sprintf(buf, "%s.new", ballots);
	fdw = open(buf, O_WRONLY | O_CREAT, 0600);
	flock(fd, LOCK_EX);     /* Thor: ňゎ衡 */
	while (read(fd, &buf2[0], 1) == 1) {
	    blah = buf2[0];
	    if (blah >= 'A')
		blah -= 'A';
	    write(fdw, &blah, sizeof(short));
	}
	flock(fd, LOCK_UN);
	close(fd);
	close(fdw);
	Rename(buf, ballots);
    }

    sprintf(buf2, "%s.new", file);
    if (!(fpw = fopen(buf2, "w"))) {
	rewind(fp);
	flock(fileno(fp), LOCK_UN);
	return;
    }
    fprintf(fpw, "000,000\n");
    while (fgets(buf, sizeof(buf), fp)) {
	fprintf(fpw, "%s", buf);
	count++;
    }
    rewind(fpw);
    fprintf(fpw, "%3d,%3d", count, tmp);
    fclose(fpw);
    flock(fileno(fp), LOCK_UN);
    fclose(fp);
    unlink(file);
    Rename(buf2, file);
    fp = fopen(file, "r");
}
#endif

void
b_suckinfile(FILE * fp, char *fname)
{
    FILE           *sfp;

    if ((sfp = fopen(fname, "r"))) {
	char            inbuf[256];

	while (fgets(inbuf, sizeof(inbuf), sfp))
	    fputs(inbuf, fp);
	fclose(sfp);
    }
}

static void
b_count(char *buf, int counts[], short item_num, int *total)
{
    short	    choice;
    int             fd;

    memset(counts, 0, item_num * sizeof(counts[0]));
    *total = 0;
    if ((fd = open(buf, O_RDONLY)) != -1) {
	flock(fd, LOCK_EX);	/* Thor: ňゎ衡 */
	while (read(fd, &choice, sizeof(short)) == sizeof(short)) {
	    if (choice >= item_num)
		continue;
	    counts[choice]++;
	    (*total)++;
	}
	flock(fd, LOCK_UN);
	close(fd);
    }
}


static int
b_nonzeroNum(char *buf)
{
    int             i = 0;
    char            inchar;
    int             fd;

    if ((fd = open(buf, O_RDONLY)) != -1) {
	while (read(fd, &inchar, 1) == 1)
	    if (inchar)
		i++;
	close(fd);
    }
    return i;
}

static void
vote_report(char *bname, char *fname, char *fpath)
{
    register char  *ip;
    time_t          dtime;
    int             fd, bid;
    fileheader_t    header;

    ip = fpath;
    while (*(++ip));
    *ip++ = '/';

    /* get a filename by timestamp */

    dtime = now;
    for (;;) {
	sprintf(ip, "M.%ld.A", ++dtime);
	fd = open(fpath, O_CREAT | O_EXCL | O_WRONLY, 0644);
	if (fd >= 0)
	    break;
	dtime++;
    }
    close(fd);

    unlink(fpath);
    link(fname, fpath);

    /* append record to .DIR */

    memset(&header, 0, sizeof(fileheader_t));
    strlcpy(header.owner, "[皑隔贝]", sizeof(header.owner));
    snprintf(header.title, sizeof(header.title), "[%s] 狾 匡薄厨旧", bname);
    {
	register struct tm *ptime = localtime(&dtime);

	snprintf(header.date, sizeof(header.date),
		 "%2d/%02d", ptime->tm_mon + 1, ptime->tm_mday);
    }
    strlcpy(header.filename, ip, sizeof(header.filename));

    strcpy(ip, ".DIR");
    if ((fd = open(fpath, O_WRONLY | O_CREAT, 0644)) >= 0) {
	flock(fd, LOCK_EX);
	lseek(fd, 0, SEEK_END);
	write(fd, &header, sizeof(fileheader_t));
	flock(fd, LOCK_UN);
	close(fd);
	if ((bid = getbnum(bname)) > 0)
	    setbtotal(bid);

    }
}

static void
b_result_one(boardheader_t * fh, int ind, int *total)
{
    FILE           *cfp, *tfp, *frp, *xfp;
    char           *bname;
    char            buf[STRLEN];
    char            inbuf[80];
    int            *counts;
    int             people_num;
    short	    item_num, junk;
    char            b_control[64];
    char            b_newresults[64];
    char            b_report[64];
    time_t          closetime;

    fh->bvote--;

    if (fh->bvote == 0)
	fh->bvote = 2;
    else if (fh->bvote == 2)
	fh->bvote = 1;

    if (ind) {
	snprintf(STR_new_ballots, sizeof(STR_new_ballots), "%s%d", STR_bv_ballots, ind);
	snprintf(STR_new_control, sizeof(STR_new_control),"%s%d", STR_bv_control, ind);
	snprintf(STR_new_desc, sizeof(STR_new_desc), "%s%d", STR_bv_desc, ind);
	snprintf(STR_new_flags, sizeof(STR_new_flags), "%s%d", STR_bv_flags, ind);
	snprintf(STR_new_comments, sizeof(STR_new_comments), "%s%d", STR_bv_comments, ind);
	snprintf(STR_new_limited, sizeof(STR_new_limited), "%s%d", STR_bv_limited, ind);
	snprintf(STR_new_title, sizeof(STR_new_title), "%s%d", STR_bv_title, ind);
    } else {
	strlcpy(STR_new_ballots, STR_bv_ballots, sizeof(STR_new_ballots));
	strlcpy(STR_new_control, STR_bv_control, sizeof(STR_new_control));
	strlcpy(STR_new_desc, STR_bv_desc, sizeof(STR_new_desc));
	strlcpy(STR_new_flags, STR_bv_flags, sizeof(STR_new_flags));
	strlcpy(STR_new_comments, STR_bv_comments, sizeof(STR_new_comments));
	strlcpy(STR_new_limited, STR_bv_limited, sizeof(STR_new_limited));
	strlcpy(STR_new_title, STR_bv_title, sizeof(STR_new_title));
    }

    bname = fh->brdname;

    setbfile(buf, bname, STR_new_control);
    cfp = fopen(buf, "r");
#if 1 // backward compatible
    setbfile(b_control, bname, STR_new_ballots);
    convert_to_newversion(cfp, buf, b_control);
#endif
    fscanf(cfp, "%hd,%hd\n%lu\n", &item_num, &junk, &closetime);
    fclose(cfp);

    counts = (int *)malloc(item_num * sizeof(int));

    setbfile(b_control, bname, "tmp");
    if (rename(buf, b_control) == -1)
	return;
    setbfile(buf, bname, STR_new_flags);
    people_num = b_nonzeroNum(buf);
    unlink(buf);
    setbfile(buf, bname, STR_new_ballots);
#if 0 // backward compatible
    if (!newversion)
	b_count_old(buf, counts, total);
    else
#endif
    b_count(buf, counts, item_num, total);
    unlink(buf);

    setbfile(b_newresults, bname, "newresults");
    if ((tfp = fopen(b_newresults, "w")) == NULL)
	return;

    setbfile(buf, bname, STR_new_title);

    if ((xfp = fopen(buf, "r"))) {
	fgets(inbuf, sizeof(inbuf), xfp);
	fprintf(tfp, "%s\n』 щ布嘿: %s\n\n", msg_seperator, inbuf);
	fclose(xfp);
    }
    fprintf(tfp, "%s\n』 щ布いゎ: %s\n\n』 布匡肈ヘ磞瓃:\n\n",
	    msg_seperator, ctime(&closetime));
    fh->vtime = now;

    setbfile(buf, bname, STR_new_desc);

    b_suckinfile(tfp, buf);
    unlink(buf);

    if ((cfp = fopen(b_control, "r"))) {
	fgets(inbuf, sizeof(inbuf), cfp);
	fgets(inbuf, sizeof(inbuf), cfp);
	fprintf(tfp, "\n』щ布挡狦:(Τ %d щ布,–程щ %hd 布)\n",
		people_num, junk);
	fprintf(tfp, "    匡    兜                                   羆布计 眔布瞯   眔布だガ\n");
	for (junk = 0; junk < item_num; junk++) {
	    fgets(inbuf, sizeof(inbuf), cfp);
	    inbuf[(strlen(inbuf) - 1)] = '\0';
	    fprintf(tfp, "    %-42s %3d 布   %02.2f%%   %02.2f%%\n", inbuf + 3, counts[junk],                
		    (float)(counts[junk] * 100) / (float)(people_num),
		    (float)(counts[junk] * 100) / (float)(*total));
	}
	fclose(cfp);
    }
    unlink(b_control);
    free(counts);

    fprintf(tfp, "%s\n』 ㄏノ某\n\n", msg_seperator);
    setbfile(buf, bname, STR_new_comments);
    b_suckinfile(tfp, buf);
    unlink(buf);

    fprintf(tfp, "%s\n』 羆布计 = %d 布\n\n", msg_seperator, *total);
    fclose(tfp);

    setbfile(b_report, bname, "report");
    if ((frp = fopen(b_report, "w"))) {
	b_suckinfile(frp, b_newresults);
	fclose(frp);
    }
    snprintf(inbuf, sizeof(inbuf), "boards/%c/%s", bname[0], bname);
    vote_report(bname, b_report, inbuf);
    if (!(fh->brdattr & BRD_NOCOUNT)) {
	snprintf(inbuf, sizeof(inbuf), "boards/%c/%s", 'R', "Record");
	vote_report(bname, b_report, inbuf);
    }
    unlink(b_report);

    tfp = fopen(b_newresults, "a");
    setbfile(buf, bname, STR_bv_results);
    b_suckinfile(tfp, buf);
    fclose(tfp);
    Rename(b_newresults, buf);
}

static void
b_result(boardheader_t * fh)
{
    FILE           *cfp;
    time_t          closetime;
    int             i, total;
    char            buf[STRLEN];
    char            temp[STRLEN];

    for (i = 0; i < 20; i++) {
	if (i)
	    snprintf(STR_new_control, sizeof(STR_new_control), "%s%d", STR_bv_control, i);
	else
	    strlcpy(STR_new_control, STR_bv_control, sizeof(STR_new_control));

	setbfile(buf, fh->brdname, STR_new_control);
	cfp = fopen(buf, "r");
	if (!cfp)
	    continue;
	fgets(temp, sizeof(temp), cfp);
	fscanf(cfp, "%lu\n", &closetime);
	fclose(cfp);
	if (closetime < now)
	    b_result_one(fh, i, &total);
    }
}

static int
b_close(boardheader_t * fh)
{

    if (fh->bvote == 2) {
	if (fh->vtime < now - 3 * 86400) {
	    fh->bvote = 0;
	    return 1;
	} else
	    return 0;
    }
    b_result(fh);
    return 1;
}

int
b_closepolls()
{
    char    *fn_vote_polling = ".polling";
    boardheader_t  *fhp;
    FILE           *cfp;
    int             pos, dirty;
    time_t          last;
    char            timebuf[100];

    /* Edited by CharlieL for can't auto poll bug */

    if ((cfp = fopen(fn_vote_polling, "r"))) {
	fgets(timebuf, 100 * sizeof(char), cfp);
	sscanf(timebuf, "%lu", &last);
	fclose(cfp);
	if (last + 3600 >= now)
	    return 0;
    }
    if ((cfp = fopen(fn_vote_polling, "w")) == NULL)
	return 0;
    fprintf(cfp, "%lu\n%s\n", now, ctime(&now));
    fclose(cfp);

    dirty = 0;
    for (fhp = bcache, pos = 1; pos <= numboards; fhp++, pos++) {
	if (fhp->bvote && b_close(fhp)) {
	    if (substitute_record(fn_board, fhp, sizeof(*fhp), pos) == -1)
		outs(err_board_update);
	    dirty = 1;
	}
    }
    if (dirty)			/* vote flag changed */
	reset_board(pos);

    return 0;
}

static int
vote_view(char *bname, int vote_index)
{
    boardheader_t  *fhp;
    FILE           *fp;
    char            buf[STRLEN], genbuf[STRLEN], inbuf[STRLEN];
    short	    item_num, i;
    int             num = 0, pos, *counts, total;
    time_t          closetime;

    if (vote_index) {
	snprintf(STR_new_ballots, sizeof(STR_new_ballots),"%s%d", STR_bv_ballots, vote_index);
	snprintf(STR_new_control, sizeof(STR_new_control), "%s%d", STR_bv_control, vote_index);
	snprintf(STR_new_desc, sizeof(STR_new_desc), "%s%d", STR_bv_desc, vote_index);
	snprintf(STR_new_flags, sizeof(STR_new_flags), "%s%d", STR_bv_flags, vote_index);
	snprintf(STR_new_comments, sizeof(STR_new_comments), "%s%d", STR_bv_comments, vote_index);
	snprintf(STR_new_limited, sizeof(STR_new_limited), "%s%d", STR_bv_limited, vote_index);
	snprintf(STR_new_title, sizeof(STR_new_title), "%s%d", STR_bv_title, vote_index);
    } else {
	strlcpy(STR_new_ballots, STR_bv_ballots, sizeof(STR_new_ballots));
	strlcpy(STR_new_control, STR_bv_control, sizeof(STR_new_control));
	strlcpy(STR_new_desc, STR_bv_desc, sizeof(STR_new_desc));
	strlcpy(STR_new_flags, STR_bv_flags, sizeof(STR_new_flags));
	strlcpy(STR_new_comments, STR_bv_comments, sizeof(STR_new_comments));
	strlcpy(STR_new_limited, STR_bv_limited, sizeof(STR_new_limited));
	strlcpy(STR_new_title, STR_bv_title, sizeof(STR_new_title));
    }

    setbfile(buf, bname, STR_new_ballots);
    if ((num = dashs(buf)) < 0) /* file size */
	num = 0;

    setbfile(buf, bname, STR_new_title);
    move(0, 0);
    clrtobot();

    if ((fp = fopen(buf, "r"))) {
	fgets(inbuf, sizeof(inbuf), fp);
	prints("\nщ布嘿: %s", inbuf);
	fclose(fp);
    }
    setbfile(buf, bname, STR_new_control);
    fp = fopen(buf, "r");
#if 1 // backward compatible
    setbfile(genbuf, bname, STR_new_ballots);
    convert_to_newversion(fp, buf, genbuf);
#endif
    fscanf(fp, "%hd,%hd\n%lu\n", &item_num, &i, &closetime);
    counts = (int *)malloc(item_num * sizeof(int));

    prints("\n』 箇щ布ㄆ: –程щ %d 布,ヘ玡Τ %d 布,\n"
	   "セΩщ布盢挡 %s", atoi(inbuf), (num / sizeof(short)),
	   ctime(&closetime));

    /* Thor: 秨 布计 箇 */
    setbfile(buf, bname, STR_new_flags);
    prints("Τ %d щ布\n", b_nonzeroNum(buf));

    setbfile(buf, bname, STR_new_ballots);
#if 0 // backward compatible
    if (!newversion)
	b_count_old(buf, counts, &total);
    else
#endif
    b_count(buf, counts, item_num, &total);

    total = 0;

    for (i = num = 0; i < item_num; i++, num++) {
	fgets(inbuf, sizeof(inbuf), fp);
	inbuf[(strlen(inbuf) - 1)] = '\0';
	inbuf[30] = '\0';	/* truncate */
	move(num % 15 + 6, num / 15 * 40);
	prints("  %-32s%3d 布", inbuf, counts[i]);
	total += counts[i];
	if (num == 29) {
	    num = -1;
	    pressanykey();
	    move(6, 0);
	    clrtobot();
	}
    }
    fclose(fp);
    free(counts);
    pos = getbnum(bname);
    fhp = bcache + pos - 1;
    move(t_lines - 3, 0);
    prints("』 ヘ玡羆布计 = %d 布", total);
    getdata(b_lines - 1, 0, "(A)щ布 (B)矗Ν秨布 (C)膥尿[C] ", genbuf,
	    4, LCECHO);
    if (genbuf[0] == 'a') {
	setbfile(buf, bname, STR_new_control);
	unlink(buf);
	setbfile(buf, bname, STR_new_flags);
	unlink(buf);
	setbfile(buf, bname, STR_new_ballots);
	unlink(buf);
	setbfile(buf, bname, STR_new_desc);
	unlink(buf);
	setbfile(buf, bname, STR_new_limited);
	unlink(buf);
	setbfile(buf, bname, STR_new_title);
	unlink(buf);

	if (fhp->bvote)
	    fhp->bvote--;
	if (fhp->bvote == 2)
	    fhp->bvote = 1;

	if (substitute_record(fn_board, fhp, sizeof(*fhp), pos) == -1)
	    outs(err_board_update);
	reset_board(pos);
    } else if (genbuf[0] == 'b') {
	b_result_one(fhp, vote_index, &total);
	if (substitute_record(fn_board, fhp, sizeof(*fhp), pos) == -1)
	    outs(err_board_update);

	reset_board(pos);
    }
    return FULLUPDATE;
}

static int
vote_view_all(char *bname)
{
    int             i;
    int             x = -1;
    FILE           *fp, *xfp;
    char            buf[STRLEN], genbuf[STRLEN];
    char            inbuf[80];

    strlcpy(STR_new_control, STR_bv_control, sizeof(STR_new_control));
    strlcpy(STR_new_title, STR_bv_title, sizeof(STR_new_title));
    setbfile(buf, bname, STR_new_control);
    move(0, 0);
    if ((fp = fopen(buf, "r"))) {
	outs("(0) ");
	x = 0;
	fclose(fp);

	setbfile(buf, bname, STR_new_title);
	if ((xfp = fopen(buf, "r"))) {
	    fgets(inbuf, sizeof(inbuf), xfp);
	    fclose(xfp);
	} else
	    strlcpy(inbuf, "礚夹肈", sizeof(inbuf));
	prints("%s\n", inbuf);
    }
    for (i = 1; i < 20; i++) {
	snprintf(STR_new_control, sizeof(STR_new_control),
		 "%s%d", STR_bv_control, i);
	snprintf(STR_new_title, sizeof(STR_new_title),
		 "%s%d", STR_bv_title, i);
	setbfile(buf, bname, STR_new_control);
	if ((fp = fopen(buf, "r"))) {
	    prints("(%d) ", i);
	    x = i;
	    fclose(fp);

	    setbfile(buf, bname, STR_new_title);
	    if ((xfp = fopen(buf, "r"))) {
		fgets(inbuf, sizeof(inbuf), xfp);
		fclose(xfp);
	    } else
		strlcpy(inbuf, "礚夹肈", sizeof(inbuf));
	    prints("%s\n", inbuf);
	}
    }

    if (x < 0)
	return FULLUPDATE;
    snprintf(buf, sizeof(buf), "璶碭腹щ布 [%d] ", x);

    getdata(b_lines - 1, 0, buf, genbuf, 4, LCECHO);


    if (atoi(genbuf) < 0 || atoi(genbuf) > 20)
	snprintf(genbuf, sizeof(genbuf), "%d", x);
    if (genbuf[0] != '0')
	snprintf(STR_new_control, sizeof(STR_new_control),
		 "%s%d", STR_bv_control, atoi(genbuf));
    else
	strlcpy(STR_new_control, STR_bv_control, sizeof(STR_new_control));

    setbfile(buf, bname, STR_new_control);

    if (dashf(buf)) {
	return vote_view(bname, atoi(genbuf));
    } else
	return FULLUPDATE;
}

static int
vote_maintain(char *bname)
{
    FILE           *fp = NULL;
    char            inbuf[STRLEN], buf[STRLEN];
    int             num = 0, aborted, pos, x, i;
    time_t          closetime;
    boardheader_t  *fhp;
    char            genbuf[4];

    if (!(currmode & MODE_BOARD))
	return 0;
    if ((pos = getbnum(bname)) <= 0)
	return 0;

    stand_title("羭快щ布");
    fhp = bcache + pos - 1;

    /* CharlieL */
    if (fhp->bvote != 2 && fhp->bvote != 0) {
	getdata(b_lines - 1, 0,
		"(V)芠诡ヘ玡щ布 (M)羭快穝щ布 (A)┮Τщ布 (Q)膥尿 [Q]",
		genbuf, 4, LCECHO);
	if (genbuf[0] == 'v')
	    return vote_view_all(bname);
	else if (genbuf[0] == 'a') {
	    fhp->bvote = 0;

	    setbfile(buf, bname, STR_bv_control);
	    unlink(buf);
	    setbfile(buf, bname, STR_bv_flags);
	    unlink(buf);
	    setbfile(buf, bname, STR_bv_ballots);
	    unlink(buf);
	    setbfile(buf, bname, STR_bv_desc);
	    unlink(buf);
	    setbfile(buf, bname, STR_bv_limited);
	    unlink(buf);
	    setbfile(buf, bname, STR_bv_title);
	    unlink(buf);

	    for (i = 1; i < 20; i++) {
		snprintf(STR_new_ballots, sizeof(STR_new_ballots),
			 "%s%d", STR_bv_ballots, i);
		snprintf(STR_new_control, sizeof(STR_new_control),
			 "%s%d", STR_bv_control, i);
		snprintf(STR_new_desc, sizeof(STR_new_desc),
			 "%s%d", STR_bv_desc, i);
		snprintf(STR_new_flags, sizeof(STR_new_flags),
			 "%s%d", STR_bv_flags, i);
		snprintf(STR_new_comments, sizeof(STR_new_comments),
			 "%s%d", STR_bv_comments, i);
		snprintf(STR_new_limited, sizeof(STR_new_limited),
			 "%s%d", STR_bv_limited, i);
		snprintf(STR_new_title, sizeof(STR_new_title),
			 "%s%d", STR_bv_title, i);

		setbfile(buf, bname, STR_new_control);
		unlink(buf);
		setbfile(buf, bname, STR_new_flags);
		unlink(buf);
		setbfile(buf, bname, STR_new_ballots);
		unlink(buf);
		setbfile(buf, bname, STR_new_desc);
		unlink(buf);
		setbfile(buf, bname, STR_new_limited);
		unlink(buf);
		setbfile(buf, bname, STR_new_title);
		unlink(buf);
	    }
	    if (substitute_record(fn_board, fhp, sizeof(*fhp), pos) == -1)
		outs(err_board_update);

	    return FULLUPDATE;
	} else if (genbuf[0] != 'm' || fhp->bvote >= 20)
	    return FULLUPDATE;
    }
    strlcpy(STR_new_control, STR_bv_control, sizeof(STR_new_control));
    setbfile(buf, bname, STR_new_control);
    x = 0;
    while (x < 20 && (fp = fopen(buf, "r")) != NULL) { // TODO try access()
	fclose(fp);
	x++;
	snprintf(STR_new_control, sizeof(STR_new_control),
		 "%s%d", STR_bv_control, x);
	setbfile(buf, bname, STR_new_control);
    }
    if (x >= 20)
	return FULLUPDATE;
    if (x) {
	snprintf(STR_new_ballots, sizeof(STR_new_ballots), "%s%d", STR_bv_ballots, x);
	snprintf(STR_new_control, sizeof(STR_new_control), "%s%d", STR_bv_control, x);
	snprintf(STR_new_desc, sizeof(STR_new_desc), "%s%d", STR_bv_desc, x);
	snprintf(STR_new_flags, sizeof(STR_new_flags), "%s%d", STR_bv_flags, x);
	snprintf(STR_new_comments, sizeof(STR_new_comments), "%s%d", STR_bv_comments, x);
	snprintf(STR_new_limited, sizeof(STR_new_limited), "%s%d", STR_bv_limited, x);
	snprintf(STR_new_title, sizeof(STR_new_title), "%s%d", STR_bv_title, x);
    } else {
	strlcpy(STR_new_ballots, STR_bv_ballots, sizeof(STR_new_ballots));
	strlcpy(STR_new_control, STR_bv_control, sizeof(STR_new_control));
	strlcpy(STR_new_desc, STR_bv_desc, sizeof(STR_new_desc));
	strlcpy(STR_new_flags, STR_bv_flags, sizeof(STR_new_flags));
	strlcpy(STR_new_comments, STR_bv_comments, sizeof(STR_new_comments));
	strlcpy(STR_new_limited, STR_bv_limited, sizeof(STR_new_limited));
	strlcpy(STR_new_title, STR_bv_title, sizeof(STR_new_title));
    }
    clear();
    move(0, 0);
    prints("材 %d 腹щ布\n", x);
    setbfile(buf, bname, STR_new_title);
    getdata(4, 0, "叫块щ布嘿:", inbuf, 50, LCECHO);
    if (inbuf[0] == '\0')
	strlcpy(inbuf, "ぃ", sizeof(inbuf));
    fp = fopen(buf, "w");
    assert(fp);
    fprintf(fp, "%s", inbuf);
    fclose(fp);

    prints("ヴ龄秨﹍絪胯Ω [щ布﹙Ξ]");
    pressanykey();
    setbfile(buf, bname, STR_new_desc);
    aborted = vedit(buf, NA, NULL);
    if (aborted == -1) {
	vmsg("Ωщ布");
	return FULLUPDATE;
    }
    aborted = 0;
    setbfile(buf, bname, STR_new_flags);
    unlink(buf);

    getdata(4, 0,
	    "琌﹚щ布虫(y)絪胯щ布虫[n]ヴщ布:[N]",
	    inbuf, 2, LCECHO);
    setbfile(buf, bname, STR_new_limited);
    if (inbuf[0] == 'y') {
	fp = fopen(buf, "w");
	assert(fp);
	//fprintf(fp, "Ωщ布砞");
	fclose(fp);
	friend_edit(FRIEND_CANVOTE);
    } else {
	if (dashf(buf))
	    unlink(buf);
    }
    clear();
    getdata(0, 0, "Ωщ布秈︽碭ぱ (ぱ)", inbuf, 4, DOECHO);

    closetime = atoi(inbuf);
    if (closetime <= 0)
	closetime = 1;
    else if (closetime > 30)
	closetime = 30;

    closetime = closetime * 86400 + now;
    setbfile(buf, bname, STR_new_control);
    fp = fopen(buf, "w");
    assert(fp);
    fprintf(fp, "000,000\n%lu\n", closetime);

    outs("\n叫ㄌ块匡兜,  ENTER ЧΘ砞﹚");
    num = 0;
    x = 0;	/* x is the page number */
    while (!aborted) {
	if( num % 15 == 0 ){
	    for( i = num ; i < num + 15 ; ++i ){
		snprintf(buf, sizeof(buf), "\033[1;30m%c)\033[m ", i + 'A');
		move((i % 15) + 2, (i / 15) * 40);
		prints(buf);
	    }
	}
	snprintf(buf, sizeof(buf), "%c) ", num + 'A');
	getdata((num % 15) + 2, (num / 15) * 40, buf,
		inbuf, 37, DOECHO);
	if (*inbuf) {
	    fprintf(fp, "%1c) %s\n", (num + 'A'), inbuf);
	    num++;
	}
	if ((*inbuf == '\0' && num >= 1) || x == MAX_VOTE_PAGE)
	    aborted = 1;
	if (num == 30) {
	    x++;
	    num = 0;
	}
    }
    snprintf(buf, sizeof(buf), "叫拜–程щ碭布([1]°%d): ", x * 30 + num);

    getdata(t_lines - 3, 0, buf, inbuf, 3, DOECHO);

    if (atoi(inbuf) <= 0 || atoi(inbuf) > (x * 30 + num)) {
	inbuf[0] = '1';
	inbuf[1] = 0;
    }

    rewind(fp);
    fprintf(fp, "%3d,%3d\n", x * 30 + num, MAX(1, atoi(inbuf)));
    fclose(fp);

    if (fhp->bvote == 2)
	fhp->bvote = 0;
    else if (fhp->bvote == 1)
	fhp->bvote = 2;
    else if (fhp->bvote == 2)
	fhp->bvote = 1;

    fhp->bvote++;

    if (substitute_record(fn_board, fhp, sizeof(*fhp), pos) == -1)
	outs(err_board_update);
    reset_board(pos);
    outs("秨﹍щ布");

    return FULLUPDATE;
}

static int
vote_flag(char *bname, int index, char val)
{
    char            buf[256], flag;
    int             fd, num, size;

    if (index)
	snprintf(STR_new_flags, sizeof(STR_new_flags), "%s%d", STR_bv_flags, index);
    else
	strlcpy(STR_new_flags, STR_bv_flags, sizeof(STR_new_flags));

    num = usernum - 1;
    setbfile(buf, bname, STR_new_flags);
    if ((fd = open(buf, O_RDWR | O_CREAT, 0600)) == -1)
	return -1;
    size = lseek(fd, 0, SEEK_END);
    memset(buf, 0, sizeof(buf));
    while (size <= num) {
	write(fd, buf, sizeof(buf));
	size += sizeof(buf);
    }
    lseek(fd, num, SEEK_SET);
    read(fd, &flag, 1);
    if (flag == 0 && val != 0) {
	lseek(fd, num, SEEK_SET);
	write(fd, &val, 1);
    }
    close(fd);
    return flag;
}

static int
user_vote_one(char *bname, int ind)
{
    FILE           *cfp, *fcm;
    char            buf[STRLEN], redo;
    boardheader_t  *fhp;
    short	    pos = 0, i = 0, count, tickets, fd;
    short	    curr_page, item_num, max_page;
    char            inbuf[80], choices[31], vote[4], *chosen;
    time_t          closetime;

    if (ind) {
	snprintf(STR_new_ballots, sizeof(STR_new_ballots), "%s%d", STR_bv_ballots, ind);
	snprintf(STR_new_control, sizeof(STR_new_control), "%s%d", STR_bv_control, ind);
	snprintf(STR_new_desc, sizeof(STR_new_desc), "%s%d", STR_bv_desc, ind);
	snprintf(STR_new_flags, sizeof(STR_new_flags),"%s%d", STR_bv_flags, ind);
	snprintf(STR_new_comments, sizeof(STR_new_comments), "%s%d", STR_bv_comments, ind);
	snprintf(STR_new_limited, sizeof(STR_new_limited), "%s%d", STR_bv_limited, ind);
    } else {
	strlcpy(STR_new_ballots, STR_bv_ballots, sizeof(STR_new_ballots));
	strlcpy(STR_new_control, STR_bv_control, sizeof(STR_new_control));
	strlcpy(STR_new_desc, STR_bv_desc, sizeof(STR_new_desc));
	strlcpy(STR_new_flags, STR_bv_flags, sizeof(STR_new_flags));
	strlcpy(STR_new_comments, STR_bv_comments, sizeof(STR_new_comments));
	strlcpy(STR_new_limited, STR_bv_limited, sizeof(STR_new_limited));
    }

    setbfile(buf, bname, STR_new_control);
    cfp = fopen(buf, "r");
    if (!cfp)
	return FULLUPDATE;

    setbfile(buf, bname, STR_new_limited);	/* Ptt */
    if (dashf(buf)) {
	setbfile(buf, bname, FN_CANVOTE);
	if (!belong(buf, cuser.userid)) {
	    fclose(cfp);
	    vmsg("癸ぃ癬! 硂琌╬щ布..⊿Τ淋!");
	    return FULLUPDATE;
	} else {
	    vmsg("尺淋Ω╬щ布.   <浪跌Ω淋虫>");
	    more(buf, YEA);
	}
    }
    if (vote_flag(bname, ind, '\0')) {
	vmsg("Ωщ布щ筁");
	return FULLUPDATE;
    }
    setutmpmode(VOTING);
    setbfile(buf, bname, STR_new_desc);
    more(buf, YEA);

    stand_title("щ布絚");
    if ((pos = getbnum(bname)) <= 0)
	return 0;

    fhp = bcache + pos - 1;
#if 1 // backward compatible
    setbfile(buf, bname, STR_new_control);
    setbfile(inbuf, bname, STR_new_ballots);
    convert_to_newversion(cfp, buf, inbuf);
#endif
    fscanf(cfp, "%hd,%hd\n%lu\n", &item_num, &tickets, &closetime);
    chosen = (char *)malloc(item_num);
    memset(chosen, 0, item_num);
    memset(choices, 0, sizeof(choices));
    max_page = (item_num - 1)/ 30 + 1;

    prints("щ布よΑ絋﹚眤匡拒块ㄤ絏(A, B, C...)\n"
	   "Ωщ布щ %1hd 布 0 щ布, 1 ЧΘщ布, > , < \n"
	   "Ωщ布盢挡%s \n",
	   tickets, ctime(&closetime));

#define REDO_DRAW	1
#define REDO_SCAN	2
    redo = REDO_DRAW;
    curr_page = 0;
    while (1) {

	if (redo) {
	    int i, j;
	    move(5, 0);
	    clrtobot();

	    /* 稱ぃよ猭 ぃ稱俱弄秈 memory
	     * τ场だщ布ぃ穦禬筁 ┮眖郎 scan */
	    if (redo & REDO_SCAN) {
		for (i = 0; i < curr_page; i++)
		    for (j = 0; j < 30; j++)
			fgets(inbuf, sizeof(inbuf), cfp);
	    }

	    count = 0;
	    for (i = 0; i < 30 && fgets(inbuf, sizeof(inbuf), cfp); i++) {
		move((count % 15) + 5, (count / 15) * 40);
		prints("%c%s", chosen[curr_page * 30 + i] ? '*' : ' ',
			strtok(inbuf, "\n\0"));
		choices[count % 30] = inbuf[0];
		count++;
	    }
	    redo = 0;
	}

	vote[0] = vote[1] = '\0';
	move(t_lines - 2, 0);
	prints("临щ %2d 布   [ ヘ玡┮计 %2d /  %2d  (块 '<' '>' 传) ]", tickets - i, curr_page + 1, max_page);
	getdata(t_lines - 4, 0, "块眤匡拒: ", vote, sizeof(vote), DOECHO);
	*vote = toupper(*vote);

#define CURRENT_CHOICE \
    chosen[curr_page * 30 + vote[0] - 'A']
	if (vote[0] == '0' || (!vote[0] && !i)) {
	    outs("癘眔ㄓщ翅!!");
	    break;
	} else if (vote[0] == '1' && i);
	else if (!vote[0])
	    continue;
	else if (vote[0] == '<' && max_page > 1) {
	    curr_page = (curr_page + max_page - 1) % max_page;
    	    rewind(cfp);
	    fgets(inbuf, sizeof(inbuf), cfp);
    	    fgets(inbuf, sizeof(inbuf), cfp);
	    redo = REDO_DRAW | REDO_SCAN;
	    continue;
	}
	else if (vote[0] == '>' && max_page > 1) {
	    curr_page = (curr_page + 1) % max_page;
	    if (curr_page == 0) {
		rewind(cfp);
		fgets(inbuf, sizeof(inbuf), cfp);
		fgets(inbuf, sizeof(inbuf), cfp);
	    }
	    redo = REDO_DRAW;
	    continue;
	}
	else if (index(choices, vote[0]) == NULL)	/* 礚 */
	    continue;
	else if ( CURRENT_CHOICE ) { /* 匡 */
	    move(((vote[0] - 'A') % 15) + 5, (((vote[0] - 'A')) / 15) * 40);
	    prints(" ");
	    CURRENT_CHOICE = 0;
	    i--;
	    continue;
	} else {
	    if (i == tickets)
		continue;
	    move(((vote[0] - 'A') % 15) + 5, (((vote[0] - 'A')) / 15) * 40);
	    prints("*");
	    CURRENT_CHOICE = 1;
	    i++;
	    continue;
	}

	if (vote_flag(bname, ind, vote[0]) != 0)
	    prints("狡щ布! ぃぉ璸布");
	else {
	    setbfile(buf, bname, STR_new_ballots);
	    if ((fd = open(buf, O_WRONLY | O_CREAT | O_APPEND, 0600)) == 0)
		outs("礚猭щ布詏\n");
	    else {
		struct stat     statb;
		char            buf[3], mycomments[3][74], b_comments[80];

		for (i = 0; i < 3; i++)
		    strlcpy(mycomments[i], "\n", sizeof(mycomments[i]));

		flock(fd, LOCK_EX);
		for (count = 0; count < item_num; count++) {
		    if (chosen[count])
			write(fd, &count, sizeof(short));
		}
		flock(fd, LOCK_UN);
		fstat(fd, &statb);
		close(fd);
		getdata(b_lines - 2, 0,
			"眤癸硂Ωщ布Τぐ或腳禥種ǎ盾(y/n)[N]",
			buf, 3, DOECHO);
		if (buf[0] == 'Y' || buf[0] == 'y') {
		    do {
			move(5, 0);
			clrtobot();
			outs("叫拜眤癸硂Ωщ布Τぐ或腳禥種ǎ"
			     "程︽[Enter]挡");
			for (i = 0; (i < 3) &&
			     getdata(7 + i, 0, "",
				     mycomments[i], sizeof(mycomments[i]),
				     DOECHO); i++);
			getdata(b_lines - 2, 0, "(S)纗 (E)穝ㄓ筁 "
				"(Q)[S]", buf, 3, LCECHO);
		    } while (buf[0] == 'E' || buf[0] == 'e');
		    if (buf[0] == 'Q' || buf[0] == 'q')
			break;
		    setbfile(b_comments, bname, STR_new_comments);
		    if (mycomments[0])
			if ((fcm = fopen(b_comments, "a"))) {
			    fprintf(fcm,
				    "\033[36m〕ㄏノ\033[1;36m %s "
				    "\033[;36m某\033[m\n",
				    cuser.userid);
			    for (i = 0; i < 3; i++)
				fprintf(fcm, "    %s\n", mycomments[i]);
			    fprintf(fcm, "\n");
			    fclose(fcm);
			}
		}
		move(b_lines - 1, 0);
		prints("ЧΘщ布\n");
	    }
	}
	break;
    }
    free(chosen);
    fclose(cfp);
    pressanykey();
    return FULLUPDATE;
}

static int
user_vote(char *bname)
{
    int             pos;
    boardheader_t  *fhp;
    char            buf[STRLEN];
    FILE           *fp, *xfp;
    int             i, x = -1;
    char            genbuf[STRLEN];
    char            inbuf[80];

    if ((pos = getbnum(bname)) <= 0)
	return 0;

    fhp = bcache + pos - 1;

    move(0, 0);
    clrtobot();

    if (fhp->bvote == 2 || fhp->bvote == 0) {
	vmsg("ヘ玡⊿Τヴщ布羭︽");
	return FULLUPDATE;
    }
    if (!HAS_PERM(PERM_LOGINOK)) {
	vmsg("癸ぃ癬! 眤ゼ骸烦, 临⊿Τщ布舦翅!");
	return FULLUPDATE;
    }
    strlcpy(STR_new_control, STR_bv_control, sizeof(STR_new_control));
    strlcpy(STR_new_title, STR_bv_title, sizeof(STR_new_title));
    setbfile(buf, bname, STR_new_control);
    move(0, 0);
    if ((fp = fopen(buf, "r"))) {
	prints("(0) ");
	x = 0;
	fclose(fp);

	setbfile(buf, bname, STR_new_title);
	if ((xfp = fopen(buf, "r"))) {
	    fgets(inbuf, sizeof(inbuf), xfp);
	    fclose(xfp);
	} else
	    strlcpy(inbuf, "礚夹肈", sizeof(inbuf));
	prints("%s\n", inbuf);
    }
    for (i = 1; i < 20; i++) {
	snprintf(STR_new_control, sizeof(STR_new_control),
		 "%s%d", STR_bv_control, i);
	snprintf(STR_new_title, sizeof(STR_new_title),
		 "%s%d", STR_bv_title, i);
	setbfile(buf, bname, STR_new_control);
	if ((fp = fopen(buf, "r"))) {
	    prints("(%d) ", i);
	    x = i;
	    fclose(fp);

	    setbfile(buf, bname, STR_new_title);
	    if ((xfp = fopen(buf, "r"))) {
		fgets(inbuf, sizeof(inbuf), xfp);
		fclose(xfp);
	    } else
		strlcpy(inbuf, "礚夹肈", sizeof(inbuf));
	    prints("%s\n", inbuf);
	}
    }

    if (x < 0)
	return FULLUPDATE;

    snprintf(buf, sizeof(buf), "璶щ碭腹щ布 [%d] ", x);

    getdata(b_lines - 1, 0, buf, genbuf, 4, LCECHO);

    if (atoi(genbuf) < 0 || atoi(genbuf) > 20)
	snprintf(genbuf, sizeof(genbuf), "%d", x);

    if (genbuf[0] != '0')
	snprintf(STR_new_control, sizeof(STR_new_control),
		 "%s%d", STR_bv_control, atoi(genbuf));
    else
	strlcpy(STR_new_control, STR_bv_control, sizeof(STR_new_control));

    setbfile(buf, bname, STR_new_control);

    if ((fp = fopen(buf, "r"))) { // TODO try access()
	fclose(fp);

	return user_vote_one(bname, atoi(genbuf));
    } else
	return FULLUPDATE;
}

static int
vote_results(char *bname)
{
    char            buf[STRLEN];

    setbfile(buf, bname, STR_bv_results);
    if (more(buf, YEA) == -1)
	vmsg("ヘ玡⊿Τヴщ布挡狦");
    return FULLUPDATE;
}

int
b_vote_maintain()
{
    return vote_maintain(currboard);
}

int
b_vote()
{
    return user_vote(currboard);
}

int
b_results()
{
    return vote_results(currboard);
}
