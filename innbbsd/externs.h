#ifndef EXTERNS_H
#define EXTERNS_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "bbslib.h"
#include "nocem.h"
#include "dbz.h"
#include "daemon.h"
#include "his.h"
#include "bbs.h"

/* file.c */
char *fileglue (char *, ...);

/* pmain.c */
int pmain(char *port);
int p_unix_main(char *);

/* bbslib.c / echobbslib.c */
char *ascii_date();
char **split (char *, char *);
nodelist_t *search_nodelist_bynode(char *node);
newsfeeds_t *search_board(char *board);
int readnffile(char *);
int readnlfile(char *, char *);
void verboselog(char *fmt, ...);
void verboseon(char *);
int isverboselog(void);
void setverboseoff(void);
void setverboseon(void);
void testandmkdir(char *);
char **BNGsplit(char *);

/* rfc931.c */
char *my_rfc931_name(int, struct sockaddr_in *);

/* strdecode.c */
int isreturn(unsigned char);
void str_decode_M3(unsigned char *str);

/* inntobbs.c */
int headervalue(char *);
void init_echomailfp(void);
void init_bbsfeedsfp(void);
void bbsfeedslog(char *, int);
void readlines(ClientType *);

/* connectsock.c */
int open_listen(char *, char *, int (*) (int));
int open_unix_listen(char *, char *, int (*) (int));
int unixclient(char *, char *);
void docompletehalt(int);
int inetclient(char *, char *, char *);
int tryaccept(int);
void startfrominetd(int);
void sethaltfunction(int (*)(int));

/* innbbsd.c */
int INNBBSDshutdown(void);
void installinnbbsd(void);

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

/* daemon.c */
int argify(char *, char ***);
void deargify (char ***);
daemoncmd_t *searchcmd(char *);

/* receive_article.c */
int cancel_article_front(char *);
int receive_control(void);

/* nocem.c */
ncmperm_t *search_issuer(char *);
int receive_nocem(void);

/* closeonexec.c */
void closeOnExec(int, int);

/* dbz.c */
int dbzwritethrough(int);

/* dbztool.c */
char *DBfetch(char *);
int storeDB(char *, char *);

/* inndchannel.c */
int innbbsdstartup(void);
void clearfdset(int);
void channeldestroy(ClientType *);
void feedfplog(newsfeeds_t *, char *, int);

#endif
