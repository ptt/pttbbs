/*
 * BBS implementation dependendent part
 * 
 * The only two interfaces you must provide
 * 
 * #include "inntobbs.h" int receive_article(); 0 success not 0 fail
 * 
 * if (storeDB(HEADER[MID_H], hispaths) < 0) { .... fail }
 * 
 * int cancel_article_front( char *msgid );          0 success not 0 fail
 * 
 * char *ptr = (char*)DBfetch(msgid);
 * 
 * 收到之文章內容 (body)在 char *BODY, 檔頭 (header)在 char *HEADER[] SUBJECT_H,
 * FROM_H, DATE_H, MID_H, NEWSGROUPS_H, NNTPPOSTINGHOST_H, NNTPHOST_H,
 * CONTROL_H, PATH_H, ORGANIZATION_H
 */

/*
 * Sample Implementation
 * 
 * receive_article()         --> post_article()   --> bbspost_write_post();
 * cacnel_article_front(mid) --> cancel_article() --> bbspost_write_cancel();
 */


#ifndef PowerBBS
#include "innbbsconf.h"
#include "daemon.h"
#include "bbslib.h"
#include "inntobbs.h"
#include "antisplam.h"

extern int      Junkhistory;

char           *post_article ARG((char *, char *, char *, int (*) (), char *, char *));
int cancel_article ARG((char *, char *, char *));


#ifdef  MapleBBS
#include "config.h"
#include "pttstruct.h"
#define _BBS_UTIL_C_
#else
report()
{
    /* Function called from record.o */
    /* Please leave this function empty */
}
#endif


#if defined(PalmBBS)

#ifndef PATH
#define PATH XPATH
#endif

#ifndef HEADER
#define HEADER XHEADER
#endif

#endif

/* process post write */
int
bbspost_write_post(fh, board, filename)
    int             fh;
    char           *board;
    char           *filename;
{
    char           *fptr, *ptr;
    FILE           *fhfd = fdopen(fh, "w");

    if (fhfd == NULL) {
	bbslog("can't fdopen, maybe disk full\n");
	return -1;
    }
    fprintf(fhfd, "發信人: %.60s, 看板: %s\n", FROM, board);
    fprintf(fhfd, "標  題: %.70s\n", SUBJECT);
    fprintf(fhfd, "發信站: %.43s (%s)\n", SITE, DATE);
    fprintf(fhfd, "轉信站: %.70s\n", PATH);

#ifndef MapleBBS
    if (POSTHOST != NULL) {
	fprintf(fhfd, "Origin: %.70s\n", POSTHOST);
    }
#endif

    fprintf(fhfd, "\n");
    for (fptr = BODY, ptr = strchr(fptr, '\r'); ptr != NULL && *ptr != '\0'; fptr = ptr + 1, ptr = strchr(fptr, '\r')) {
	int             ch = *ptr;
	*ptr = '\0';
	fputs(fptr, fhfd);
	*ptr = ch;
    }
    fputs(fptr, fhfd);

    fflush(fhfd);
    fclose(fhfd);
    return 0;
}

#ifdef  KEEP_NETWORK_CANCEL
/* process cancel write */
bbspost_write_cancel(fh, board, filename)
    int             fh;
    char           *board, *filename;
{
    char           *fptr, *ptr;
    FILE           *fhfd = fdopen(fh, "w"), *fp;
    char            buffer[256];

    if (fhfd == NULL) {
	bbslog("can't fdopen, maybe disk full\n");
	return -1;
    }
    fprintf(fhfd, "發信人: %s, 信區: %s\n", FROM, board);
    fprintf(fhfd, "標  題: %s\n", SUBJECT);
    fprintf(fhfd, "發信站: %.43s (%s)\n", SITE, DATE);
    fprintf(fhfd, "轉信站: %.70s\n", PATH);
    if (HEADER[CONTROL_H] != NULL) {
	fprintf(fhfd, "Control: %s\n", HEADER[CONTROL_H]);
    }
    if (POSTHOST != NULL) {
	fprintf(fhfd, "Origin: %s\n", POSTHOST);
    }
    fprintf(fhfd, "\n");
    for (fptr = BODY, ptr = strchr(fptr, '\r'); ptr != NULL && *ptr != '\0'; fptr = ptr + 1, ptr = strchr(fptr, '\r')) {
	int             ch = *ptr;
	*ptr = '\0';
	fputs(fptr, fhfd);
	*ptr = ch;
    }
    fputs(fptr, fhfd);
    if (POSTHOST != NULL) {
	fprintf(fhfd, "\n * Origin: ● %.26s ● From: %.40s\n", SITE, POSTHOST);
    }
    fprintf(fhfd, "\n---------------------\n");
    fp = fopen(filename, "r");
    if (fp == NULL) {
	bbslog("can't open %s\n", filename);
	return -1;
    }
    while (fgets(buffer, sizeof buffer, fp) != NULL) {
	fputs(buffer, fhfd);
    }
    fclose(fp);
    fflush(fhfd);
    fclose(fhfd);

    {
	fp = fopen(filename, "w");
	if (fp == NULL) {
	    bbslog("can't write %s\n", filename);
	    return -1;
	}
	fprintf(fp, "發信人: %s, 信區: %s\n", FROM, board);
	fprintf(fp, "標  題: %.70s\n", SUBJECT);
	fprintf(fp, "發信站: %.43s (%s)\n", SITE, DATE);
	fprintf(fp, "轉信站: %.70s\n", PATH);
	if (POSTHOST != NULL) {
	    fprintf(fhfd, "Origin: %s\n", POSTHOST);
	}
	if (HEADER[CONTROL_H] != NULL) {
	    fprintf(fhfd, "Control: %s\n", HEADER[CONTROL_H]);
	}
	fprintf(fp, "\n");
	for (fptr = BODY, ptr = strchr(fptr, '\r'); ptr != NULL && *ptr != '\0'; fptr = ptr + 1, ptr = strchr(fptr, '\r')) {
	    *ptr = '\0';
	    fputs(fptr, fp);
	}
	fputs(fptr, fp);
	if (POSTHOST != NULL) {
	    fprintf(fp, "\n * Origin: ● %.26s ● From: %.40s\n", SITE, POSTHOST);
	}
	fclose(fp);
    }
    return 0;
}
#endif


int
bbspost_write_control(fh, board, filename)
    int             fh;
    char           *board;
    char           *filename;
{
    char           *fptr, *ptr;
    FILE           *fhfd = fdopen(fh, "w");

    if (fhfd == NULL) {
	bbslog("can't fdopen, maybe disk full\n");
	return -1;
    }
    fprintf(fhfd, "Path: %s!%s\n", MYBBSID, HEADER[PATH_H]);
    fprintf(fhfd, "From: %s\n", FROM);
    fprintf(fhfd, "Newsgroups: %s\n", GROUPS);
    fprintf(fhfd, "Subject: %s\n", SUBJECT);
    fprintf(fhfd, "Date: %s\n", DATE);
    fprintf(fhfd, "Organization: %s\n", SITE);
    if (POSTHOST != NULL) {
	fprintf(fhfd, "NNTP-Posting-Host: %.70s\n", POSTHOST);
    }
    if (HEADER[CONTROL_H] != NULL) {
	fprintf(fhfd, "Control: %s\n", HEADER[CONTROL_H]);
    }
    if (HEADER[APPROVED_H] != NULL) {
	fprintf(fhfd, "Approved: %s\n", HEADER[APPROVED_H]);
    }
    if (HEADER[DISTRIBUTION_H] != NULL) {
	fprintf(fhfd, "Distribution: %s\n", HEADER[DISTRIBUTION_H]);
    }
    fprintf(fhfd, "\n");
    for (fptr = BODY, ptr = strchr(fptr, '\r'); ptr != NULL && *ptr != '\0'; fptr = ptr + 1, ptr = strchr(fptr, '\r')) {
	int             ch = *ptr;
	*ptr = '\0';
	fputs(fptr, fhfd);
	*ptr = ch;
    }
    fputs(fptr, fhfd);


    fflush(fhfd);
    fclose(fhfd);
    return 0;
}


time_t          datevalue;


/* process cancel write */
int
receive_article()
{
    char           *user, *userptr;
    char           *ngptr, *pathptr;
    char          **splitptr;
    static char     userid[32];
    static char     xdate[32];
    static char     xpath[180];
    newsfeeds_t    *nf;
    char           *boardhome;
    char            hispaths[4096];
    char            firstpath[MAXPATHLEN], *firstpathbase;
    char           *lesssym, *nameptrleft, *nameptrright;

#ifdef HMM_USE_ANTI_SPAM
    char           *notitle[] =
    {"行銷", "業務代表", "徵", "資訊", "免費", "大贈送", "傳銷", "未滿",
	"年費", "傳呼", "價", "操你媽", "未成年", "馬賽克", "信用", "賺錢",
	"=?", "!!!",
    "操你", "操你", "幹妳", "操妳", "**", "★★", "＊＊", "＄＄", "泡麵", NULL},
                   *nofrom[] =
    {"TaipeiNet.Net", "hotmail.com", "mt.touc.edu.tw", "ms11.hinet.net", NULL},
                   *nocont[] =
    {"名額有限", "優惠價", "動作要快", "訂購", "特價", "專賣", "BBC",
	"幹你", "操你", "幹妳", "操妳", "每片", "最新目錄", "http://", "收錢",
    "創業", "付款", "廣告信", "只賣", "市價", "NCg", "ICAg", NULL};
#endif

    if (FROM == NULL) {
	bbslog(":Err: article without usrid %s\n", MSGID);
	return 0;
    } else {
#ifdef HMM_USE_ANTI_SPAM
	for (i = 0; nofrom[i]; i++)
	    if (strstr(FROM, nofrom[i])) {
		morelog_to(INNBBSD_SPAM, "spam from [%s]: %s\n", nofrom[i], FROM);
		morelog_to(INNBBSD_SPAM, "  %s %s %s %s\n", FROM, PATH, GROUPS, SUBJECT);
		bbslog(":Ptt: spam from [%s]: %s\n", nofrom[i], FROM);
		return 0;
	    }
#endif
    }

    if (!BODY) {
	bbslog(":Err: article without body %s\n", MSGID);
	return 0;
    } else {
#ifdef HMM_USE_ANTI_SPAM
	for (i = 0; nocont[i]; i++)
	    if (strstr(BODY, nocont[i])) {
		morelog_to(INNBBSD_SPAM, "spam body [%s]: %s\n", nocont[i]);
		morelog_to(INNBBSD_SPAM, "  %s %s %s %s\n", FROM, PATH, GROUPS, SUBJECT);
		bbslog(":Ptt: spam body [%s]: %s\n", nocont[i]);
		return 0;
	    }
#endif
    }

    if (!SUBJECT) {
	bbslog(":Err: article without subject %s\n", MSGID);
	return 0;
    } else {
#ifdef HMM_USE_ANTI_SPAM
	for (i = 0; notitle[i]; i++)
	    if (strstr(SUBJECT, notitle[i])) {
		morelog_to(INNBBSD_SPAM, "spam title [%s]: %s\n", notitle[i], SUBJECT);
		morelog_to(INNBBSD_SPAM, "  %s %s %s %s\n", FROM, PATH, GROUPS, SUBJECT);
		bbslog(":Ptt: spam title [%s]: %s\n", notitle[i], SUBJECT);
		return 0;
	    }
#endif
    }


    user = (char *)strchr(FROM, '@');
    lesssym = (char *)strchr(FROM, '<');
    nameptrleft = NULL, nameptrright = NULL;
    if (lesssym == NULL || lesssym >= user) {
	lesssym = FROM;
	nameptrleft = strchr(FROM, '(');
	if (nameptrleft != NULL)
	    nameptrleft++;
	nameptrright = strrchr(FROM, ')');
    } else {
	nameptrleft = FROM;
	nameptrright = strrchr(FROM, '<');
	lesssym++;
    }
    if (user != NULL) {
	*user = '\0';
	userptr = (char *)strchr(FROM, '.');
	if (userptr != NULL) {
	    *userptr = '\0';
	    strncpy(userid, lesssym, sizeof userid);
	    *userptr = '.';
	} else {
	    strncpy(userid, lesssym, sizeof userid);
	}
	*user = '@';
    } else {
	strncpy(userid, lesssym, sizeof userid);
    }
    strcat(userid, ".");

    {
	struct tm       tmbuf;

	strptime(DATE, "%d %b %Y %X ", &tmbuf);
	datevalue = timegm(&tmbuf);
    }

    if (datevalue > 0) {
	char           *p;
	strncpy(xdate, ctime(&datevalue), sizeof(xdate));
	p = (char *)strchr(xdate, '\n');
	if (p != NULL)
	    *p = '\0';
	DATE = xdate;
    }
#ifndef MapleBBS
    if (SITE == NULL || *SITE == '\0') {
	if (nameptrleft != NULL && nameptrright != NULL) {
	    char            savech = *nameptrright;
	    *nameptrright = '\0';
	    strncpy(sitebuf, nameptrleft, sizeof sitebuf);
	    *nameptrright = savech;
	    SITE = sitebuf;
	} else
	    /* SITE = "(Unknown)"; */
	    SITE = "";
    }
    if (strlen(MYBBSID) > 70) {
	bbslog(" :Err: your bbsid %s too long\n", MYBBSID);
	return 0;
    }
#endif

    sprintf(xpath, "%s!%.*s", MYBBSID, sizeof(xpath) - strlen(MYBBSID) - 2, PATH);
    PATH = xpath;
    for (pathptr = PATH; pathptr != NULL && (pathptr = strstr(pathptr, ".edu.tw")) != NULL;) {
	if (pathptr != NULL) {
	    strcpy(pathptr, pathptr + 7);
	}
    }
    xpath[71] = '\0';

#ifndef MapleBBS
    echomaillog();
#endif

    *hispaths = '\0';
    splitptr = (char **)BNGsplit(GROUPS);
    firstpath[0] = '\0';
    firstpathbase = firstpath;

    for (ngptr = *splitptr; ngptr != NULL; ngptr = *(++splitptr)) {
	char           *boardptr, *nboardptr;

	if (*ngptr == '\0')
	    continue;
	nf = (newsfeeds_t *) search_group(ngptr);
	if (nf == NULL) {
	    bbslog("unwanted \'%s\'\n", ngptr);
	    continue;
	}
	if (nf->board == NULL || !*nf->board)
	    continue;
	if (nf->path == NULL || !*nf->path)
	    continue;
	for (boardptr = nf->board, nboardptr = (char *)strchr(boardptr, ','); boardptr != NULL && *boardptr != '\0'; nboardptr = (char *)strchr(boardptr, ',')) {
	    if (nboardptr != NULL) {
		*nboardptr = '\0';
	    }
	    if (*boardptr == '\t') {
		goto boardcont;
	    }
	    boardhome = (char *)fileglue("%s/boards/%c/%s", BBSHOME, boardptr[0], boardptr);
	    if (!isdir(boardhome)) {
		bbslog(":Err: unable to write %s\n", boardhome);
	    } else {
		char           *fname;
		/*
		 * if ( !isdir( boardhome )) { bbslog( ":Err: unable to write
		 * %s\n",boardhome); testandmkdir(boardhome); }
		 */
		fname = (char *)post_article(boardhome, userid, boardptr,
				       bbspost_write_post, NULL, firstpath);
		if (fname != NULL) {
		    fname = (char *)fileglue("%s/%s", boardptr, fname);
		    if (firstpath[0] == '\0') {
			sprintf(firstpath, "%s/boards/%c, %s", BBSHOME, fname[0], fname);
			firstpathbase = firstpath + strlen(BBSHOME) + strlen("/boards/x/");
		    }
		    if (strlen(fname) + strlen(hispaths) + 1 < sizeof(hispaths)) {
			strcat(hispaths, fname);
			strcat(hispaths, " ");
		    }
		} else {
		    bbslog("fname is null %s\n", boardhome);
		    return -1;
		}
	    }

    boardcont:
	    if (nboardptr != NULL) {
		*nboardptr = ',';
		boardptr = nboardptr + 1;
	    } else
		break;

	}			/* for board1,board2,... */
	/*
	 * if (nngptr != NULL) ngptr = nngptr + 1; else break;
	 */
	if (*firstpathbase)
	    feedfplog(nf, firstpathbase, 'P');
    }
    if (*hispaths)
	bbsfeedslog(hispaths, 'P');

    if (Junkhistory || *hispaths) {
	if (storeDB(HEADER[MID_H], hispaths) < 0) {
	    bbslog("store DB fail\n");
	    /* I suspect here will introduce duplicated articles */
	    /* return -1; */
	}
    }
    return 0;
}

int
receive_control()
{
    char           *boardhome, *fname;
    char            firstpath[MAXPATHLEN], *firstpathbase;
    char          **splitptr, *ngptr;
    newsfeeds_t    *nf;

    bbslog("control post %s\n", HEADER[CONTROL_H]);
    boardhome = (char *)fileglue("%s/boards/c/control", BBSHOME);
    testandmkdir(boardhome);
    *firstpath = '\0';
    if (isdir(boardhome)) {
	fname = (char *)post_article(boardhome, FROM, "control", bbspost_write_control, NULL, firstpath);
	if (fname != NULL) {
	    if (firstpath[0] == '\0')
		sprintf(firstpath, "%s/boards/c/control/%s", BBSHOME, fname);
	    if (storeDB(HEADER[MID_H], (char *)fileglue("control/%s", fname)) < 0) {
	    }
	    bbsfeedslog(fileglue("control/%s", fname), 'C');
	    firstpathbase = firstpath + strlen(BBSHOME) + strlen("/boards/x/");
	    splitptr = (char **)BNGsplit(GROUPS);
	    for (ngptr = *splitptr; ngptr != NULL; ngptr = *(++splitptr)) {
		if (*ngptr == '\0')
		    continue;
		nf = (newsfeeds_t *) search_group(ngptr);
		if (nf == NULL)
		    continue;
		if (nf->board == NULL)
		    continue;
		if (nf->path == NULL)
		    continue;
		feedfplog(nf, firstpathbase, 'C');
	    }
	}
    }
    return 0;
}

int
cancel_article_front(msgid)
    char           *msgid;
{
    char           *ptr = (char *)DBfetch(msgid);
    char           *filelist, filename[2048];
    char            histent[4096];
    char            firstpath[MAXPATHLEN], *firstpathbase;
    if (ptr == NULL) {
	bbslog("cancel failed(DBfetch): %s\n", msgid);
	return 0;
    }
    strncpy(histent, ptr, sizeof histent);
    ptr = histent;

#ifdef DEBUG
    printf("**** try to cancel %s *****\n", ptr);
#endif

    filelist = strchr(ptr, '\t');
    if (filelist != NULL) {
	filelist++;
    }
    *firstpath = '\0';
    for (ptr = filelist; ptr && *ptr;) {
	char           *file;
	for (; *ptr && isspace(*ptr); ptr++);
	if (*ptr == '\0')
	    break;
	file = ptr;
	for (ptr++; *ptr && !isspace(*ptr); ptr++);
	if (*ptr != '\0') {
	    *ptr++ = '\0';
	}
	sprintf(filename, "%s/boards/%c/%s", BBSHOME, file[0], file);
	bbslog("cancel post %s\n", filename);
	if (isfile(filename)) {
	    FILE           *fp = fopen(filename, "r");
	    char            buffer[1024];
	    char            xfrom0[100], xfrom[100], xpath[1024];

	    if (fp == NULL)
		continue;
	    strncpy(xfrom0, HEADER[FROM_H], 99);
	    xfrom0[99] = 0;
	    strtok(xfrom0, ", ");
	    while (fgets(buffer, sizeof buffer, fp) != NULL) {
		char           *hptr;
		if (buffer[0] == '\n')
		    break;
		hptr = strchr(buffer, '\n');
		if (hptr != NULL)
		    *hptr = '\0';
		if (strncmp(buffer, "發信人: ", 8) == 0) {
		    strncpy(xfrom, buffer + 8, 99);
		    xfrom[99] = 0;
		    strtok(xfrom, ", ");
		} else if (strncmp(buffer, "轉信站: ", 8) == 0) {
		    strcpy(xpath, buffer + 8);
		}
	    }
	    fclose(fp);
	    if (strcmp(xfrom0, xfrom) && !search_issuer(FROM)) {
		bbslog("Invalid cancel %s, path: %s!%s, [`%s` != `%s`]\n",
		       FROM, MYBBSID, PATH, xfrom0, xfrom);
		return 0;
	    }
#ifdef  KEEP_NETWORK_CANCEL
	    bbslog("cancel post %s\n", filename);
	    boardhome = (char *)fileglue("%s/boards/d/deleted", BBSHOME);
	    testandmkdir(boardhome);
	    if (isdir(boardhome)) {
		char            subject[1024];
		char           *fname;
		if (POSTHOST) {
		    sprintf(subject, "cancel by: %.1000s", POSTHOST);
		} else {
		    char           *body, *body2;
		    body = strchr(BODY, '\r');
		    if (body != NULL)
			*body = '\0';
		    body2 = strchr(BODY, '\n');
		    if (body2 != NULL)
			*body = '\0';
		    sprintf(subject, "%.1000s", BODY);
		    if (body != NULL)
			*body = '\r';
		    if (body2 != NULL)
			*body = '\n';
		}
		if (*subject) {
		    SUBJECT = subject;
		}
		fname = (char *)post_article(boardhome, FROM, "deleted", bbspost_write_cancel, filename, firstpath);
		if (fname != NULL) {
		    if (firstpath[0] == '\0') {
			sprintf(firstpath, "%s/boards/d/deleted/%s", BBSHOME, fname);
			firstpathbase = firstpath + strlen(BBSHOME) + strlen("/boards/x/");
		    }
		    if (storeDB(HEADER[MID_H], (char *)fileglue("deleted/%s", fname)) < 0) {
			/* should do something */
			bbslog("store DB fail\n");
			/* return -1; */
		    }
		    bbsfeedslog(fileglue("deleted/%s", fname), 'D');

#ifdef OLDDISPATCH
		    {
			char            board[256];
			newsfeeds_t    *nf;
			char           *filebase = filename + strlen(BBSHOME) + strlen("/boards/x/");
			char           *filetail = strrchr(filename, '/');
			if (filetail != NULL) {
			    strncpy(board, filebase, filetail - filebase);
			    nf = (newsfeeds_t *) search_board(board);
			    if (nf != NULL && nf->board && nf->path) {
				feedfplog(nf, firstpathbase, 'D');
			    }
			}
		    }
#endif
		} else {
		    bbslog(" fname is null %s %s\n", boardhome, filename);
		    return -1;
		}
	    }
#else
	    /* bbslog("**** %s should be removed\n", filename); */
	    /*
	     * unlink(filename);
	     */
#endif

	    {
		char           *fp = strrchr(file, '/');
		if (fp != NULL) {
		    *fp = '\0';
		    cancel_article(BBSHOME, file, fp + 1);
		    *fp = '/';
		}
	    }
	}
    }
    if (*firstpath) {
	char          **splitptr, *ngptr;
	newsfeeds_t    *nf;
	splitptr = (char **)BNGsplit(GROUPS);
	for (ngptr = *splitptr; ngptr != NULL; ngptr = *(++splitptr)) {
	    if (*ngptr == '\0')
		continue;
	    nf = (newsfeeds_t *) search_group(ngptr);
	    if (nf == NULL)
		continue;
	    if (nf->board == NULL)
		continue;
	    if (nf->path == NULL)
		continue;
	    feedfplog(nf, firstpathbase, 'D');
	}
    }
    return 0;
}


#if defined(PhoenixBBS) || defined(SecretBBS) || defined(PivotBBS) || defined(MapleBBS)
/* for PhoenixBBS's post article and cancel article */
#include "config.h"


char           *
post_article(homepath, userid, board, writebody, pathname, firstpath)
    char           *homepath;
    char           *userid, *board;
    int             (*writebody) ();
    char           *pathname, *firstpath;
{
    struct fileheader_t header;
    char           *subject = SUBJECT;
    char            index[MAXPATHLEN];
    static char     name[MAXPATHLEN];
    char            article[MAXPATHLEN];
    FILE           *fidx;
    int             fh, bid;
    time_t          now;
    int             linkflag;
    /*
     * Ptt if(bad_subject(subject))  return NULL;
     */
    sprintf(index, "%s/.DIR", homepath);
    if ((fidx = fopen(index, "r")) == NULL) {
	if ((fidx = fopen(index, "w")) == NULL) {
	    bbslog(":Err: Unable to post in %s.\n", homepath);
	    return NULL;
	}
    }
    fclose(fidx);

    now = time(NULL);
    while (1) {
	sprintf(name, "M.%d.A", ++now);
	sprintf(article, "%s/%s", homepath, name);
	fh = open(article, O_CREAT | O_EXCL | O_WRONLY, 0644);
	if (fh >= 0)
	    break;
	if (errno != EEXIST) {
	    bbslog(" Err: can't writable or other errors\n");
	    return NULL;
	}
    }

#ifdef DEBUG
    printf("post to %s\n", article);
#endif

    linkflag = 1;
    if (firstpath && *firstpath) {
	close(fh);
	unlink(article);

#ifdef DEBUGLINK
	bbslog("try to link %s to %s", firstpath, article);
#endif

	linkflag = link(firstpath, article);
	if (linkflag) {
	    fh = open(article, O_CREAT | O_EXCL | O_WRONLY, 0644);
	}
    }
    if (linkflag) {
	if (writebody) {
	    if ((*writebody) (fh, board, pathname) < 0)
		return NULL;
	} else {
	    if (bbspost_write_post(fh, board, pathname) < 0)
		return NULL;
	}
	close(fh);
    }
    bzero((void *)&header, sizeof(header));

#ifndef MapleBBS
    strcpy(header.filename, name);
    strncpy(header.owner, userid, IDLEN);
    strncpy(header.title, subject, STRLEN);
    header.filename[STRLEN - 1] = 'M';
#else

    strcpy(header.filename, name);
    if (userid[IDLEN])
	strcpy(&userid[IDLEN], ".");
    strcpy(header.owner, userid);
    strncpy(header.title, subject, TTLEN);
    header.filemode |= FILE_MULTI;
    {
	struct tm      *ptime;
	ptime = localtime(&datevalue);
	sprintf(header.date, "%2d/%02d", ptime->tm_mon + 1, ptime->tm_mday);
    }
#endif
    {
	int     i;
	for( i = 0 ; header.title[i] != 0 && i < sizeof(header.title) ; ++i )
	    if( header.title[i] == '\n' ||
		header.title[i] == '\r' ||
		header.title[i] == '\033' ){
		header.title[i] = 0;
		break;
	    }
    }
    append_record(index, &header, sizeof(header));

    if ((bid = getbnum(board)) > 0) {
	touchbtotal(bid);
    }
    return name;
}

/*
 * woju Cross-fs rename()
 */

int
Rename(const char *src, const char *dst)
{
    char            cmd[200];

    bbslog("Rename: %s -> %s\n", src, dst);
    if (rename(src, dst) == 0)
	return 0;

    sprintf(cmd, "/bin/mv %s %s", src, dst);
    return system(cmd);
}


void
cancelpost(fileheader_t * fhdr, char *boardname)
{
    int             fd;
    char            fpath[MAXPATHLEN];

    sprintf(fpath, BBSHOME "/boards/%c/%s/%s", boardname[0], boardname, fhdr->filename);
    if ((fd = open(fpath, O_RDONLY)) >= 0) {
	fileheader_t    postfile;
	char            fn2[MAXPATHLEN] = BBSHOME "/boards/d/deleted",
	               *junkdir;

	stampfile(fn2, &postfile);
	memcpy(postfile.owner, fhdr->owner, IDLEN + TTLEN + 10);
	close(fd);
	Rename(fpath, fn2);
	strcpy(strrchr(fn2, '/') + 1, ".DIR");
	append_record(fn2, &postfile, sizeof(postfile));
    } else
	bbslog("cancelpost: %s opened error\n", fpath);
}


/* ---------------------------- */
/* new/old/lock file processing */
/* ---------------------------- */

typedef struct {
    char            newfn[MAXPATHLEN];
    char            oldfn[MAXPATHLEN];
    char            lockfn[MAXPATHLEN];
}               nol;


static void
nolfilename(n, fpath)
    nol            *n;
    char           *fpath;
{
    sprintf(n->newfn, "%s.new", fpath);
    sprintf(n->oldfn, "%s.old", fpath);
    sprintf(n->lockfn, "%s.lock", fpath);
}



static int
delete_record(const char *fpath, int size, int id)
{
    nol             my;
    char            abuf[512];
    int             fdr, fdw, fd;
    int             count;
    fileheader_t    fhdr;

    nolfilename(&my, fpath);

    if ((fd = open(my.lockfn, O_RDWR | O_CREAT | O_APPEND, 0644)) == -1)
	return -1;
    flock(fd, LOCK_EX);

    if ((fdr = open(fpath, O_RDONLY, 0)) == -1) {

#ifdef  HAVE_REPORT
	report("delete_record failed!!! (open)");
#endif

	flock(fd, LOCK_UN);
	close(fd);
	return -1;
    }
    if ((fdw = open(my.newfn, O_WRONLY | O_CREAT | O_EXCL, 0644)) == -1) {
	flock(fd, LOCK_UN);

#ifdef  HAVE_REPORT
	report("delete_record failed!!! (open tmpfile)");
#endif

	close(fd);
	close(fdr);
	return -1;
    }
    count = 1;
    while (read(fdr, abuf, size) == size) {
	if (id == count) {
	    memcpy(&fhdr, abuf, sizeof(fhdr));
	    bbslog("delete_record: %d, %s, %s\n", count, fhdr.owner, fhdr.title);
	}
	if (id != count++ && (write(fdw, abuf, size) == -1)) {

	    bbslog("delete_record: %s failed!!! (write)\n", fpath);
#ifdef  HAVE_REPORT
	    report("delete_record failed!!! (write)");
#endif

	    unlink(my.newfn);
	    close(fdr);
	    close(fdw);
	    flock(fd, LOCK_UN);
	    close(fd);
	    return -1;
	}
    }
    close(fdr);
    close(fdw);
    if (Rename(fpath, my.oldfn) == -1 || Rename(my.newfn, fpath) == -1) {

#ifdef  HAVE_REPORT
	report("delete_record failed!!! (Rename)");
#endif

	flock(fd, LOCK_UN);
	close(fd);
	return -1;
    }
    flock(fd, LOCK_UN);
    close(fd);
    return 0;
}

int
cancel_article(homepath, board, file)
    char           *homepath;
    char           *board, *file;
{
    struct fileheader_t header;
    struct stat     state;
    char            dirname[MAXPATHLEN];
    long            size, time, now;
    int             fd, lower, ent;


    if (file == NULL || file[0] != 'M' || file[1] != '.' ||
	(time = atoi(file + 2)) <= 0) {
	bbslog("cancel_article: invalid filename `%s`\n", file);
	return 0;
    }
    size = sizeof(header);
    sprintf(dirname, "%s/boards/%c/%s/.DIR", homepath, board[0], board);
    if ((fd = open(dirname, O_RDONLY)) == -1) {
	bbslog("cancel_article: open `%s` error\n", dirname);
	return 0;
    }
    fstat(fd, &state);
    ent = ((long)state.st_size) / size;
    lower = 0;
    while (1) {
	ent -= 8;
	if (ent <= 0 || lower >= 2)
	    break;
	lseek(fd, size * ent, SEEK_SET);
	if (read(fd, &header, size) != size) {
	    ent = 0;
	    break;
	}
	now = atoi(header.filename + 2);
	lower = (now < time) ? lower + 1 : 0;
    }
    if (ent < 0)
	ent = 0;
    while (read(fd, &header, size) == size) {
	if (strcmp(file, header.filename) == 0) {
	    if ((header.filemode & FILE_MARKED)
	     || (header.filemode & FILE_DIGEST) || (header.owner[0] == '-'))
		break;
	    delete_record(dirname, sizeof(fileheader_t), lseek(fd, 0, SEEK_CUR) / size);
	    cancelpost(&header, board);
	    break;
	}
	now = atoi(header.filename + 2);
	if (now > time)
	    break;
    }
    close(fd);
    return 0;
}

#elif defined(PalmBBS)
#undef PATH XPATH
#undef HEADER XHEADER
#include "server.h"

char           *
post_article(homepath, userid, board, writebody, pathname, firstpath)
    char           *homepath;
    char           *userid, *board;
    int             (*writebody) ();
    char           *pathname, *firstpath;
{
    PATH            msgdir, msgfile;
    static PATH     name;

    READINFO        readinfo;
    SHORT           fileid;
    char            buf[MAXPATHLEN];
    struct stat     stbuf;
    int             fh;

    strcpy(msgdir, homepath);
    if (stat(msgdir, &stbuf) == -1 || !S_ISDIR(stbuf.st_mode)) {
	/* A directory is missing! */
	bbslog(":Err: Unable to post in %s.\n", msgdir);
	return NULL;
    }
    get_filelist_ids(msgdir, &readinfo);

    for (fileid = 1; fileid <= BBS_MAX_FILES; fileid++) {
	int             oumask;
	if (test_readbit(&readinfo, fileid))
	    continue;
	fileid_to_fname(msgdir, fileid, msgfile);
	sprintf(name, "%04x", fileid);

#ifdef DEBUG
	printf("post to %s\n", msgfile);
#endif

	if (firstpath && *firstpath) {

#ifdef DEBUGLINK
	    bbslog("try to link %s to %s", firstpath, msgfile);
#endif

	    if (link(firstpath, msgfile) == 0)
		break;
	}
	oumask = umask(0);
	fh = open(msgfile, O_CREAT | O_EXCL | O_WRONLY, 0664);
	umask(oumask);
	if (writebody) {
	    if ((*writebody) (fh, board, pathname) < 0)
		return NULL;
	} else {
	    if (bbspost_write_post(fh, board, pathname) < 0)
		return NULL;
	}
	close(fh);
	break;
    }

#ifdef CACHED_OPENBOARD
    {
	char           *bname;
	bname = strrchr(msgdir, '/');
	if (bname)
	    notify_new_post(++bname, 1, fileid, stbuf.st_mtime);
    }
#endif

    return name;
}

cancel_article(homepath, board, file)
    char           *homepath;
    char           *board, *file;
{
    PATH            fname;

#ifdef  CACHED_OPENBOARD
    PATH            bdir;
    struct stat     stbuf;

    sprintf(bdir, "%s/boards/%c/%s", homepath, board[0], board);
    stat(bdir, &stbuf);
#endif

    sprintf(fname, "%s/boards/%c/%s/%s", homepath, board[0], board, file);
    unlink(fname);
    /* kill it now! the function is far small then original..  :) */
    /* because it won't make system load heavy like before */

#ifdef CACHED_OPENBOARD
    notify_new_post(board, -1, hex2SHORT(file), stbuf.st_mtime);
#endif
}

#else
error("You should choose one of the systems: PhoenixBBS, PowerBBS, or PalmBBS")
#endif

#else

receive_article()
{
}

cancel_article_front(msgid)
    char           *msgid;
{
}
#endif
