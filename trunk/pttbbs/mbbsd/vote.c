/* $Id: vote.c,v 1.4 2002/03/29 14:33:07 ptt Exp $ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "config.h"
#include "pttstruct.h"
#include "modes.h"
#include "common.h"
#include "perm.h"
#include "proto.h"

static int total;
extern int numboards;
extern boardheader_t *bcache;     /* Thor: for speed up */
extern char *err_board_update;
extern char *fn_board;
extern char *msg_seperator;
extern int t_lines, t_columns;  /* Screen size / width */
extern int b_lines;             /* Screen bottom line number: t_lines-1 */
extern int currmode;
extern int usernum;
extern char currboard[];        /* name of currently selected board */
extern userec_t cuser;

static char STR_bv_control[] = "control";  /* щ布ら戳 匡兜 */
static char STR_bv_desc[] = "desc";        /* щ布ヘ */
static char STR_bv_ballots[] = "ballots";
static char STR_bv_flags[] = "flags";
static char STR_bv_comments[] = "comments"; /* щ布種 */
static char STR_bv_limited[] = "limited"; /* ╬щ布 */
static char STR_bv_title[] = "vtitle";

static char STR_bv_results[] = "results";

static char STR_new_control[] = "control0\0";  /* щ布ら戳 匡兜 */
static char STR_new_desc[] = "desc0\0";        /* щ布ヘ */
static char STR_new_ballots[] = "ballots0\0";
static char STR_new_flags[] = "flags0\0";
static char STR_new_comments[] = "comments0\0"; /* щ布種 */
static char STR_new_limited[] = "limited0\0"; /* ╬щ布 */
static char STR_new_title[] = "vtitle0\0";

int strip_ansi(char *buf, char *str, int mode) {
    register int ansi, count = 0;

    for(ansi = 0; *str /*&& *str != '\n' */; str++) {
	if(*str == 27) {
	    if(mode) {
		if(buf)
		    *buf++ = *str;
		count++;
	    }
	    ansi = 1;
	} else if(ansi && strchr("[;1234567890mfHABCDnsuJKc=n", *str)) {
	    if((mode == NO_RELOAD && !strchr("c=n", *str)) ||
	       (mode == ONLY_COLOR && strchr("[;1234567890m", *str))) {
		if(buf)
		    *buf++ = *str;
		count++;
	    }
	    if(strchr("mHn ", *str))
		ansi = 0;
	} else {
	    ansi =0;
	    if(buf)
		*buf++ = *str;
	    count++;
	}
    }
    if(buf)
	*buf = '\0';
    return count;
}

void b_suckinfile(FILE *fp, char *fname) {
    FILE *sfp;

    if((sfp = fopen(fname, "r"))) {
	char inbuf[256];

	while(fgets(inbuf, sizeof(inbuf), sfp))
	    fputs(inbuf, fp);
	fclose(sfp);
    }
}

static void b_count(char *buf, int counts[]) {
    char inchar;
    int fd;

    memset(counts, 0, 31 * sizeof(counts[0]));
    total = 0;
    if((fd = open(buf, O_RDONLY)) != -1) {
	flock(fd, LOCK_EX);         /* Thor: ňゎ衡 */
	while(read(fd, &inchar, 1) == 1) {
	    counts[(int)(inchar - 'A')]++;
	    total++;
	}
	flock(fd, LOCK_UN);
	close(fd);
    }
}


static int b_nonzeroNum(char *buf) {
    int i = 0;
    char inchar;
    int fd;

    if((fd = open(buf, O_RDONLY)) != -1) {
	while(read(fd, &inchar, 1) == 1)
	    if(inchar)
		i++;
	close(fd);
    }
    return i;
}

static void vote_report(char *bname, char *fname, char *fpath) {
    register char *ip;
    time_t dtime;
    int fd, bid;
    fileheader_t header;

    ip = fpath;
    while(*(++ip));
    *ip++ = '/';

    /* get a filename by timestamp */

    dtime = time(0);
    for(;;) {
	sprintf(ip, "M.%ld.A", ++dtime);
	fd = open(fpath, O_CREAT | O_EXCL | O_WRONLY, 0644);
	if(fd >= 0)
	    break;
	dtime++;
    }
    close(fd);
    
    unlink(fpath);
    link(fname, fpath);
    
    /* append record to .DIR */
    
    memset(&header, 0, sizeof(fileheader_t));
    strcpy(header.owner, "[皑隔贝]");
    sprintf(header.title, "[%s] 狾 匡薄厨旧", bname);
    {
	register struct tm *ptime = localtime(&dtime);
	
	sprintf(header.date, "%2d/%02d", ptime->tm_mon + 1, ptime->tm_mday);
    }
    strcpy(header.filename, ip);

    strcpy(ip, ".DIR");
    if((fd = open(fpath, O_WRONLY | O_CREAT, 0644)) >= 0) {
	flock(fd, LOCK_EX);
	lseek(fd, 0, SEEK_END);
	write(fd, &header, sizeof(fileheader_t));
	flock(fd, LOCK_UN);
	close(fd);
        if((bid = getbnum(bname)) > 0)
    	   setbtotal(bid);

    }
   
}

static void b_result_one(boardheader_t *fh, int ind) {
    FILE *cfp, *tfp, *frp, *xfp;
    char *bname ;
    char buf[STRLEN];
    char inbuf[80];
    int counts[31];
    int num ;
    int junk;
    char b_control[64];
    char b_newresults[64];
    char b_report[64];
    time_t closetime, now;

    fh->bvote--;

    if(fh->bvote==0)
        fh->bvote=2;
    else if(fh->bvote==2)
        fh->bvote=1;

    if(ind) {
	sprintf(STR_new_ballots, "%s%d", STR_bv_ballots, ind);
	sprintf(STR_new_control, "%s%d", STR_bv_control, ind);
	sprintf(STR_new_desc, "%s%d", STR_bv_desc, ind);
	sprintf(STR_new_flags, "%s%d", STR_bv_flags, ind);
	sprintf(STR_new_comments, "%s%d", STR_bv_comments, ind);
	sprintf(STR_new_limited, "%s%d", STR_bv_limited, ind);
	sprintf(STR_new_title, "%s%d", STR_bv_title, ind);
    } else {
	strcpy(STR_new_ballots, STR_bv_ballots);
	strcpy(STR_new_control, STR_bv_control);
	strcpy(STR_new_desc, STR_bv_desc);
	strcpy(STR_new_flags, STR_bv_flags);
	strcpy(STR_new_comments, STR_bv_comments);
	strcpy(STR_new_limited, STR_bv_limited);
	strcpy(STR_new_title, STR_bv_title);
    }

    bname = fh->brdname;

    setbfile(buf, bname, STR_new_control);
    cfp = fopen(buf,"r");
    fscanf(cfp, "%d\n%lu\n", &junk, &closetime);
    fclose(cfp);

    setbfile(b_control, bname, "tmp");
    if(rename(buf, b_control) == -1)
	return;
    setbfile(buf, bname, STR_new_flags);
    num = b_nonzeroNum(buf);
    unlink(buf);
    setbfile(buf, bname, STR_new_ballots);
    b_count(buf, counts);
    unlink(buf);

    setbfile(b_newresults, bname, "newresults");
    if((tfp = fopen(b_newresults, "w")) == NULL)
	return;

    now = time(NULL);
    setbfile(buf, bname, STR_new_title);

    if((xfp=fopen(buf,"r"))) {
	fgets(inbuf, sizeof(inbuf), xfp);
	fprintf(tfp, "%s\n』 щ布嘿: %s\n\n", msg_seperator, inbuf);
    }
    
    fprintf(tfp, "%s\n』 щ布いゎ: %s\n\n』 布匡肈ヘ磞瓃:\n\n",
	    msg_seperator, ctime(&closetime));
    fh->vtime = now;
    
    setbfile(buf, bname, STR_new_desc);
    
    b_suckinfile(tfp, buf);
    unlink(buf);
    
    if((cfp = fopen(b_control, "r"))) {
	fgets(inbuf, sizeof(inbuf), cfp);
	fgets(inbuf, sizeof(inbuf), cfp);
	fprintf(tfp, "\n』щ布挡狦:(Τ %d щ布,–程щ %d 布)\n",
		num, junk);
	while(fgets(inbuf, sizeof(inbuf), cfp)) {
	    inbuf[(strlen(inbuf) - 1)] = '\0';
	    num = counts[inbuf[0] - 'A'];
	    fprintf(tfp, "    %-42s %3d 布   %02.2f%%\n", inbuf + 3, num,
		    (float)(num*100)/(float)(total));
	}
	fclose(cfp);
    }
    unlink(b_control);
    
    fprintf(tfp, "%s\n』 ㄏノ某\n\n", msg_seperator);
    setbfile(buf, bname, STR_new_comments);
    b_suckinfile(tfp, buf);
    unlink(buf);
    
    fprintf(tfp, "%s\n』 羆布计 = %d 布\n\n", msg_seperator, total);
    fclose(tfp);
    
    setbfile(b_report, bname, "report");
    if((frp = fopen(b_report, "w"))) {
	b_suckinfile(frp, b_newresults);
	fclose(frp);
    }
    sprintf(inbuf, "boards/%c/%s", bname[0], bname);
    vote_report(bname, b_report, inbuf);
    if(!(fh->brdattr &BRD_NOCOUNT)) {
	sprintf(inbuf, "boards/%c/%s", 'R', "Record");
	vote_report(bname, b_report, inbuf);
    }
    unlink(b_report);
    
    tfp = fopen(b_newresults, "a");
    setbfile(buf, bname, STR_bv_results);
    b_suckinfile(tfp, buf);
    fclose(tfp);
    Rename(b_newresults, buf);
}

static void b_result(boardheader_t *fh) {
    FILE *cfp;
    time_t closetime, now;
    int i;
    char buf[STRLEN];
    char temp[STRLEN];

    now = time(NULL);
    for(i = 0; i < 20; i++) {
	if(i)
	    sprintf(STR_new_control, "%s%d", STR_bv_control, i);
        else
	    strcpy(STR_new_control, STR_bv_control);

        setbfile(buf, fh->brdname, STR_new_control);
        cfp = fopen(buf,"r");
        if (!cfp)
            continue;
        fgets(temp,sizeof(temp),cfp);
        fscanf(cfp, "%lu\n", &closetime);
        fclose(cfp);
        if(closetime < now)
	    b_result_one(fh,i);
    }
}

static int b_close(boardheader_t *fh) {
    time_t now;
    now = time(NULL);

    if(fh->bvote == 2) {
	if(fh->vtime < now - 3 * 86400) {
	    fh->bvote = 0;
	    return 1;
	}
	else
	    return 0;
    }
    b_result(fh);
    return 1;
}

int b_closepolls() {
    static char *fn_vote_polling = ".polling";
    boardheader_t *fhp;
    FILE *cfp;
    time_t now;
    int pos, dirty;
    time_t last;
    char timebuf[100];

    now = time(NULL);
/* Edited by CharlieL for can't auto poll bug */

    if((cfp = fopen(fn_vote_polling,"r"))) {
        fgets(timebuf,100*sizeof(char),cfp);
        sscanf(timebuf, "%lu", &last);
        fclose(cfp);
        if(last + 3600 >= now)
            return 0;
    }

    if((cfp = fopen(fn_vote_polling, "w")) == NULL)
	return 0;
    fprintf(cfp, "%lu\n%s\n", now, ctime(&now));
    fclose(cfp);

    dirty = 0;
    for(fhp = bcache, pos = 1; pos <= numboards; fhp++, pos++) {
	if(fhp->bvote && b_close(fhp)) {
	    if(substitute_record(fn_board, fhp, sizeof(*fhp), pos) == -1)
		outs(err_board_update);
	    dirty = 1;
	}
    }
    if(dirty) /* vote flag changed */
	reset_board(pos);

    return 0;
}

static int vote_view(char *bname, int index) {
    boardheader_t *fhp;
    FILE* fp;
    char buf[STRLEN], genbuf[STRLEN], inbuf[STRLEN];
    struct stat stbuf;
    int fd, num = 0, i, pos, counts[31];
    time_t closetime;

    if(index) {
	sprintf(STR_new_ballots, "%s%d", STR_bv_ballots, index);
	sprintf(STR_new_control, "%s%d", STR_bv_control, index);
	sprintf(STR_new_desc, "%s%d", STR_bv_desc, index);
	sprintf(STR_new_flags, "%s%d", STR_bv_flags, index);
	sprintf(STR_new_comments, "%s%d", STR_bv_comments, index);
	sprintf(STR_new_limited, "%s%d", STR_bv_limited, index);
	sprintf(STR_new_title, "%s%d", STR_bv_title, index);
    } else {
	strcpy(STR_new_ballots, STR_bv_ballots);
	strcpy(STR_new_control, STR_bv_control);
	strcpy(STR_new_desc, STR_bv_desc);
	strcpy(STR_new_flags, STR_bv_flags);
	strcpy(STR_new_comments, STR_bv_comments);
	strcpy(STR_new_limited, STR_bv_limited);
	strcpy(STR_new_title, STR_bv_title);
    }

    setbfile(buf, bname, STR_new_ballots);
    if((fd = open(buf, O_RDONLY)) > 0) {
	fstat(fd, &stbuf);
	close(fd);
    } else
	stbuf.st_size = 0;
    
    setbfile(buf, bname, STR_new_title);
    move(0, 0);
    clrtobot();
    
    if((fp = fopen(buf, "r"))) {
	fgets(inbuf, sizeof(inbuf), fp);
	prints("\nщ布嘿: %s", inbuf);
    }
    
    setbfile(buf, bname, STR_new_control);
    fp = fopen(buf, "r");
    fgets(inbuf, sizeof(inbuf), fp);
    fscanf(fp, "%lu\n", &closetime);

    prints("\n』 箇щ布ㄆ: –程щ %d 布,ヘ玡Τ %d 布,\n"
	   "セΩщ布盢挡 %s", atoi(inbuf),  stbuf.st_size,
	   ctime(&closetime));
    
    /* Thor: 秨 布计 箇 */
    setbfile(buf, bname, STR_new_flags);
    num = b_nonzeroNum(buf);
    
    setbfile(buf, bname, STR_new_ballots);
    b_count(buf, counts);
    
    prints("Τ %d щ布\n", num);
    total = 0;
    
    while(fgets(inbuf, sizeof(inbuf), fp)) {
	inbuf[(strlen(inbuf) - 1)] = '\0';
	inbuf[30] = '\0';         /* truncate */
	i = inbuf[0] - 'A';
	num = counts[i];
	move(i % 15 + 6, i / 15 * 40);
	prints("  %-32s%3d 布", inbuf, num);
	total += num;
    }
    fclose(fp);
    pos = getbnum(bname);
    fhp = bcache + pos - 1;
    move(t_lines - 3, 0);
    prints("』 ヘ玡羆布计 = %d 布", total);
    getdata(b_lines - 1, 0, "(A)щ布 (B)矗Ν秨布 (C)膥尿[C] ", genbuf,
	    4, LCECHO);
    if(genbuf[0] == 'a') {
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
	setbfile(buf,bname, STR_new_title);
	unlink(buf);
	
	if(fhp->bvote)
	    fhp->bvote--;
	if (fhp->bvote == 2)
	    fhp->bvote = 1;
	
	if(substitute_record(fn_board, fhp, sizeof(*fhp), pos) == -1)
	    outs(err_board_update);
	reset_board(pos);
    } else if(genbuf[0] == 'b') {
	b_result_one(fhp,index);
	if(substitute_record(fn_board, fhp, sizeof(*fhp), pos) == -1)
	    outs(err_board_update);
	
	reset_board(pos);
    }
    return FULLUPDATE;
}

static int vote_view_all(char *bname) {
    int i;
    int x = -1;
    FILE *fp, *xfp;
    char buf[STRLEN], genbuf[STRLEN];
    char inbuf[80];
    
    strcpy(STR_new_control, STR_bv_control);
    strcpy(STR_new_title, STR_bv_title);
    setbfile(buf, bname, STR_new_control);
    move(0, 0);
    if((fp=fopen(buf,"r"))) {
	prints("(0) ");
	x = 0;
	fclose(fp);
	
	setbfile(buf, bname, STR_new_title);
	if((xfp=fopen(buf,"r")))
	    fgets(inbuf, sizeof(inbuf), xfp);
	else
	    strcpy(inbuf, "礚夹肈");
	prints("%s\n", inbuf);
	fclose(xfp);
    }

    for(i = 1; i < 20; i++) {
	sprintf(STR_new_control, "%s%d", STR_bv_control, i);
	sprintf(STR_new_title, "%s%d", STR_bv_title, i);
	setbfile(buf, bname, STR_new_control);
	if((fp=fopen(buf,"r"))) {
	    prints("(%d) ", i);
	    x = i;
	    fclose(fp);

	    setbfile(buf, bname, STR_new_title);
	    if((xfp=fopen(buf,"r")))
		fgets(inbuf, sizeof(inbuf), xfp);
	    else
		strcpy(inbuf, "礚夹肈");
	    prints("%s\n", inbuf);
	    fclose(xfp);
	}
    }

    if(x < 0)
	return FULLUPDATE;
    sprintf(buf, "璶碭腹щ布 [%d] ", x);
    
    getdata(b_lines - 1, 0, buf, genbuf, 4, LCECHO);


    if(atoi(genbuf) < 0 || atoi(genbuf) > 20)
	sprintf(genbuf,"%d",x);
    if(genbuf[0] != '0')
	sprintf(STR_new_control, "%s%d", STR_bv_control, atoi(genbuf));
    else
	strcpy(STR_new_control, STR_bv_control);
    
    setbfile(buf, bname, STR_new_control);

    if((fp=fopen(buf,"r"))) {
	fclose(fp);
	return vote_view(bname, atoi(genbuf));
    }
    else
	return FULLUPDATE;
}

static int vote_maintain(char *bname) {
    FILE *fp = NULL;
    char inbuf[STRLEN], buf[STRLEN];
    int num = 0, aborted, pos, x, i;
    time_t closetime;
    boardheader_t *fhp;
    char genbuf[4];

    if(!(currmode & MODE_BOARD))
	return 0;
    if((pos = getbnum(bname)) <= 0)
	return 0;

    stand_title("羭快щ布");
    fhp = bcache + pos - 1;

/* CharlieL */
    if(fhp->bvote != 2 && fhp->bvote !=0) {
	getdata(b_lines - 1, 0,
		"(V)芠诡ヘ玡щ布 (M)羭快穝щ布 (A)┮Τщ布 (Q)膥尿 [Q]",
		genbuf, 4, LCECHO);
	if(genbuf[0] == 'v')
	    return vote_view_all(bname);
	else if(genbuf[0] == 'a') {
	    fhp->bvote=0;
	    
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
	    
	    for(i = 1; i < 20; i++) {
		sprintf(STR_new_ballots, "%s%d", STR_bv_ballots, i);
		sprintf(STR_new_control, "%s%d", STR_bv_control, i);
		sprintf(STR_new_desc, "%s%d", STR_bv_desc, i);
		sprintf(STR_new_flags, "%s%d", STR_bv_flags, i);
		sprintf(STR_new_comments, "%s%d", STR_bv_comments, i);
		sprintf(STR_new_limited, "%s%d", STR_bv_limited, i);
		sprintf(STR_new_title, "%s%d", STR_bv_title, i);
		
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
	    if(substitute_record(fn_board, fhp, sizeof(*fhp), pos) == -1)
		outs(err_board_update);
	    
	    return FULLUPDATE;
	} else if(genbuf[0] != 'm' || fhp->bvote >= 20)
	    return FULLUPDATE;
    }

    strcpy(STR_new_control, STR_bv_control);
    setbfile(buf,bname, STR_new_control);
    x = 0;
    while(x < 20 && (fp = fopen(buf,"r")) != NULL) {
	fclose(fp);
        x++;
        sprintf(STR_new_control, "%s%d", STR_bv_control, x);
	setbfile(buf, bname, STR_new_control);
    }
    if(fp)
	fclose(fp);
    if(x >=20)
        return FULLUPDATE;
    if(x) {
	sprintf(STR_new_ballots, "%s%d", STR_bv_ballots,x);
	sprintf(STR_new_control, "%s%d", STR_bv_control,x);
	sprintf(STR_new_desc, "%s%d", STR_bv_desc,x);
	sprintf(STR_new_flags, "%s%d", STR_bv_flags,x);
	sprintf(STR_new_comments, "%s%d", STR_bv_comments,x);
	sprintf(STR_new_limited, "%s%d", STR_bv_limited,x);
	sprintf(STR_new_title, "%s%d", STR_bv_title,x);
    } else {
	strcpy(STR_new_ballots, STR_bv_ballots);
	strcpy(STR_new_control, STR_bv_control);
	strcpy(STR_new_desc, STR_bv_desc);
	strcpy(STR_new_flags, STR_bv_flags);
	strcpy(STR_new_comments, STR_bv_comments);
	strcpy(STR_new_limited, STR_bv_limited);
	strcpy(STR_new_title, STR_bv_title);
    }
    clear();
    move(0,0);
    prints("材 %d 腹щ布\n", x);
    setbfile(buf, bname, STR_new_title);
    getdata(4, 0, "叫块щ布嘿", inbuf, 30, LCECHO);
    if(inbuf[0]=='\0')
	strcpy(inbuf,"ぃ");
    fp = fopen(buf, "w");
    fprintf(fp, "%s", inbuf);
    fclose(fp);
    
    prints("ヴ龄秨﹍絪胯Ω [щ布﹙Ξ]");
    pressanykey();
    setbfile(buf, bname, STR_new_desc);
    aborted = vedit(buf, NA, NULL);
    if(aborted== -1) {
	clear();
	outs("Ωщ布");
	pressanykey();
	return FULLUPDATE;
    }
    aborted = 0;
    setbfile(buf, bname, STR_new_flags);
    unlink(buf);
    
    getdata(4, 0,
	    "琌﹚щ布虫(y)絪膟щ布虫[n]ヴщ布:[N]",
	    inbuf, 2, LCECHO);
    setbfile(buf, bname, STR_new_limited);
    if(inbuf[0] == 'y') {
	fp = fopen(buf, "w");
	fprintf(fp,"Ωщ布砞");
	fclose(fp);
	friend_edit(FRIEND_CANVOTE);
    } else {
	if(dashf(buf))
	    unlink(buf);
    }
    clear();
    getdata(0, 0, "Ωщ布秈︽碭ぱ (ぱ)", inbuf, 4, DOECHO);

    closetime = atoi(inbuf);
    if(closetime <= 0)
	closetime = 1;
    else if(closetime >10)
	closetime = 10;
    
    closetime = closetime * 86400 + time(NULL);
    setbfile(buf, bname, STR_new_control);
    fp = fopen(buf, "w");
    fprintf(fp, "00\n%lu\n", closetime);

    outs("\n叫ㄌ块匡兜,  ENTER ЧΘ砞﹚");
    num = 0;
    while(!aborted) {
	sprintf(buf, "%c) ", num + 'A');
	getdata((num % 15) + 2, (num / 15) * 40, buf, inbuf, 36, DOECHO);
	if(*inbuf) {
	    fprintf(fp, "%1c) %s\n", (num+'A'), inbuf);
	    num++;
	}
	if((*inbuf == '\0' && num >= 1) || num == 30)
	    aborted = 1;
    }
    sprintf(buf, "叫拜–程щ碭布([1]°%d): ", num);
    
    getdata(t_lines-3, 0, buf, inbuf, 3, DOECHO);
    
    if(atoi(inbuf) <= 0 || atoi(inbuf) > num)
	strcpy(inbuf,"1");
    
    rewind(fp);
    fprintf(fp, "%2d\n", MAX(1, atoi(inbuf)));
    fclose(fp);
    
    if(fhp->bvote == 2)
        fhp->bvote = 0;
    else if(fhp->bvote == 1)
        fhp->bvote = 2;
    else if(fhp->bvote == 2)
        fhp->bvote = 1;
    
    fhp->bvote ++;
    
    if(substitute_record(fn_board, fhp, sizeof(*fhp), pos) == -1)
	outs(err_board_update);
    reset_board(pos);
    outs("秨﹍щ布");
    
    return FULLUPDATE;
}

static int vote_flag(char *bname, int index, char val) {
    char buf[256], flag;
    int fd, num, size;

    if(index)
	sprintf(STR_new_flags, "%s%d", STR_bv_flags, index);
    else
	strcpy(STR_new_flags, STR_bv_flags);
    
    num = usernum - 1;
    setbfile(buf, bname, STR_new_flags);
    if((fd = open(buf, O_RDWR | O_CREAT, 0600)) == -1)
	return -1;
    size = lseek(fd, 0, SEEK_END);
    memset(buf, 0, sizeof(buf));
    while(size <= num) {
	write(fd, buf, sizeof(buf));
	size += sizeof(buf);
    }
    lseek(fd, num, SEEK_SET);
    read(fd, &flag, 1);
    if(flag == 0 && val != 0) {
	lseek(fd, num, SEEK_SET);
	write(fd, &val, 1);
    }
    close(fd);
    return flag;
}

static int same(char compare, char list[], int num) {
    int n;
    int rep = 0;

    for(n = 0; n < num; n++) {
	if(compare == list[n])
	    rep = 1;
	if(rep == 1)
	    list[n] = list[n + 1];
    }
    return rep;
}

static int user_vote_one(char *bname, int ind) {
    FILE* cfp,*fcm;
    char buf[STRLEN];
    boardheader_t *fhp;
    int pos = 0, i = 0, count = 0, tickets, fd;
    char inbuf[80], choices[31], vote[4], chosen[31];
    time_t closetime;

    if(ind) {
	sprintf(STR_new_ballots, "%s%d", STR_bv_ballots, ind);
	sprintf(STR_new_control, "%s%d", STR_bv_control, ind);
	sprintf(STR_new_desc, "%s%d", STR_bv_desc, ind);
	sprintf(STR_new_flags, "%s%d", STR_bv_flags, ind);
	sprintf(STR_new_comments, "%s%d", STR_bv_comments, ind);
	sprintf(STR_new_limited, "%s%d", STR_bv_limited, ind);
    } else {
	strcpy(STR_new_ballots, STR_bv_ballots);
	strcpy(STR_new_control, STR_bv_control);
	strcpy(STR_new_desc, STR_bv_desc);
	strcpy(STR_new_flags, STR_bv_flags);
	strcpy(STR_new_comments, STR_bv_comments);
	strcpy(STR_new_limited, STR_bv_limited);
    }

    setbfile(buf, bname, STR_new_control);
    cfp = fopen(buf,"r");
    if(!cfp)
        return FULLUPDATE;

    setbfile(buf, bname, STR_new_limited); /* Ptt */
    if(dashf(buf)) {
	setbfile(buf, bname, FN_CANVOTE);
	if(!belong(buf, cuser.userid)) {
	    fclose(cfp);
	    outs("\n\n癸ぃ癬! 硂琌╬щ布..⊿Τ淋!");
	    pressanykey();
	    return FULLUPDATE;
	} else {
	    outs("\n\n尺淋Ω╬щ布....<ヴ種龄浪跌Ω淋虫>");
	    pressanykey();
	    more(buf, YEA);
	}
    }
    if(vote_flag(bname, ind, '\0')) {
	outs("\n\nΩщ布щ筁");
	pressanykey();
	return FULLUPDATE;
    }
    
    setutmpmode(VOTING);
    setbfile(buf, bname, STR_new_desc);
    more(buf, YEA);

    stand_title("щ布絚");
    if((pos = getbnum(bname)) <= 0)
	return 0;

    fhp = bcache + pos - 1;
    fgets(inbuf, sizeof(inbuf), cfp);
    tickets = atoi(inbuf);
    fscanf(cfp,"%lu\n", &closetime);

    prints("щ布よΑ絋﹚眤匡拒块ㄤ絏(A, B, C...)\n"
	   "Ωщ布щ %1d 布"
	   " 0 щ布 , 1 ЧΘщ布\n"
	   "Ωщ布盢挡%s \n",
	   tickets, ctime(&closetime));
    move(5, 0);
    memset(choices, 0, sizeof(choices));
    memset(chosen , 0, sizeof(chosen));

    while(fgets(inbuf, sizeof(inbuf), cfp)) {
	move((count % 15) + 5, (count / 15) * 40);
	prints( " %s", strtok(inbuf, "\n\0"));
	choices[count++] = inbuf[0];
    }
    fclose(cfp);

    while(1) {
	vote[0] = vote[1] = '\0';
	move(t_lines - 2, 0);
	prints("临щ %2d 布", tickets - i);
	getdata(t_lines - 4, 0, "块眤匡拒: ", vote, 3, DOECHO);
	*vote = toupper(*vote);
	if(vote[0] == '0' || (!vote[0] && !i)) {
	    outs("癘ㄓщ翅!!");
	    break;
	} else if(vote[0] == '1' && i)
	    ;
	else if(!vote[0])
	    continue;
	else if(index(choices, vote[0]) == NULL) /* 礚 */
	    continue;
	else if(same(vote[0], chosen, i)) {
	    move(((vote[0] - 'A') % 15) + 5, (((vote[0] - 'A')) / 15) * 40);
	    prints(" ");
	    i--;
	    continue;
	} else {
	    if(i == tickets)
		continue;
	    chosen[i] = vote[0];
	    move(((vote[0]-'A') % 15) + 5, (((vote[0] - 'A')) / 15) * 40);
	    prints("*");
	    i++;
	    continue;
	}
	
	if(vote_flag(bname, ind, vote[0]) != 0)
	    prints("滦щ布! ぃぉ璸布");
	else {
	    setbfile(buf, bname, STR_new_ballots);
	    if((fd = open(buf, O_WRONLY | O_CREAT | O_APPEND, 0600)) == 0)
		outs("礚猭щ布詏\n");
	    else {
		struct stat statb;
		char buf[3], mycomments[3][74], b_comments[80];
		
		for(i = 0; i < 3; i++)
		    strcpy(mycomments[i], "\n");

		flock(fd, LOCK_EX);
		for(count = 0; count < 31; count++) {
		    if(chosen[count])
			write(fd, &chosen[count], 1);
		}
		flock(fd, LOCK_UN);
		fstat(fd, &statb);
		close(fd);
		getdata(b_lines - 2, 0,
			"眤癸硂Ωщ布Τぐ或腳禥種ǎ盾(y/n)[N]",
			buf, 3 ,DOECHO);
		if(buf[0] == 'Y' || buf[0] == 'y'){
		    do {
			move(5,0);clrtobot();
			outs("叫拜眤癸硂Ωщ布Τぐ或腳禥種ǎ"
			     "程︽[Enter]挡");
			for(i = 0; (i < 3) &&
				getdata(7 + i, 0, "", mycomments[i], 74,
					DOECHO); i++);
			getdata(b_lines-2,0, "(S)纗 (E)穝ㄓ筁 "
				"(Q)[S]", buf, 3, LCECHO);
		    } while(buf[0] == 'E' || buf[0] == 'e');
		    if(buf[0] == 'Q' || buf[0] == 'q')
			break;
		    setbfile(b_comments, bname, STR_new_comments);
		    if(mycomments[0])
			if((fcm = fopen(b_comments, "a"))){
			    fprintf(fcm, 
		                     "\033[36m〕ㄏノ\033[1;36m %s "
			             "\033[;36m某\033[m\n",
				    cuser.userid);
			    for(i = 0; i < 3; i++)
				fprintf(fcm, "    %s\n", mycomments[i]);
			    fprintf(fcm, "\n");
			    fclose(fcm);
		        }
		}
		move(b_lines - 1 ,0);
		prints("ЧΘщ布\n");
	    }
	}
	break;
    }
    pressanykey();
    return FULLUPDATE;
}

static int user_vote(char *bname) {
    int pos;
    boardheader_t *fhp;
    char buf[STRLEN];
    FILE* fp,*xfp;
    int i, x = -1;
    char genbuf[STRLEN];
    char inbuf[80];
    
    if((pos = getbnum(bname)) <= 0)
	return 0;
    
    fhp = bcache + pos - 1;
    
    move(0,0);
    clrtobot();
    
    if(fhp->bvote == 2 || fhp->bvote == 0) {
	outs("\n\nヘ玡⊿Τヴщ布羭︽");
	pressanykey();
	return FULLUPDATE;
    }
    
    if(!HAS_PERM(PERM_LOGINOK)) {
	outs("\n癸ぃ癬! 眤ゼ骸烦, 临⊿Τщ布舦翅!");
	pressanykey();
	return FULLUPDATE;
    }
    
    strcpy(STR_new_control, STR_bv_control);
    strcpy(STR_new_title, STR_bv_title);
    setbfile(buf, bname, STR_new_control);
    move(0, 0);
    if((fp = fopen(buf, "r"))) {
	prints("(0) ");
	x = 0;
	fclose(fp);

	setbfile(buf, bname, STR_new_title);
	if((xfp = fopen(buf,"r")))
	    fgets(inbuf, sizeof(inbuf), xfp);
	else
	    strcpy(inbuf, "礚夹肈");
	prints("%s\n", inbuf);
	fclose(xfp);
    }
    
    for(i = 1; i < 20; i++) {
	sprintf(STR_new_control, "%s%d", STR_bv_control, i);
	sprintf(STR_new_title, "%s%d", STR_bv_title, i);
	setbfile(buf, bname, STR_new_control);
	if((fp = fopen(buf, "r"))) {
	    prints("(%d) ", i);
	    x = i;
	    fclose(fp);
	    
	    setbfile(buf, bname, STR_new_title);
	    if((xfp = fopen(buf, "r")))
		fgets(inbuf, sizeof(inbuf), xfp);
	    else
		strcpy(inbuf, "礚夹肈");
	    prints("%s\n", inbuf);
	    fclose(xfp);
	}
    }
    
    if(x < 0)
	return FULLUPDATE;
    
    sprintf(buf, "璶щ碭腹щ布 [%d] ", x);
    
    getdata(b_lines - 1, 0, buf, genbuf, 4, LCECHO);
    
    if(atoi(genbuf) < 0 || atoi(genbuf) > 20)
	sprintf(genbuf,"%d",x);
    
    if(genbuf[0] != '0')
	sprintf(STR_new_control, "%s%d", STR_bv_control, atoi(genbuf));
    else
	strcpy(STR_new_control, STR_bv_control);
    
    setbfile(buf, bname, STR_new_control);

    if((fp = fopen(buf, "r"))){
	fclose(fp);
	
	return user_vote_one(bname, atoi(genbuf));
    } else
	return FULLUPDATE;
}

static int vote_results(char *bname) {
    char buf[STRLEN];

    setbfile(buf, bname, STR_bv_results);
    if(more(buf, YEA) == -1)
	outs("\nヘ玡⊿Τヴщ布挡狦");
    return FULLUPDATE;
}

int b_vote_maintain() {
    return vote_maintain(currboard);
}

int b_vote() {
    return user_vote(currboard);
}

int b_results() {
    return vote_results(currboard);
}
