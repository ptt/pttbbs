#include <stdlib.h>
#if defined( LINUX )
#include "innbbsconf.h"
#include "bbslib.h"
#include <stdarg.h>
#else
#include <stdarg.h>
#include "innbbsconf.h"
#include "bbslib.h"
#endif
#include "config.h"

char            INNBBSCONF[MAXPATHLEN];
char            INNDHOME[MAXPATHLEN];
char            HISTORY[MAXPATHLEN];
char            LOGFILE[MAXPATHLEN];
char            MYBBSID[MAXPATHLEN];
char            ECHOMAIL[MAXPATHLEN];
char            BBSFEEDS[MAXPATHLEN];
char            LOCALDAEMON[MAXPATHLEN];

int             His_Maint_Min = HIS_MAINT_MIN;
int             His_Maint_Hour = HIS_MAINT_HOUR;
int             Expiredays = EXPIREDAYS;

nodelist_t     *NODELIST = NULL, **NODELIST_BYNODE = NULL;
newsfeeds_t    *NEWSFEEDS = NULL, **NEWSFEEDS_BYBOARD = NULL;
static char    *NODELIST_BUF, *NEWSFEEDS_BUF;
int             NFCOUNT, NLCOUNT;
int             LOCALNODELIST = 0, NONENEWSFEEDS = 0;

#ifndef _PATH_BBSHOME
#define _PATH_BBSHOME "/u/staff/bbsroot/csie_util/bntpd/home"
#endif

static FILE    *bbslogfp;

static int
                verboseFlag = 0;

static char    *
                verboseFilename = NULL;
static char     verbosename[MAXPATHLEN];

void
verboseon(filename)
    char           *filename;
{
    verboseFlag = 1;
    if (filename != NULL) {
	if (strchr(filename, '/') == NULL) {
	    sprintf(verbosename, "%s/innd/%s", BBSHOME, filename);
	    filename = verbosename;
	}
    }
    verboseFilename = filename;
}
void
verboseoff()
{
    verboseFlag = 0;
}

void
setverboseon()
{
    verboseFlag = 1;
}

int
isverboselog()
{
    return verboseFlag;
}

void
setverboseoff()
{
    verboseoff();
    if (bbslogfp != NULL) {
	fclose(bbslogfp);
	bbslogfp = NULL;
    }
}

void
verboselog(char *fmt,...)
{
    va_list         ap;
    char            datebuf[40];
    time_t          now;

    if (verboseFlag == 0)
	return;

    va_start(ap, fmt);

    time(&now);
    strftime(datebuf, sizeof(datebuf), "%b %d %X ", localtime(&now));

    if (bbslogfp == NULL) {
	if (verboseFilename != NULL)
	    bbslogfp = fopen(verboseFilename, "a");
	else
	    bbslogfp = fdopen(1, "a");
    }
    if (bbslogfp == NULL) {
	va_end(ap);
	return;
    }
    fprintf(bbslogfp, "%s[%d] ", datebuf, getpid());
    vfprintf(bbslogfp, fmt, ap);
    fflush(bbslogfp);
    va_end(ap);
}

void
#ifdef PalmBBS
xbbslog(char *fmt,...)
#else
bbslog(char *fmt,...)
#endif
{
    va_list         ap;
    char            datebuf[40];
    time_t          now;

    va_start(ap, fmt);

    time(&now);
    strftime(datebuf, sizeof(datebuf), "%b %d %X ", localtime(&now));

    if (bbslogfp == NULL) {
	bbslogfp = fopen(LOGFILE, "a");
    }
    if (bbslogfp == NULL) {
	va_end(ap);
	return;
    }
    fprintf(bbslogfp, "%s[%d] ", datebuf, getpid());
    vfprintf(bbslogfp, fmt, ap);
    fflush(bbslogfp);
    va_end(ap);
}

int
initial_bbs(outgoing)
    char           *outgoing;
{
    /* reopen bbslog */
    if (bbslogfp != NULL) {
	fclose(bbslogfp);
	bbslogfp = NULL;
    }
#ifdef WITH_ECHOMAIL
    init_echomailfp();
    init_bbsfeedsfp();
#endif

    LOCALNODELIST = 0, NONENEWSFEEDS = 0;
    sprintf(INNDHOME, "%s/innd", BBSHOME);
    sprintf(HISTORY, "%s/history", INNDHOME);
    sprintf(LOGFILE, "%s/bbslog", INNDHOME);
    sprintf(ECHOMAIL, "%s/echomail.log", BBSHOME);
    sprintf(LOCALDAEMON, "%s/.innbbsd", INNDHOME);
    sprintf(INNBBSCONF, "%s/innbbs.conf", INNDHOME);
    sprintf(BBSFEEDS, "%s/bbsfeeds.log", INNDHOME);

    if (isfile(INNBBSCONF)) {
	FILE           *conf;
	char            buffer[MAXPATHLEN];
	conf = fopen(INNBBSCONF, "r");
	if (conf != NULL) {
	    while (fgets(buffer, sizeof buffer, conf) != NULL) {
		char           *ptr, *front = NULL, *value = NULL, *value2 = NULL,
		               *value3 = NULL;
		if (buffer[0] == '#' || buffer[0] == '\n')
		    continue;
		for (front = buffer; *front && isspace(*front); front++);
		for (ptr = front; *ptr && !isspace(*ptr); ptr++);
		if (*ptr == '\0')
		    continue;
		*ptr++ = '\0';
		for (; *ptr && isspace(*ptr); ptr++);
		if (*ptr == '\0')
		    continue;
		value = ptr++;
		for (; *ptr && !isspace(*ptr); ptr++);
		if (*ptr) {
		    *ptr++ = '\0';
		    for (; *ptr && isspace(*ptr); ptr++);
		    value2 = ptr++;
		    for (; *ptr && !isspace(*ptr); ptr++);
		    if (*ptr) {
			*ptr++ = '\0';
			for (; *ptr && isspace(*ptr); ptr++);
			value3 = ptr++;
			for (; *ptr && !isspace(*ptr); ptr++);
			if (*ptr) {
			    *ptr++ = '\0';
			}
		    }
		}
		if (strcasecmp(front, "expiredays") == 0) {
		    Expiredays = atoi(value);
		    if (Expiredays < 0) {
			Expiredays = EXPIREDAYS;
		    }
		} else if (strcasecmp(front, "expiretime") == 0) {
		    ptr = strchr(value, ':');
		    if (ptr == NULL) {
			fprintf(stderr, "Syntax error in innbbs.conf\n");
		    } else {
			*ptr++ = '\0';
			His_Maint_Hour = atoi(value);
			His_Maint_Min = atoi(ptr);
			if (His_Maint_Hour < 0)
			    His_Maint_Hour = HIS_MAINT_HOUR;
			if (His_Maint_Min < 0)
			    His_Maint_Min = HIS_MAINT_MIN;
		    }
		} else if (strcasecmp(front, "newsfeeds") == 0) {
		    if (strcmp(value, "none") == 0)
			NONENEWSFEEDS = 1;
		} else if (strcasecmp(front, "nodelist") == 0) {
		    if (strcmp(value, "local") == 0)
			LOCALNODELIST = 1;
		}		/* else if ( strcasecmp(front,"newsfeeds") ==
				 * 0) { printf("newsfeeds %s\n", value); }
				 * else if ( strcasecmp(front,"nodelist") ==
				 * 0) { printf("nodelist %s\n", value); }
				 * else if ( strcasecmp(front,"bbsname") ==
				 * 0) { printf("bbsname %s\n", value); } */
	    }
	    fclose(conf);
	}
    }
#ifdef WITH_ECHOMAIL
    bbsnameptr = (char *)fileglue("%s/bbsname.bbs", INNDHOME);
    if ((FN = fopen(bbsnameptr, "r")) == NULL) {
	fprintf(stderr, "can't open file %s\n", bbsnameptr);
	return 0;
    }
    while (fscanf(FN, "%s", MYBBSID) != EOF);
    fclose(FN);
    if (!isdir(fileglue("%s/out.going", BBSHOME))) {
	mkdir((char *)fileglue("%s/out.going", BBSHOME), 0750);
    }
    if (NONENEWSFEEDS == 0)
	readnffile(INNDHOME);
    readNCMfile(INNDHOME);
    if (LOCALNODELIST == 0) {
	if (readnlfile(INNDHOME, outgoing) != 0)
	    return 0;
    }
#endif
    return 1;
}

static int
nf_byboardcmp(a, b)
    newsfeeds_t   **a, **b;
{
    /*
     * if (!a || !*a || !(*a)->board) return -1; if (!b || !*b ||
     * !(*b)->board) return 1;
     */
    return strcasecmp((*a)->board, (*b)->board);
}

static int
nfcmp(a, b)
    newsfeeds_t    *a, *b;
{
    /*
     * if (!a || !a->newsgroups) return -1; if (!b || !b->newsgroups) return
     * 1;
     */
    return strcasecmp(a->newsgroups, b->newsgroups);
}

static int
nlcmp(a, b)
    nodelist_t     *a, *b;
{
    /*
     * if (!a || !a->host) return -1; if (!b || !b->host) return 1;
     */
    return strcasecmp(a->host, b->host);
}

static int
nl_bynodecmp(a, b)
    nodelist_t    **a, **b;
{
    /*
     * if (!a || !*a || !(*a)->node) return -1; if (!b || !*b || !(*b)->node)
     * return 1;
     */
    return strcasecmp((*a)->node, (*b)->node);
}

/* read in newsfeeds.bbs and nodelist.bbs */
int
readnlfile(inndhome, outgoing)
    char           *inndhome;
    char           *outgoing;
{
    FILE           *fp;
    char            buff[1024];
    struct stat     st;
    int             i, count;
    char           *ptr, *nodelistptr;
    static int      lastcount = 0;

    sprintf(buff, "%s/nodelist.bbs", inndhome);
    fp = fopen(buff, "r");
    if (fp == NULL) {
	fprintf(stderr, "open fail %s", buff);
	return -1;
    }
    if (fstat(fileno(fp), &st) != 0) {
	fprintf(stderr, "stat fail %s", buff);
	return -1;
    }
    if (NODELIST_BUF == NULL) {
	NODELIST_BUF = (char *)mymalloc(st.st_size + 1);
    } else {
	NODELIST_BUF = (char *)myrealloc(NODELIST_BUF, st.st_size + 1);
    }
    i = 0, count = 0;
    while (fgets(buff, sizeof buff, fp) != NULL) {
	if (buff[0] == '#')
	    continue;
	if (buff[0] == '\n')
	    continue;
	strcpy(NODELIST_BUF + i, buff);
	i += strlen(buff);
	count++;
    }
    fclose(fp);
    if (NODELIST == NULL) {
	NODELIST = (nodelist_t *) mymalloc(sizeof(nodelist_t) * (count + 1));
	NODELIST_BYNODE = (nodelist_t **) mymalloc(sizeof(nodelist_t *) * (count + 1));
    } else {
	NODELIST = (nodelist_t *) myrealloc(NODELIST, sizeof(nodelist_t) * (count + 1));
	NODELIST_BYNODE = (nodelist_t **) myrealloc(NODELIST_BYNODE, sizeof(nodelist_t *) * (count + 1));
    }
    for (i = lastcount; i < count; i++) {
	NODELIST[i].feedfp = NULL;
    }
    lastcount = count;
    NLCOUNT = 0;
    for (ptr = NODELIST_BUF; (nodelistptr = (char *)strchr(ptr, '\n')) != NULL; ptr = nodelistptr + 1, NLCOUNT++) {
	char           *nptr, *tptr;
	*nodelistptr = '\0';
	NODELIST[NLCOUNT].host = "";
	NODELIST[NLCOUNT].exclusion = "";
	NODELIST[NLCOUNT].node = "";
	NODELIST[NLCOUNT].protocol = "IHAVE(119)";
	NODELIST[NLCOUNT].comments = "";
	NODELIST_BYNODE[NLCOUNT] = NODELIST + NLCOUNT;
	for (nptr = ptr; *nptr && isspace(*nptr);)
	    nptr++;
	if (*nptr == '\0') {
	    bbslog("nodelist.bbs %d entry read error\n", NLCOUNT);
	    return -1;
	}
	/* NODELIST[NLCOUNT].id = nptr; */
	NODELIST[NLCOUNT].node = nptr;
	for (nptr++; *nptr && !isspace(*nptr);)
	    nptr++;
	if (*nptr == '\0') {
	    bbslog("nodelist.bbs node %d entry read error\n", NLCOUNT);
	    return -1;
	}
	*nptr = '\0';
	if ((tptr = strchr(NODELIST[NLCOUNT].node, '/'))) {
	    *tptr = '\0';
	    NODELIST[NLCOUNT].exclusion = tptr + 1;
	} else {
	    NODELIST[NLCOUNT].exclusion = "";
	}
	for (nptr++; *nptr && isspace(*nptr);)
	    nptr++;
	if (*nptr == '\0')
	    continue;
	if (*nptr == '+' || *nptr == '-') {
	    NODELIST[NLCOUNT].feedtype = *nptr;
	    if (NODELIST[NLCOUNT].feedfp != NULL) {
		fclose(NODELIST[NLCOUNT].feedfp);
	    }
	    if (NODELIST[NLCOUNT].feedtype == '+')
		if (outgoing != NULL) {
		    NODELIST[NLCOUNT].feedfp = fopen((char *)fileglue("%s/out.going/%s.%s", BBSHOME, NODELIST[NLCOUNT].node, outgoing), "a");
		}
	    nptr++;
	} else {
	    NODELIST[NLCOUNT].feedtype = ' ';
	}
	NODELIST[NLCOUNT].host = nptr;
	for (nptr++; *nptr && !isspace(*nptr);)
	    nptr++;
	if (*nptr == '\0') {
	    continue;
	}
	*nptr = '\0';
	for (nptr++; *nptr && isspace(*nptr);)
	    nptr++;
	if (*nptr == '\0')
	    continue;
	NODELIST[NLCOUNT].protocol = nptr;
	for (nptr++; *nptr && !isspace(*nptr);)
	    nptr++;
	if (*nptr == '\0')
	    continue;
	*nptr = '\0';
	for (nptr++; *nptr && strchr(" \t\r\n", *nptr);)
	    nptr++;
	if (*nptr == '\0')
	    continue;
	NODELIST[NLCOUNT].comments = nptr;
    }
    qsort(NODELIST, NLCOUNT, sizeof(nodelist_t), nlcmp);
    qsort(NODELIST_BYNODE, NLCOUNT, sizeof(nodelist_t *), nl_bynodecmp);
    return 0;
}

int
readnffile(inndhome)
    char           *inndhome;
{
    FILE           *fp;
    char            buff[1024];
    struct stat     st;
    int             i, count;
    char           *ptr, *newsfeedsptr;

    sprintf(buff, "%s/newsfeeds.bbs", inndhome);
    fp = fopen(buff, "r");
    if (fp == NULL) {
	fprintf(stderr, "open fail %s", buff);
	return -1;
    }
    if (fstat(fileno(fp), &st) != 0) {
	fprintf(stderr, "stat fail %s", buff);
	return -1;
    }
    if (NEWSFEEDS_BUF == NULL) {
	NEWSFEEDS_BUF = (char *)mymalloc(st.st_size + 1);
    } else {
	NEWSFEEDS_BUF = (char *)myrealloc(NEWSFEEDS_BUF, st.st_size + 1);
    }
    i = 0, count = 0;
    while (fgets(buff, sizeof buff, fp) != NULL) {
	if (buff[0] == '#')
	    continue;
	if (buff[0] == '\n')
	    continue;
	strcpy(NEWSFEEDS_BUF + i, buff);
	i += strlen(buff);
	count++;
    }
    fclose(fp);
    if (NEWSFEEDS == NULL) {
	NEWSFEEDS = (newsfeeds_t *) mymalloc(sizeof(newsfeeds_t) * (count + 1));
	NEWSFEEDS_BYBOARD = (newsfeeds_t **) mymalloc(sizeof(newsfeeds_t *) * (count + 1));
    } else {
	NEWSFEEDS = (newsfeeds_t *) myrealloc(NEWSFEEDS, sizeof(newsfeeds_t) * (count + 1));
	NEWSFEEDS_BYBOARD = (newsfeeds_t **) myrealloc(NEWSFEEDS_BYBOARD, sizeof(newsfeeds_t *) * (count + 1));
    }
    NFCOUNT = 0;
    for (ptr = NEWSFEEDS_BUF; (newsfeedsptr = (char *)strchr(ptr, '\n')) != NULL; ptr = newsfeedsptr + 1, NFCOUNT++) {
	char           *nptr;
	*newsfeedsptr = '\0';
	NEWSFEEDS[NFCOUNT].newsgroups = "";
	NEWSFEEDS[NFCOUNT].board = "";
	NEWSFEEDS[NFCOUNT].path = NULL;
	NEWSFEEDS_BYBOARD[NFCOUNT] = NEWSFEEDS + NFCOUNT;
	for (nptr = ptr; *nptr && isspace(*nptr);)
	    nptr++;
	if (*nptr == '\0')
	    continue;
	NEWSFEEDS[NFCOUNT].newsgroups = nptr;
	for (nptr++; *nptr && !isspace(*nptr);)
	    nptr++;
	if (*nptr == '\0')
	    continue;
	*nptr = '\0';
	for (nptr++; *nptr && isspace(*nptr);)
	    nptr++;
	if (*nptr == '\0')
	    continue;
	NEWSFEEDS[NFCOUNT].board = nptr;
	for (nptr++; *nptr && !isspace(*nptr);)
	    nptr++;
	if (*nptr == '\0')
	    continue;
	*nptr = '\0';
	for (nptr++; *nptr && isspace(*nptr);)
	    nptr++;
	if (*nptr == '\0')
	    continue;
	NEWSFEEDS[NFCOUNT].path = nptr;
	for (nptr++; *nptr && !strchr("\r\n", *nptr);)
	    nptr++;
	*nptr = '\0';
    }
    qsort(NEWSFEEDS, NFCOUNT, sizeof(newsfeeds_t), nfcmp);
    qsort(NEWSFEEDS_BYBOARD, NFCOUNT, sizeof(newsfeeds_t *), nf_byboardcmp);
    return 0;
}

newsfeeds_t    *
search_board(board)
    char           *board;
{
    newsfeeds_t     nft, *nftptr, **find;
    if (NONENEWSFEEDS)
	return NULL;
    nft.board = board;
    nftptr = &nft;
    find = (newsfeeds_t **) bsearch((char *)&nftptr, NEWSFEEDS_BYBOARD, NFCOUNT, sizeof(newsfeeds_t *), nf_byboardcmp);
    if (find != NULL)
	return *find;
    return NULL;
}

nodelist_t     *
search_nodelist_bynode(node)
    char           *node;
{
    nodelist_t      nlt, *nltptr, **find;
    if (LOCALNODELIST)
	return NULL;
    nlt.node = node;
    nltptr = &nlt;
    find = (nodelist_t **) bsearch((char *)&nltptr, NODELIST_BYNODE, NLCOUNT, sizeof(nodelist_t *), nl_bynodecmp);
    if (find != NULL)
	return *find;
    return NULL;
}


nodelist_t     *
search_nodelist(site, identuser)
    char           *site;
    char           *identuser;
{
    nodelist_t      nlt, *find;
    char            buffer[1024];
    if (LOCALNODELIST)
	return NULL;
    nlt.host = site;
    find = (nodelist_t *) bsearch((char *)&nlt, NODELIST, NLCOUNT, sizeof(nodelist_t), nlcmp);
    if (find == NULL && identuser != NULL) {
	sprintf(buffer, "%s@%s", identuser, site);
	nlt.host = buffer;
	find = (nodelist_t *) bsearch((char *)&nlt, NODELIST, NLCOUNT, sizeof(nodelist_t), nlcmp);
    }
    return find;
}

newsfeeds_t    *
search_group(newsgroup)
    char           *newsgroup;
{
    newsfeeds_t     nft, *find;
    if (NONENEWSFEEDS)
	return NULL;
    nft.newsgroups = newsgroup;
    find = (newsfeeds_t *) bsearch((char *)&nft, NEWSFEEDS, NFCOUNT, sizeof(newsfeeds_t), nfcmp);
    return find;
}

char           *
ascii_date(now)
    time_t          now;
{
    static char     datebuf[40];
    /*
     * time_t now; time(&now);
     */
    strftime(datebuf, sizeof(datebuf), "%d %b %Y %X " INNTIMEZONE, gmtime(&now));
    return datebuf;
}

char           *
restrdup(ptr, string)
    char           *ptr;
    char           *string;
{
    int             len;
    if (string == NULL) {
	if (ptr != NULL)
	    *ptr = '\0';
	return ptr;
    }
    len = strlen(string) + 1;
    if (ptr != NULL) {
	ptr = (char *)myrealloc(ptr, len);
    } else
	ptr = (char *)mymalloc(len);
    strcpy(ptr, string);
    return ptr;
}



void           *
mymalloc(size)
    int             size;
{
    char           *ptr = (char *)malloc(size);
    if (ptr == NULL) {
	fprintf(stderr, "cant allocate memory\n");
	syslog(LOG_ERR, "cant allocate memory %m");
	exit(1);
    }
    return ptr;
}

void           *
myrealloc(optr, size)
    void           *optr;
    int             size;
{
    char           *ptr = (char *)realloc(optr, size);
    if (ptr == NULL) {
	fprintf(stderr, "cant allocate memory\n");
	syslog(LOG_ERR, "cant allocate memory %m");
	exit(1);
    }
    return ptr;
}

void
testandmkdir(dir)
    char           *dir;
{
    if (!isdir(dir)) {
	char            path[MAXPATHLEN + 12];
	sprintf(path, "mkdir -p %s", dir);
	system(path);
    }
}

static char     splitbuf[2048];
static char     joinbuf[1024];
#define MAXTOK 50
static char    *Splitptr[MAXTOK];
char          **
split(line, pat)
    char           *line, *pat;
{
    char           *p;
    int             i;

    for (i = 0; i < MAXTOK; ++i)
	Splitptr[i] = NULL;
    strncpy(splitbuf, line, sizeof splitbuf - 1);
    /* printf("%d %d\n",strlen(line),strlen(splitbuf)); */
    splitbuf[sizeof splitbuf - 1] = '\0';
    for (i = 0, p = splitbuf; *p && i < MAXTOK - 1;) {
	for (Splitptr[i++] = p; *p && !strchr(pat, *p); p++);
	if (*p == '\0')
	    break;
	for (*p++ = '\0'; *p && strchr(pat, *p); p++);
    }
    return Splitptr;
}

char          **
BNGsplit(line)
    char           *line;
{
    char          **ptr = split(line, ",");
    newsfeeds_t    *nf1, *nf2;
    char           *n11, *n12, *n21, *n22;
    int             i, j;
    for (i = 0; ptr[i] != NULL; i++) {
	nf1 = (newsfeeds_t *) search_group(ptr[i]);
	for (j = i + 1; ptr[j] != NULL; j++) {
	    if (strcmp(ptr[i], ptr[j]) == 0) {
		*ptr[j] = '\0';
		continue;
	    }
	    nf2 = (newsfeeds_t *) search_group(ptr[j]);
	    if (nf1 && nf2) {
		if (strcmp(nf1->board, nf2->board) == 0) {
		    *ptr[j] = '\0';
		    continue;
		}
		for (n11 = nf1->board, n12 = (char *)strchr(n11, ',');
		     n11 && *n11; n12 = (char *)strchr(n11, ',')) {
		    if (n12)
			*n12 = '\0';
		    for (n21 = nf2->board, n22 = (char *)strchr(n21, ',');
			 n21 && *n21; n22 = (char *)strchr(n21, ',')) {
			if (n22)
			    *n22 = '\0';
			if (strcmp(n11, n21) == 0) {
			    *n21 = '\t';
			}
			if (n22) {
			    *n22 = ',';
			    n21 = n22 + 1;
			} else
			    break;
		    }
		    if (n12) {
			*n12 = ',';
			n11 = n12 + 1;
		    } else
			break;
		}
	    }
	}
    }
    return ptr;
}

char          **
ssplit(line, pat)
    char           *line, *pat;
{
    char           *p;
    int             i;
    for (i = 0; i < MAXTOK; ++i)
	Splitptr[i] = NULL;
    strncpy(splitbuf, line, 1024);
    for (i = 0, p = splitbuf; *p && i < MAXTOK;) {
	for (Splitptr[i++] = p; *p && !strchr(pat, *p); p++);
	if (*p == '\0')
	    break;
	*p = 0;
	p++;
	/* for (*p='\0'; strchr(pat,*p);p++); */
    }
    return Splitptr;
}

char           *
join(lineptr, pat, num)
    char          **lineptr, *pat;
    int             num;
{
    int             i;
    joinbuf[0] = '\0';
    if (lineptr[0] != NULL)
	strncpy(joinbuf, lineptr[0], 1024);
    else {
	joinbuf[0] = '\0';
	return joinbuf;
    }
    for (i = 1; i < num; i++) {
	strcat(joinbuf, pat);
	if (lineptr[i] != NULL)
	    strcat(joinbuf, lineptr[i]);
	else
	    break;
    }
    return joinbuf;
}

#ifdef BBSLIB
main()
{
    initial_bbs("feed");
    printf("%s\n", ascii_date());
}
#endif
