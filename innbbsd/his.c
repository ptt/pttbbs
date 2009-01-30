/*
 * $Revision: 1.1 $ *
 * 
 *  History file routines.
 */
#include "bbs.h"
#include <stdlib.h>
#include "innbbsconf.h"
#include "bbslib.h"
#include "his.h"
#include "externs.h"

#define STATIC static
/* STATIC char	HIShistpath[] = _PATH_HISTORY; */
STATIC FILE    *HISwritefp;
STATIC int      HISreadfd;
STATIC int      HISdirty;
STATIC int      HISincore = XINDEX_DBZINCORE;
STATIC char    *LogName = "xindexchan";

#ifndef EXPIREDAYS
#define EXPIREDAYS 4
#endif

#ifndef DEFAULT_HIST_SIZE
#define DEFAULT_HIST_SIZE 100000
#endif

void
hisincore(flag)
    int             flag;
{
    HISincore = flag;
}

void
makedbz(histpath, entry)
    char           *histpath;
    long            entry;
{
    long            size;
    size = dbzsize(entry);
    dbzfresh(histpath, size, '\t', 0, 1);
    dbmclose();
}

void            HISsetup();
void            HISclose();

void
mkhistory(srchist)
    char           *srchist;
{
    FILE           *hismaint;
    char            maintbuff[256];
    char           *ptr;
    hismaint = fopen(srchist, "r");
    if (hismaint == NULL) {
	return;
    } {
	char            newhistpath[1024];
	char            newhistdirpath[1024];
	char            newhistpagpath[1024];
	sprintf(newhistpath, "%s.n", srchist);
	sprintf(newhistdirpath, "%s.n.dir", srchist);
	sprintf(newhistpagpath, "%s.n.pag", srchist);
	if (!dashf(newhistdirpath) || !dashf(newhistpagpath)) {
	    makedbz(newhistpath, DEFAULT_HIST_SIZE);
	}
	myHISsetup(newhistpath);
	while (fgets(maintbuff, sizeof(maintbuff), hismaint) != NULL) {
	    datum           key;
	    ptr = (char *)strchr(maintbuff, '\t');
	    if (ptr != NULL) {
		*ptr = '\0';
		ptr++;
	    }
	    key.dptr = maintbuff;
	    key.dsize = strlen(maintbuff);
	    myHISwrite(&key, ptr);
	}
	(void)HISclose();
	/*
	 * rename(newhistpath, srchist); 	rename(newhistdirpath,
	 * fileglue("%s.dir", srchist)); rename(newhistpagpath,
	 * fileglue("%s.pag", srchist));
	 */
    }
    fclose(hismaint);
}

time_t
gethisinfo(void)
{
    FILE           *hismaint;
    time_t          lasthist;
    char            maintbuff[4096];
    char           *ptr;
    hismaint = fopen(HISTORY, "r");
    if (hismaint == NULL) {
	return 0;
    }
    fgets(maintbuff, sizeof(maintbuff), hismaint);
    fclose(hismaint);
    ptr = (char *)strchr(maintbuff, '\t');
    if (ptr != NULL) {
	ptr++;
	lasthist = atol(ptr);
	return lasthist;
    }
    return 0;
}

void
HISmaint(void)
{
    FILE           *hismaint;
    time_t          lasthist, now;
    char            maintbuff[4096];
    char           *ptr;

    if (!dashf(HISTORY)) {
	makedbz(HISTORY, DEFAULT_HIST_SIZE);
    }
    hismaint = fopen(HISTORY, "r");
    if (hismaint == NULL) {
	return;
    }
    fgets(maintbuff, sizeof(maintbuff), hismaint);
    ptr = (char *)strchr(maintbuff, '\t');
    if (ptr != NULL) {
	ptr++;
	lasthist = atol(ptr);
	time(&now);
	if (lasthist + 86400 * Expiredays * 2 < now) {
	    char            newhistpath[1024];
	    char            newhistdirpath[1024];
	    char            newhistpagpath[1024];
	    (void)HISclose();
	    sprintf(newhistpath, "%s.n", HISTORY);
	    sprintf(newhistdirpath, "%s.n.dir", HISTORY);
	    sprintf(newhistpagpath, "%s.n.pag", HISTORY);
	    if (!dashf(newhistdirpath)) {
		makedbz(newhistpath, DEFAULT_HIST_SIZE);
	    }
	    myHISsetup(newhistpath);
	    while (fgets(maintbuff, sizeof(maintbuff), hismaint) != NULL) {
		datum           key;
		ptr = (char *)strchr(maintbuff, '\t');
		if (ptr != NULL) {
		    *ptr = '\0';
		    ptr++;
		    lasthist = atol(ptr);
		} else {
		    continue;
		}
		if (lasthist + 99600 * Expiredays < now)
		    continue;
		key.dptr = maintbuff;
		key.dsize = strlen(maintbuff);
		myHISwrite(&key, ptr);
	    }
	    (void)HISclose();
	    rename(HISTORY, (char *)fileglue("%s.o", HISTORY));
	    rename(newhistpath, HISTORY);
	    rename(newhistdirpath, (char *)fileglue("%s.dir", HISTORY));
	    rename(newhistpagpath, (char *)fileglue("%s.pag", HISTORY));
	    (void)HISsetup();
	}
    }
    fclose(hismaint);
}


/*
 * *  Set up the history files.
 */
void
HISsetup(void)
{
    myHISsetup(HISTORY);
}

int
myHISsetup(histpath)
    char           *histpath;
{
    if (HISwritefp == NULL) {
	/* Open the history file for appending formatted I/O. */
	if ((HISwritefp = fopen(histpath, "a")) == NULL) {
	    syslog(LOG_CRIT, "%s cant fopen %s %m", LogName, histpath);
	    exit(1);
	}
	CloseOnExec((int)fileno(HISwritefp), TRUE);

	/* Open the history file for reading. */
	if ((HISreadfd = open(histpath, O_RDONLY)) < 0) {
	    syslog(LOG_CRIT, "%s cant open %s %m", LogName, histpath);
	    exit(1);
	}
	CloseOnExec(HISreadfd, TRUE);

	/* Open the DBZ file. */
	/* (void)dbzincore(HISincore); */
	(void)dbzincore(HISincore);
	(void)dbzwritethrough(1);
	if (dbminit(histpath) < 0) {
	    syslog(LOG_CRIT, "%s cant dbminit %s %m", histpath, LogName);
	    exit(1);
	}
    }
    return 0;
}


/*
 * *  Synchronize the in-core history file (flush it).
 */
void
HISsync()
{
    if (HISdirty) {
	if (dbzsync()) {
	    syslog(LOG_CRIT, "%s cant dbzsync %m", LogName);
	    exit(1);
	}
	HISdirty = 0;
    }
}


/*
 * *  Close the history files.
 */
void
HISclose(void)
{
    if (HISwritefp != NULL) {
	/*
	 * Since dbmclose calls dbzsync we could replace this line with
	 * "HISdirty = 0;".  Oh well, it keeps the abstraction clean.
	 */
	HISsync();
	if (dbmclose() < 0)
	    syslog(LOG_ERR, "%s cant dbmclose %m", LogName);
	if (fclose(HISwritefp) == EOF)
	    syslog(LOG_ERR, "%s cant fclose history %m", LogName);
	HISwritefp = NULL;
	if (close(HISreadfd) < 0)
	    syslog(LOG_ERR, "%s cant close history %m", LogName);
	HISreadfd = -1;
    }
}


#ifdef HISset
/*
 * *  File in the DBZ datum for a Message-ID, making sure not to copy any *
 * illegal characters.
 */
STATIC void
HISsetkey(p, keyp)
    register char  *p;
    datum          *keyp;
{
    static BUFFER   MessageID;
    register char  *dest;
    register int    i;

    /* Get space to hold the ID. */
    i = strlen(p);
    if (MessageID.Data == NULL) {
	MessageID.Data = NEW(char, i + 1);
	MessageID.Size = i;
    } else if (MessageID.Size < i) {
	RENEW(MessageID.Data, char, i + 1);
	MessageID.Size = i;
    }
    for (keyp->dptr = dest = MessageID.Data; *p; p++)
	if (*p == HIS_FIELDSEP || *p == '\n')
	    *dest++ = HIS_BADCHAR;
	else
	    *dest++ = *p;
    *dest = '\0';

    keyp->dsize = dest - MessageID.Data + 1;
}

#endif
/*
 * *  Get the list of files under which a Message-ID is stored.
 */
char           *
HISfilesfor(key, output)
    datum          *key;
    datum          *output;
{
    char           *dest;
    datum           val;
    long            offset;
    register char  *p;
    register int    i;
    int             Used;

    /* Get the seek value into the history file. */
    val = dbzfetch(*key);
    if (val.dptr == NULL || val.dsize != sizeof offset) {
	/* printf("fail here val.dptr %d\n",val.dptr); */
	return NULL;
    }
    /* Get space. */
    if (output->dptr == NULL) {
	printf("fail here output->dptr null\n");
	return NULL;
    }
    /* Copy the value to an aligned spot. */
    for (p = val.dptr, dest = (char *)&offset, i = sizeof offset; --i >= 0;)
	*dest++ = *p++;
    if (lseek(HISreadfd, offset, SEEK_SET) == -1) {
	printf("fail here lseek %d\n", offset);
	return NULL;
    }
    /* Read the text until \n or EOF. */
    for (output->dsize = 0, Used = 0;;) {
	i = read(HISreadfd,
		 &output->dptr[output->dsize], LEN - 1);
	if (i <= 0) {
	    printf("fail here i %d\n", i);
	    return NULL;
	}
	Used += i;
	output->dptr[Used] = '\0';
	if ((p = (char *)strchr(output->dptr, '\n')) != NULL) {
	    *p = '\0';
	    break;
	}
    }

    /* Move past the first two fields -- Message-ID and date info. */
    if ((p = (char *)strchr(output->dptr, HIS_FIELDSEP)) == NULL) {
	printf("fail here no HIS_FILE\n");
	return NULL;
    }
    return p + 1;
    /*
     * if ((p = (char*)strchr(p + 1, HIS_FIELDSEP)) == NULL) return NULL;
     */

    /* Translate newsgroup separators to slashes, return the fieldstart. */
}

/*
 * *  Have we already seen an article?
 */
#ifdef HISh
BOOL
HIShavearticle(MessageID)
    char           *MessageID;
{
    datum           key;
    datum           val;

    HISsetkey(MessageID, &key);
    val = dbzfetch(key);
    return val.dptr != NULL;
}
#endif


/*
 * *  Turn a history filename entry from slashes to dots.  It's a pity *  we
 * have to do this.
 */
STATIC void
HISslashify(p)
    register char  *p;
{
    register char  *last;

    for (last = NULL; *p; p++) {
	if (*p == '/') {
	    *p = '.';
	    last = p;
	} else if (*p == ' ' && last != NULL)
	    *last = '/';
    }
    if (last)
	*last = '/';
}


void
IOError(error)
    char           *error;
{
    fprintf(stderr, "%s\n", error);
}

/* BOOL */
int
myHISwrite(key, remain)
    datum          *key;
    char           *remain;
{
    long            offset;
    datum           val;
    int             i;

    val = dbzfetch(*key);
    if (val.dptr != NULL) {
	return FALSE;
    }
    flock(fileno(HISwritefp), LOCK_EX);
    offset = ftell(HISwritefp);
    i = fprintf(HISwritefp, "%s%c%s",
		key->dptr, HIS_FIELDSEP, remain);
    if (i == EOF || fflush(HISwritefp) == EOF) {
	/* The history line is now an orphan... */
	IOError("history");
	syslog(LOG_ERR, "%s cant write history %m", LogName);
	flock(fileno(HISwritefp), LOCK_UN);
	return FALSE;
    }
    /* Set up the database values and write them. */
    val.dptr = (char *)&offset;
    val.dsize = sizeof offset;
    if (dbzstore(*key, val) < 0) {
	IOError("my history database");
	syslog(LOG_ERR, "%s cant dbzstore %m", LogName);
	flock(fileno(HISwritefp), LOCK_UN);
	return FALSE;
    }
    if (++HISdirty >= ICD_SYNC_COUNT)
	HISsync();
    flock(fileno(HISwritefp), LOCK_UN);
    return TRUE;
}


/*
 * *  Write a history entry.
 */
BOOL
HISwrite(key, date, paths)
    datum          *key;
    long            date;
    char           *paths;
{
    long            offset;
    datum           val;
    int             i;

    flock(fileno(HISwritefp), LOCK_EX);
    offset = ftell(HISwritefp);
    i = fprintf(HISwritefp, "%s%c%ld%c%s\n",
		key->dptr, HIS_FIELDSEP, (long)date, HIS_FIELDSEP,
		paths);
    if (i == EOF || fflush(HISwritefp) == EOF) {
	/* The history line is now an orphan... */
	IOError("history");
	syslog(LOG_ERR, "%s cant write history %m", LogName);
	flock(fileno(HISwritefp), LOCK_UN);
	return FALSE;
    }
    /* Set up the database values and write them. */
    val.dptr = (char *)&offset;
    val.dsize = sizeof offset;
    if (dbzstore(*key, val) < 0) {
	IOError("history database");
	syslog(LOG_ERR, "%s cant dbzstore %m", LogName);
	flock(fileno(HISwritefp), LOCK_UN);
	return FALSE;
    }
    if (++HISdirty >= ICD_SYNC_COUNT)
	HISsync();
    flock(fileno(HISwritefp), LOCK_UN);
    return TRUE;
}
