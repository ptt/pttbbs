#ifndef EXTERNS_H
#define EXTERNS_H

#ifndef ARG
#ifdef __STDC__
#define ARG(x) x
#else
#define ARG(x) ()
#endif
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "bbslib.h"
#include "nocem.h"
#include "dbz.h"
#include "daemon.h"
#include "his.h"
#include "bbs.h"

char           *fileglue ARG((char *,...));
char           *ascii_date ARG(());
char          **split ARG((char *, char *));
char           *my_rfc931_name(int, struct sockaddr_in *);
int isreturn(unsigned char);
nodelist_t     *search_nodelist_bynode(char *node);
int isfile(char *);
void str_decode_M3(unsigned char *str);
int headervalue(char *);
int open_listen(char *, char *, int (*) ARG((int)));
int open_unix_listen(char *, char *, int (*) ARG((int)));
int unixclient(char *, char *);
int pmain(char *port);
void docompletehalt(int);
int p_unix_main(char *);
int INNBBSDshutdown(void);
void HISclose(void);
void HISmaint(void);
newsfeeds_t    *search_board(char *board);
long filesize(char *);
int inetclient(char *, char *, char *);
int iszerofile(char *);
void init_echomailfp(void);
void init_bbsfeedsfp(void);
int isdir(char *);
int readnffile(char *);
int readnlfile(char *, char *);
int tryaccept(int);
void verboselog(char *fmt,...);
int argify(char *, char ***);
void deargify   ARG((char ***));
void mkhistory(char *);
int cancel_article_front(char *);
ncmperm_t *search_issuer(char *);
int myHISsetup(char *);
void closeOnExec(int, int);
int dbzwritethrough(int);
char *HISfilesfor(datum *, datum *);
int myHISwrite(datum *, char *);
void CloseOnExec(int, int);
void verboseon(char *);
daemoncmd_t    *searchcmd(char *);
void hisincore(int);
void startfrominetd(int);
void HISsetup(void);
void installinnbbsd(void);
void sethaltfunction(int (*) (int));
int innbbsdstartup(void);
int isverboselog(void);
time_t gethisinfo(void);
void setverboseoff(void);
void setverboseon(void);
char *DBfetch(char *);
int storeDB(char *, char *);
void readlines(ClientType *);
int receive_control(void);
int receive_nocem(void);
void clearfdset(int);
void channeldestroy(ClientType *);
BOOL HISwrite(datum *, long, char *);
void mkhistory(char *);
void testandmkdir(char *);
void feedfplog(newsfeeds_t *, char *, int);
char **BNGsplit(char *);
void bbsfeedslog(char *, int);

#endif
