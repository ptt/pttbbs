#ifndef BBSLIB_H
#define BBSLIB_H
#include <stdio.h> /* for FILE */
typedef struct nodelist_t {
    char           *node;
    char           *exclusion;
    char           *host;
    char           *protocol;
    char           *comments;
    int             feedtype;
    FILE           *feedfp;
}               nodelist_t;

typedef struct newsfeeds_t {
    char           *newsgroups;
    char           *board;
    char           *path;
}               newsfeeds_t;

typedef struct overview_t {
    char           *board, *filename, *group;
    time_t          mtime;
    char           *from, *subject;
}               overview_t;

extern char     MYBBSID[];
extern char     ECHOMAIL[];
extern char     BBSFEEDS[];
extern char     LOCALDAEMON[];
extern char     INNDHOME[];
extern char     HISTORY[];
extern char     LOGFILE[];
extern char     INNBBSCONF[];
extern nodelist_t *NODELIST;
extern nodelist_t **NODELIST_BYNODE;
extern newsfeeds_t *NEWSFEEDS, **NEWSFEEDS_BYBOARD;
extern int      NFCOUNT, NLCOUNT;
extern int      Expiredays, His_Maint_Min, His_Maint_Hour;
extern int      LOCALNODELIST, NONENEWSFEEDS;
extern int      Maxclient;

#ifndef ARG
#ifdef __STDC__
#define ARG(x) x
#else
#define ARG(x) ()
#endif
#endif

int initial_bbs ARG((char *));
char           *restrdup ARG((char *, char *));
nodelist_t     *search_nodelist ARG((char *, char *));
newsfeeds_t    *search_group ARG((char *));
void            bbslog(char *fmt,...);
void           *mymalloc ARG((int));
void           *myrealloc ARG((void *, int));

#ifdef PalmBBS
#define bbslog xbbslog
#endif

#endif
