#ifndef HIS_H
#define HIS_H
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/param.h>
#ifndef SEEK_SET
#include <unistd.h>
#endif
#include "dbz.h"

#ifndef XINDEXDIR
# define XINDEXDIR "/homec/xindex"
#endif
#ifndef _PATH_HISTORY
# define _PATH_HISTORY "/u/staff/bbsroot/csie_util/bntpd/history"
#endif

#ifndef _PATH_COVERVIEW
# define _PATH_COVERVIEW ".coverview"
#endif

#ifndef _PATH_COVERVIEWDIR
# define _PATH_COVERVIEWDIR "/homec/xindex"
#endif

#ifndef XINDEX_DBZINCORE
# define XINDEX_DBZINCORE 1
#endif
#ifndef XINDEXNAME
# define XINDEXNAME ".index"
#endif
#ifndef XINDEXDBM
# define XINDEXDBM ".dbm"
#endif
#ifndef XINDEXINFO
# define XINDEXINFO ".info"
#endif

#define LEN 1024
struct t_article {
	long artnum;
	char subject[LEN];          /* Subject: line from mail header */
	char from[LEN];                     /* From: line from mail header (address)
 */
        char name[LEN];                     /* From: line from mail header (full nam
e) */
        long date;                      /* Date: line from header in seconds */
        char xref[LEN];                     /* Xref: cross posted article reference
line */
        int lines;                      /* Lines: number of lines in article */
        char *archive;          /* Archive-name: line from mail header */
        char *part;                     /* part  no. of archive */
        char *patch;            /* patch no. of archive */
};

typedef struct t_article art_t;

#define HIS_BADCHAR		'_'
#define HIS_FIELDSEP		'\t'
#define HIS_NOEXP		"-"
#define HIS_SUBFIELDSEP		'~'
/*#define HIS_FIELDSEP2		'\034'*/
#define HIS_FIELDSEP2		'I'

#ifndef TRUE
# define TRUE 1
# define FALSE 0
#endif

#ifndef BOOL
typedef unsigned char BOOL;
#endif

#ifndef ICD_SYNC_COUNT
# define ICD_SYNC_COUNT 1
#endif

#endif
