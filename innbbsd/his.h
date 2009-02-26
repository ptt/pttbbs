#ifndef HIS_H
#define HIS_H

#include <time.h>
#include <dbz.h>

#ifndef XINDEX_DBZINCORE
#define XINDEX_DBZINCORE 1
#endif

#define LEN 1024

#define HIS_FIELDSEP		'\t'

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#ifndef BOOL
typedef unsigned char BOOL;
#endif

#ifndef ICD_SYNC_COUNT
#define ICD_SYNC_COUNT 1
#endif

/* his.c */
void HISclose(void);
void HISmaint(void);
void mkhistory(char *);
int myHISsetup(char *);
char *HISfilesfor(datum *, datum *);
int myHISwrite(datum *, char *);
void hisincore(int);
void HISsetup(void);
BOOL HISwrite(datum *, long, char *);
void mkhistory(char *);
time_t gethisinfo(void);

/* dbztool.c */
char *DBfetch(char *);
int storeDB(char *, char *);

#endif
