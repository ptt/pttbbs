/* $Id$ */
#include "bbs.h"

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

void
passwd_force_update(int flag)
{
    if(!currutmp || (currutmp->alerts & ALERT_PWD) == 0)
	return;
    currutmp->alerts &= ~flag;
}

int
passwd_update(int num, userec_t * buf)
{
    int  pwdfd;
    if (num < 1 || num > MAX_USERS)
	return -1;
    buf->money = moneyof(num);
    if(usernum == num && currutmp && ((pwdfd = currutmp->alerts)  & ALERT_PWD))
    {
	userec_t u;
	passwd_query(num, &u);
	if(pwdfd & ALERT_PWD_BADPOST)
	   cuser.badpost = buf->badpost = u.badpost;
	if(pwdfd & ALERT_PWD_GOODPOST)
	   cuser.goodpost = buf->goodpost = u.goodpost;
        if(pwdfd & ALERT_PWD_PERM)	
	   cuser.userlevel = buf->userlevel = u.userlevel;
        if(pwdfd & ALERT_PWD_JUSTIFY)	
	{
	    memcpy(buf->justify,  u.justify, sizeof(u.justify));
	    memcpy(cuser.justify, u.justify, sizeof(u.justify));
	    memcpy(buf->email,  u.email, sizeof(u.email));
	    memcpy(cuser.email, u.email, sizeof(u.email));
	}
	cuser.numposts += u.numposts - latest_numposts;
	currutmp->alerts &= ~ALERT_PWD;

	// ALERT_PWD_RELOAD: reload all! No need to write.
	if (pwdfd & ALERT_PWD_RELOAD)
	{
	    memcpy(&cuser, &u, sizeof(u));
	    return 0;
	}
    }
    if ((pwdfd = open(fn_passwd, O_WRONLY)) < 0)
	exit(1);
    lseek(pwdfd, sizeof(userec_t) * (num - 1), SEEK_SET);
    write(pwdfd, buf, sizeof(userec_t));
    close(pwdfd);

#ifndef _BBS_UTIL_C_
    if (latest_numposts != cuser.numposts) {
	sendalert_uid(usernum, ALERT_PWD_POSTS);
	latest_numposts = cuser.numposts;
    }
#endif
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

    if (buf == &cuser)
	latest_numposts = cuser.numposts;

    return 0;
}

int initcuser(const char *userid)
{
    // Ptt: setup cuser and usernum here
   if(userid[0]=='\0' ||
   !(usernum = searchuser(userid, NULL)) || usernum > MAX_USERS)
      return -1;
   passwd_query(usernum, &cuser);
   return usernum;
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

