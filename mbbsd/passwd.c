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
union semun {
    int             val;	/* value for SETVAL */
    struct semid_ds *buf;	/* buffer for IPC_STAT & IPC_SET */
    u_short        *array;	/* array for GETALL & SETALL */
    struct seminfo *__buf;	/* buffer for IPC_INFO */
};
#endif

int
passwd_init()
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
passwd_update_money(int num) /* update money only */
{
   userec_t user;
   int pwdfd, money = moneyof(num);
   char path[256];

   if (num < 1 || num > MAX_USERS)
	return -1;
   if (num == usernum)
      {
        cuser->money = money;
        return 0; 
      } 

   sethomefile(path, getuserid(num), ".passwd");

   if ((pwdfd = open(path, O_WRONLY)) < 0)
       {
        if(passwd_index_query(num, &user)<0)  // tempory code, will be removed
               exit(1);
        user.money=money;
        passwd_update(num, &user);
        return 0;
       }

   if(lseek(pwdfd, (off_t)((int)&user.money - (int)&user), SEEK_SET) >= 0)
              write(pwdfd, &money, sizeof(int));

   close(pwdfd);
   return 0;
}

int
passwd_index_update(int num, userec_t * buf)
{
    int  pwdfd;
    if (num < 1 || num > MAX_USERS)
	return -1;
    buf->money = moneyof(num);
    if ((pwdfd = open(fn_passwd, O_WRONLY)) < 0)
	exit(1);
    lseek(pwdfd, sizeof(userec_t) * (num - 1), SEEK_SET);
    write(pwdfd, buf, sizeof(userec_t));
    close(pwdfd);
    return 0;
}

int
passwd_update(int num, userec_t * buf)
{
   int pwdfd;
   char path[256];

   if(!buf->userid[0]) return -1;

   sethomefile(path, buf->userid, ".passwd");
   buf->money = moneyof(num);
   if ((pwdfd = open(path, O_WRONLY|O_CREAT, 0600)) < 0)
           return -1;
   write(pwdfd, buf, sizeof(userec_t));
   close(pwdfd);
   return 0;
}

int
passwd_index_query(int num, userec_t * buf)
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

int initcuser(char *userid)
{
   int pwdfd;
   userec_t buf;
   char path[256];
    // Ptt: setup cuser and usernum here
   if(userid[0]=='\0') return -1;
   if(!(usernum = searchuser(userid)) || usernum > MAX_USERS) return -1;

   sethomefile(path, userid, ".passwd");
   if((pwdfd = open(path, O_RDWR)) < 0)
     {  
        if(passwd_index_query(usernum, &buf)<0)
               exit(1);
        passwd_update(usernum, &buf);
        if((pwdfd = open(path, O_RDWR)) < 0) exit(1);
     }
   cuser = (userec_t *) mmap(NULL, sizeof(userec_t), PROT_WRITE|PROT_READ, 
            MAP_NOSYNC | MAP_SHARED, pwdfd, 0);

   if(cuser == (userec_t *) -1) exit(1);
   close(pwdfd);
   return usernum;
}

int freecuser()
{
   return munmap(cuser, sizeof(userec_t));
}

int
passwd_query(int num, userec_t * buf)
{
   int pwdfd;
   char path[256], *userid;

   if (num < 1 || num > MAX_USERS)
        return -1;
   userid = getuserid(num); 

   if(userid[0]=='\0') return -1;

   sethomefile(path, userid, ".passwd");
   if((pwdfd = open(path, O_RDONLY)) < 0)
     {  // copy from index // tempory code, will be removed
        if(passwd_index_query(num, buf)<0)
               exit(1);
        passwd_update(num, buf);
        return 0;
     }
   read(pwdfd, buf, sizeof(userec_t));
   close(pwdfd);
   return 0;
}

int
passwd_apply(int (*fptr) (int, userec_t *))
{
    int             i;
    userec_t        user;
    for (i = 0; i < MAX_USERS; i++) {
	passwd_query(i + 1, &user);
	if ((*fptr) (i, &user) == QUIT)
	    return QUIT;
    }
    return 0;
}

void
passwd_lock()
{
    struct sembuf   buf = {0, -1, SEM_UNDO};

    if (semop(semid, &buf, 1)) {
	perror("semop");
	exit(1);
    }
}

void
passwd_unlock()
{
    struct sembuf   buf = {0, 1, SEM_UNDO};

    if (semop(semid, &buf, 1)) {
	perror("semop");
	exit(1);
    }
}
