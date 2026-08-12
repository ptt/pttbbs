
#include <assert.h>
#include <pthread.h>
#include <stddef.h>
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
#include "cmbbs.h"
#include "common.h"
#include "uflags.h"
#include "var.h"

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

#ifndef SEM_R
#define SEM_R 0400
#endif

#ifndef SEM_A
#define SEM_A 0200
#endif

#if !defined( __FreeBSD__ ) &&  !__DARWIN_UNIX03
#include <sys/sem.h>
union semun {
    int             val;	/* value for SETVAL */
    struct semid_ds *buf;	/* buffer for IPC_STAT & IPC_SET */
    unsigned short  *array;	/* array for GETALL & SETALL */
    struct seminfo *__buf;	/* buffer for IPC_INFO */
};
#endif

#ifdef USE_PTHREAD_MUTEX_PASSWD
#define PWD_USE_PTHREAD (1)
#else
#define PWD_USE_PTHREAD (0)
#endif

static int
passwd_init_pthread(void)
{
    // Must be already done in uhash_loader.c to the SHM variable
    return 0;
}

static void
passwd_lock_pthread(void)
{
    int rc = pthread_mutex_lock(&SHM->GV3.e.passwd_mutex);
    if (rc == EOWNERDEAD) {
        pthread_mutex_consistent(&SHM->GV3.e.passwd_mutex);
    } else if (rc != 0) {
        fprintf(stderr, "[passwd_lock error] pthread_mutex_lock failed: %s\n", strerror(rc));
        exit(1);
    }
}

static void
passwd_unlock_pthread(void)
{
    int rc = pthread_mutex_unlock(&SHM->GV3.e.passwd_mutex);
    if (rc != 0) {
        fprintf(stderr, "[passwd_unlock error] pthread_mutex_unlock failed: %s\n", strerror(rc));
        exit(1);
    }
}

// semaphore based PASSWD locking

static int      semid = -1;

static int
passwd_init_sysv(void)
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

static void
passwd_lock_sysv(void)
{
    struct sembuf   buf = {0, -1, SEM_UNDO};

    if (semid == -1)
        passwd_init();

    if (semop(semid, &buf, 1)) {
	perror("semop");
	exit(1);
    }
}

static void
passwd_unlock_sysv(void)
{
    struct sembuf   buf = {0, 1, SEM_UNDO};

    if (semop(semid, &buf, 1)) {
	perror("semop");
	exit(1);
    }
}

int
passwd_init(void) {
    if (PWD_USE_PTHREAD)
        return passwd_init_pthread();
    else
        return passwd_init_sysv();
}

void
passwd_lock(void) {
    if (PWD_USE_PTHREAD)
        passwd_lock_pthread();
    else
        passwd_lock_sysv();
}

void
passwd_unlock(void) {
    if (PWD_USE_PTHREAD)
        passwd_unlock_pthread();
    else
        passwd_unlock_sysv();
}

// updateing passwd/userec_t

int
passwd_update_money(int num)
/* update money only 
   Ptt: don't call it directly, call deumoney() */
{
    int  pwdfd;
    int  money=moneyof(num);
    if (num < 1 || num > MAX_USERS)
        return -1;

    if ((pwdfd = open(fn_passwd, O_WRONLY)) < 0)
        exit(1);
    lseek(pwdfd, sizeof(userec_t) * (num - 1) + offsetof(userec_t, money),
	  SEEK_SET);
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

/**
 * Safely purge/delete a user account and clean up home directory,
 * SHM hash table, audit logs, and .PASSWDS file.
 *
 * @param unum User ID number (1-indexed)
 * @param userid User ID string
 * @param action_tag Action description tag for audit logging (e.g. "KILL", "CLEAN(EXPIRE)", "CLEAN(CLEAR)")
 * @return 0 on success, -1 on failure
 */
int
purge_user_account(int unum, const char *userid, const char *action_tag)
{
    userec_t u = {0};
    char src[PATH_MAX], dst[PATH_MAX];
    time4_t now_t;
    const char *uid_shm;
    int ret;

    if (!userid || !*userid || unum <= 0 || unum > MAX_USERS)
        return -1;

    // Acquire lock to guarantee atomic check, SHM clearing, and .PASSWDS update
    passwd_lock();

    // Verify unum matches expected userid in SHM if present
    uid_shm = getuserid(unum);
    if (uid_shm && *uid_shm && strcasecmp(uid_shm, userid) != 0) {
        passwd_unlock();
        return -1;
    }

    // Clear user hash table mapping and money in memory SHM
    setumoney(unum, 0);
    setuserid(unum, "");

    // Zero out user record and update .PASSWDS file
    ret = passwd_update(unum, &u);

    passwd_unlock();

    // Audit log entry to USIES log
    time4(&now_t);
    if (!action_tag || !*action_tag)
        action_tag = "CLEAN(PURGE)";

    log_filef(FN_USIES, LOG_CREAT, "%s %s %-12s\n", Cdate(&now_t), action_tag, userid);

    // Archive or remove user home directory
    sethomepath(src, userid);
    SNPRINTF(dst, "tmp/%s", userid);
    if (dashd(src)) {
        if (Rename(src, dst) != 0) {
            RmTree(src);
        }
    }

    return ret;
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

int
passwd_fast_apply(void *ctx, int(*fptr)(void *ctx, int, userec_t *))
{
    int i, fd;
    userec_t user;
    if ((fd = open(fn_passwd, O_RDONLY)) < 0)
        exit(1);
    for (i = 0; i < MAX_USERS; i++) {
        memset(&user, 0, sizeof(user));
        if (read(fd, &user, sizeof(user)) != sizeof(user))
            return -1;
	if ((*fptr) (ctx, i, &user) < 0)
	    return -1;
    }
    close(fd);
    return 0;
}

int
is_bcrypt_hash(const char *hash)
{
    return (hash && strncmp(hash, "$2b$", 4) == 0);
}

// XXX NOTE: string in plain will be destroyed.
static int
checkpasswd(const char *pwhash, char *plain)
{
    int ok = 0;
    if (is_bcrypt_hash(pwhash)) {
        ok = (bcrypt_checkpass(plain, pwhash) == 0);
    } else if (pwhash && pwhash[0]) {
        char buf[9];
        STRLCPY(buf, plain);
        char *pw = fcrypt(buf, pwhash);
        if (pw && strcmp(pw, pwhash) == 0)
            ok = 1;
        explicit_bzero(buf, sizeof(buf));
    }
    explicit_bzero(plain, strlen(plain));
    return ok;
}

int
checkuser_passwd(const userec_t *u, char *plain)
{
    /* If fhash is available, the user has previously set a password <= 8
     * characters so verifying with fcrypt is enough.
     * Do not try bcrypt first because they may consider 'extra chars are fine'.
     * Only when fhash is empty, we can make sure the user did set a long
     * password with the right prompt.
     */
    if (u->pw_fhash[0] != '\0')
        return checkpasswd(u->pw_fhash, plain);
    return checkpasswd(u->pw_bhash, plain);
}

void
setuser_passwd(userec_t *u, const char *plain)
{
    size_t len = strlen(plain);
    char salt[32];
    const int fcrypt_input_max = 8;

    assert(plain && *plain);

    explicit_bzero(u->pw_bhash, sizeof(u->pw_bhash));
    explicit_bzero(u->pw_fhash, sizeof(u->pw_fhash));

    if (bcrypt_gensalt(10, salt, sizeof(salt)) == 0) {
        bcrypt_hashpass(plain, salt, u->pw_bhash, sizeof(u->pw_bhash));
    }

    // In theory, we may drop the fhash; but to allow better fail-safe
    // we'd like to retain the fhash as long as possible.
    // However, with plain text >= 8 characters, fhash-style users may expect
    // (or get confused) they can add random garbage to the end, so we should
    // enforce them using bcrypt only.
    if (len < fcrypt_input_max) {
        const char *des_hash = genpasswd(plain);
        STRLCPY(u->pw_fhash, des_hash);
    }
}

const char *
genpasswd(const char *pw)
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

    SNPRINTF(genbuf, "%c%-12s[%s] ?@%s\n", type, uid,
	    Cdate(&now), loghost);
    len = strlen(genbuf);
    // log to public (BBSHOME)
    if ((fd = OpenCreate(FN_BADLOGIN, O_WRONLY | O_APPEND)) >= 0) {
	write(fd, genbuf, len);
	close(fd);
    }
    // log to user private log
    if (type == '-') {
	SNPRINTF(genbuf, "[%s] %s\n", Cdate(&now), loghost);
	len = strlen(genbuf);
	sethomefile(fname, uid, FN_BADLOGIN);
	if ((fd = OpenCreate(fname, O_WRONLY | O_APPEND)) >= 0) {
	    write(fd, genbuf, len);
	    close(fd);
	}
    }
}

