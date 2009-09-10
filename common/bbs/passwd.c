/* $Id$ */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include "common.h"
#include "var.h"
#include "cmbbs.h"

//////////////////////////////////////////////////////////////////////////
// This is shared by utility library and core BBS,
// so do not put code using currutmp/cuser here.
//////////////////////////////////////////////////////////////////////////

// these cannot be used!
#define currutmp  YOU_FAILED
#define usernum	  YOU_FAILED
#undef  cuser
#undef  cuser_ref
#define cuser     YOU_FAILED
#define cuser_ref YOU_FAILED
#define abort_bbs YOU_FAILED
#define log_usies YOU_FAILED

static int      semid = -1;

#ifndef SEM_R
#define SEM_R 0400
#endif

#ifndef SEM_A
#define SEM_A 0200
#endif

#ifndef __FreeBSD__
#include <sys/sem.h>
union semun {
    int             val;	/* value for SETVAL */
    struct semid_ds *buf;	/* buffer for IPC_STAT & IPC_SET */
    unsigned short  *array;	/* array for GETALL & SETALL */
    struct seminfo *__buf;	/* buffer for IPC_INFO */
};
#endif

// semaphore based PASSWD locking

int
passwd_init(void)
{
    semid = semget(PASSWDSEM_KEY, 1, SEM_R | SEM_A | IPC_CREAT | IPC_EXCL);
    if (semid == -1) {
	if (errno == EEXIST) {
	    semid = semget(PASSWDSEM_KEY, 1, SEM_R | SEM_A);
	    if (semid == -1) {
		perror("semget");
		exit(1);
	    }
	} else {
	    perror("semget");
	    exit(1);
	}
    } else {
	union semun     s;

	s.val = 1;
	if (semctl(semid, 0, SETVAL, s) == -1) {
	    perror("semctl");
	    exit(1);
	}
    }

    return 0;
}

void
passwd_lock(void)
{
    struct sembuf   buf = {0, -1, SEM_UNDO};

    if (semop(semid, &buf, 1)) {
	perror("semop");
	exit(1);
    }
}

void
passwd_unlock(void)
{
    struct sembuf   buf = {0, 1, SEM_UNDO};

    if (semop(semid, &buf, 1)) {
	perror("semop");
	exit(1);
    }
}

// updateing passwd/userec_t

int
passwd_update_money(int num)
/* update money only 
   Ptt: don't call it directly, call deumoney() */
{
    int  pwdfd;
    int  money=moneyof(num);
    userec_t u;
    if (num < 1 || num > MAX_USERS)
        return -1;

    if ((pwdfd = open(fn_passwd, O_WRONLY)) < 0)
        exit(1);
    lseek(pwdfd, sizeof(userec_t) * (num - 1) +
	  ((char *)&u.money - (char *)&u), SEEK_SET);
    write(pwdfd, &money, sizeof(int));
    close(pwdfd);
    return 0;
}

int
passwd_update(int num, userec_t * buf)
{
    int  pwdfd;
    if (num < 1 || num > MAX_USERS)
	return -1;

    if ((pwdfd = open(fn_passwd, O_WRONLY)) < 0)
	exit(1);
    lseek(pwdfd, sizeof(userec_t) * (num - 1), SEEK_SET);
    write(pwdfd, buf, sizeof(userec_t));
    close(pwdfd);

    return 0;
}

int
passwd_query(int num, userec_t * buf)
{
    int             pwdfd;
    if (num < 1 || num > MAX_USERS)
	return -1;
    if ((pwdfd = open(fn_passwd, O_RDONLY)) < 0)
	exit(1);
    lseek(pwdfd, sizeof(userec_t) * (num - 1), SEEK_SET);
    read(pwdfd, buf, sizeof(userec_t));
    close(pwdfd);

    return 0;
}

int 
passwd_load_user(const char *userid, userec_t *buf)
{
    int unum = 0;

   if( !userid ||
       !userid[0] ||
       !(unum = searchuser(userid, NULL)) || 
       unum > MAX_USERS)
      return -1;

   if (passwd_query(unum, buf) != 0)
       return -1;

   return unum;
}

int
passwd_apply(void *ctx, int (*fptr) (void *ctx, int, userec_t *))
{
    int             i;
    userec_t        user;
    for (i = 0; i < MAX_USERS; i++) {
	passwd_query(i + 1, &user);
	if ((*fptr) (ctx, i, &user) < 0)
	    return -1;
    }
    return 0;
}

// XXX NOTE: string in plain will be destroyed.
int
checkpasswd(const char *passwd, char *plain)
{
    int             ok;
    char           *pw;

    ok = 0;
    pw = fcrypt(plain, passwd);
    if(pw && strcmp(pw, passwd)==0)
	ok = 1;
    memset(plain, 0, strlen(plain));

    return ok;
}

char *
genpasswd(char *pw)
{
    if (pw[0]) {
	char            saltc[2], c;
	int             i;

	i = 9 * getpid();
	saltc[0] = i & 077;
	saltc[1] = (i >> 6) & 077;

	for (i = 0; i < 2; i++) {
	    c = saltc[i] + '.';
	    if (c > '9')
		c += 7;
	    if (c > 'Z')
		c += 6;
	    saltc[i] = c;
	}
	return fcrypt(pw, saltc);
    }
    return "";
}


void
logattempt(const char *uid, char type, time4_t now, const char *loghost)
{
    char fname[PATHLEN];
    int  fd, len;
    char genbuf[200];

    snprintf(genbuf, sizeof(genbuf), "%c%-12s[%s] ?@%s\n", type, uid,
	    Cdate(&now), loghost);
    len = strlen(genbuf);
    // log to public (BBSHOME)
    if ((fd = open(FN_BADLOGIN, O_WRONLY | O_CREAT | O_APPEND, 0644)) > 0) {
	write(fd, genbuf, len);
	close(fd);
    }
    // log to user private log
    if (type == '-') {
	snprintf(genbuf, sizeof(genbuf),
		 "[%s] %s\n", Cdate(&now), loghost);
	len = strlen(genbuf);
	sethomefile(fname, uid, FN_BADLOGIN);
	if ((fd = open(fname, O_WRONLY | O_CREAT | O_APPEND, 0644)) > 0) {
	    write(fd, genbuf, len);
	    close(fd);
	}
    }
}

