/* $Id: passwd.c,v 1.3 2002/06/30 14:33:14 in2 Exp $ */
#include "bbs.h"

static int semid = -1;

#ifndef SEM_R
#define SEM_R 0400
#endif

#ifndef SEM_A
#define SEM_A 0200
#endif

#ifndef __FreeBSD__
union semun {
        int     val;            /* value for SETVAL */
        struct  semid_ds *buf;  /* buffer for IPC_STAT & IPC_SET */
        u_short *array;         /* array for GETALL & SETALL */
        struct seminfo *__buf;  /* buffer for IPC_INFO */
};
#endif

int PASSWDfd;
int passwd_mmap() {
    if( (PASSWDfd = open(fn_passwd, O_RDWR)) < 0 || PASSWDfd <= 1 )
	return -1;
    semid = semget(PASSWDSEM_KEY, 1, SEM_R | SEM_A | IPC_CREAT | IPC_EXCL);
    if(semid == -1) {
	if(errno == EEXIST) {
	    semid = semget(PASSWDSEM_KEY, 1, SEM_R | SEM_A);
	    if(semid == -1) {
		perror("semget");
		exit(1);
	    }
	} else {
	    perror("semget");
	    exit(1);
	}
    } else {
	union semun s;
	
	s.val = 1;
	if(semctl(semid, 0, SETVAL, s) == -1) {
	    perror("semctl");
	    exit(1);
	}
    }

    return 0;
}

int passwd_update_money(int num) {
    userec_t user;
    if(num < 1 || num > MAX_USERS)
        return -1;
    passwd_query(num, &user);
    user.money = moneyof(num);
    passwd_update(num, &user);
    return 0;
}   

int passwd_update(int num, userec_t *buf) {
    if(num < 1 || num > MAX_USERS)
	return -1;
    buf->money = moneyof(num);
    lseek(PASSWDfd, sizeof(userec_t) * (num - 1), SEEK_SET);
    write(PASSWDfd, buf, sizeof(userec_t));
    return 0;
}

int passwd_query(int num, userec_t *buf) {
    if(num < 1 || num > MAX_USERS)
	return -1;
    lseek(PASSWDfd, sizeof(userec_t) * (num - 1), SEEK_SET);
    read(PASSWDfd, buf, sizeof(userec_t));
    return 0;
}

int passwd_apply(int (*fptr)(userec_t *)) {
    int i;
    userec_t user;
    for(i = 0; i < MAX_USERS; i++){
	passwd_query(i + 1, &user);
	if((*fptr)(&user) == QUIT)
	    return QUIT;
    }
    return 0;
}

void passwd_lock() {
    struct sembuf buf = { 0, -1, SEM_UNDO };
    
    if(semop(semid, &buf, 1)) {
	perror("semop");
	exit(1);
    }
}

void passwd_unlock() {
    struct sembuf buf = { 0, 1, SEM_UNDO };
    
    if(semop(semid, &buf, 1)) {
	perror("semop");
	exit(1);
    }
}
