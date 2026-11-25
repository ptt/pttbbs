
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

/* passwd_lock_pthread() returns holding the mutex and passwd_unlock_pthread()
 * releases it, for passwd_lock()'s and passwd_unlock()'s callers. The
 * analysis checks one function at a time and would report both, where the
 * system marks the mutex functions (FreeBSD's pthread.h does), so they are
 * left out. */
static void TSA_NO_ANALYSIS
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

static void TSA_NO_ANALYSIS
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

    if ((pwdfd = open(FN_PASSWD, O_WRONLY)) < 0)
        exit(1);
    lseek(pwdfd, sizeof(userec_t) * (num - 1) + offsetof(userec_t, money),
	  SEEK_SET);
    write(pwdfd, &money, sizeof(int));
    close(pwdfd);
    return 0;
}

static void
userec_storage_to_mem(userec_t *u)
{
    if (!NEED_STORAGE_CONV || !u)
        return;
#define CONV_FROM_S(field) \
    storage_to_mb(u->field, u->field, sizeof(u->field))
    CONV_FROM_S(realname);
    CONV_FROM_S(nickname);
    CONV_FROM_S(address);
    CONV_FROM_S(career);
    CONV_FROM_S(justify);
#undef CONV_FROM_S
}

static inline void
conv_to_s_diff(char *dst, const char *orig_d, const char *orig_m, size_t sz)
{
    int changed = 1;
    if (orig_m) {
        changed = (memcmp(orig_m, dst, sz) != 0);
    } else if (orig_d) {
        char orig_mb[STRLEN];
        assert(sz <= sizeof(orig_mb));
        storage_to_mb(orig_d, orig_mb, sz);
        changed = (strcmp(dst, orig_mb) != 0);
    }
    if (!changed && orig_d)
        memcpy(dst, orig_d, sz);
    else
        mb_to_storage(dst, dst, sz);
}

static void
userec_mem_to_storage_diff(userec_t *disk_buf, const userec_t *orig_disk,
                           const userec_t *orig_mem)
{
    if (!NEED_STORAGE_CONV || !disk_buf)
        return;
#define CONV_TO_S_DIFF(field) \
    conv_to_s_diff(disk_buf->field, \
                   orig_disk ? orig_disk->field : NULL, \
                   orig_mem ? orig_mem->field : NULL, \
                   sizeof(disk_buf->field))
    CONV_TO_S_DIFF(realname);
    CONV_TO_S_DIFF(nickname);
    CONV_TO_S_DIFF(address);
    CONV_TO_S_DIFF(career);
    CONV_TO_S_DIFF(justify);
#undef CONV_TO_S_DIFF
}

int
passwd_update_diff(int num, const userec_t *orig_mem, userec_t *buf)
{
    int pwdfd;
    userec_t disk_buf, orig_disk;
    const userec_t *wptr = buf;
    off_t offset;
    if (num < 1 || num > MAX_USERS)
	return -1;

    offset = (off_t)sizeof(userec_t) * (num - 1);
    if ((pwdfd = open(FN_PASSWD, NEED_STORAGE_CONV ? O_RDWR : O_WRONLY)) < 0)
	exit(1);

    if (NEED_STORAGE_CONV && buf) {
        const userec_t *orig_disk_ptr = NULL;
        if (lseek(pwdfd, offset, SEEK_SET) == offset &&
            read(pwdfd, &orig_disk, sizeof(userec_t)) == sizeof(userec_t)) {
            orig_disk_ptr = &orig_disk;
        }
        disk_buf = *buf;
        userec_mem_to_storage_diff(&disk_buf, orig_disk_ptr, orig_mem);
        wptr = &disk_buf;
    }

    lseek(pwdfd, offset, SEEK_SET);
    write(pwdfd, wptr, sizeof(userec_t));
    close(pwdfd);

    return 0;
}

int
passwd_update(int num, userec_t * buf)
{
    return passwd_update_diff(num, NULL, buf);
}

// Formats t as YYYY/MM/DD HH:MM for the log of accounts, "-" if unset.
static const char *
account_event_time(char *buf, size_t sz, time4_t t)
{
    time_t tt = time4_to_time(t);
    struct tm tm;

    if (!t || !localtime_r(&tt, &tm))
        strlcpy(buf, "-", sz);
    else
        strftime(buf, sz, "%Y/%m/%d %H:%M", &tm);
    return buf;
}

// Logs an account made or deleted to FN_ACCOUNT_EVENTS, which bin/account
// posts to Security every day: what admins need to tell a deleted account
// from a new one of the same name (registered when, last seen when and
// from where, how active, the email). PTT-specific: Lemmy keeps no such
// log (an admin purging a person is in its modlog).
//
// Two lines, to fit in 80 columns (only a long email goes past). The day
// is that of the post, so only the time of the event is written (not
// with log_filef, which puts the date and seconds before each):
//
//   00:30 EXPIRE(REPLACE) olduser      #123456  regok days=812   posts=37
//         2009/03/14 21:05 - 2025/11/02 23:40 1.2.3.4         x@example.com
//
// The second line is the first and last login, where from (the last login,
// or where a new account registers from) and the email.
void
log_account_event(const char *action, int unum, const userec_t *u,
                  const char *from)
{
    char now[8], first[20], last[20];
    time_t t = time(NULL);
    struct tm tm;

    if (!u || !u->userid[0])
        return;
    if (!from || !*from)
        from = u->lasthost[0] ? u->lasthost : "-";
    localtime_r(&t, &tm);
    strftime(now, sizeof(now), "%H:%M", &tm);
    file_appendf(FN_ACCOUNT_EVENTS,
                 "%s %-15s %-12s #%-7d %s days=%-5u posts=%u\n"
                 "      %-16s - %-16s %-15s %s\n",
                 now, action, u->userid, unum,
                 (u->userlevel & PERM_LOGINOK) ? "regok" : "unreg",
                 u->numlogindays, u->numposts,
                 account_event_time(first, sizeof(first), u->firstlogin),
                 account_event_time(last, sizeof(last), u->lastlogin),
                 from, u->email[0] ? u->email : "-");
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
    userec_t u = {0}, old = {0};
    char src[PATH_MAX];
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

    // The record as it was, for the log of accounts.
    if (passwd_query(unum, &old) != 0 || strcasecmp(old.userid, userid) != 0)
        memset(&old, 0, sizeof(old));

    // Clear user hash table mapping and money in memory SHM
    setumoney(unum, 0);
    setuserid(unum, "");

    // Zero out user record and update .PASSWDS file
    ret = passwd_update(unum, &u);

    passwd_unlock();

    // Audit log entry to USIES log
    if (!action_tag || !*action_tag)
        action_tag = "CLEAN(PURGE)";

    time4_t now_t = (time4_t)time(NULL);
    file_appendf(FN_USIES, "%s %s %-12s\n", Cdate(&now_t), action_tag, userid);
    if (ret == 0)
        log_account_event(action_tag, unum, &old, NULL);

    // Archive or remove user home directory
    sethomepath(src, userid);
    if (dashd(src)) {
        AUTO_STR(dst, "tmp/%s", userid);
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
    if ((pwdfd = open(FN_PASSWD, O_RDONLY)) < 0)
	exit(1);
    posix_fadvise(pwdfd, sizeof(userec_t) * (num - 1), sizeof(userec_t), POSIX_FADV_RANDOM);
    lseek(pwdfd, sizeof(userec_t) * (num - 1), SEEK_SET);
    read(pwdfd, buf, sizeof(userec_t));
    close(pwdfd);

    userec_storage_to_mem(buf);

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
    return passwd_fast_apply(ctx, fptr);
}

int
passwd_fast_apply(void *ctx, int(*fptr)(void *ctx, int, userec_t *))
{
    int i, fd;
    userec_t user;
    if ((fd = open(FN_PASSWD, O_RDONLY)) < 0)
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

    // log to public (BBSHOME)
    file_appendf(FN_BADLOGIN, "%c%-12s[%s] ?@%s\n", type, uid,
	    Cdate(&now), loghost);

    // log to user private log
    if (type == '-') {
	sethomefile(fname, uid, FN_BADLOGIN);
	file_appendf(fname, "[%s] %s\n", Cdate(&now), loghost);
	// A history that the user can not clear, for investigations.
	if (LOG_CONF_BADLOGIN_HISTORY) {
	    sethomefile(fname, uid, FN_BADLOGIN ".history");
	    rotate_text_logfile(fname, SZ_BADLOGIN_HISTORY, 0.5);
	    file_appendf(fname, "[%s] %s\n", Cdate(&now), loghost);
	}
    }
}

