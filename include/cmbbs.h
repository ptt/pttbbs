#ifndef _LIBBBS_H_
#define _LIBBBS_H_

#include "pttstruct.h" /* for fileheader_t */

/* name.c */
extern int is_validuserid(const char *id);

/* path.c */
/* XXX set*() all assume buffer size = PATHLEN */
extern void setdirpath(char *buf, const char *direct, const char *fname);
extern void setbpath (char *buf, const char *boardname);
extern void setbfile (char *buf, const char *boardname, const char *fname);
extern void setbnfile(char *buf, const char *boardname, const char *fname, int n);
extern void setapath(char *buf, const char *boardname);
extern void setadir (char *buf, const char *path);
extern void sethomepath(char *buf, const char *userid);
extern void sethomedir (char *buf, const char *userid);
extern void sethomeman (char *buf, const char *userid);
extern void sethomefile(char *buf, const char *userid, const char *fname);
// setbdir
// setuserfile

/* money.c */
extern const char* money_level(int money);

/* string.c */  
extern void obfuscate_ipstr(char *s);
extern bool is_valid_brdname(const char *brdname);

/* time.c */
extern const char *Now();	// m3 flavor time string

/* fhdr_stamp.c */
extern int stampfile(char *fpath, fileheader_t * fh);
extern int stampdir(char *fpath, fileheader_t * fh);
extern int stamplink(char *fpath, fileheader_t * fh);
extern int stampfile_u(char *fpath, fileheader_t *fh);	// does not zero existing data in fh

/* cache.c */
#define search_ulist(uid) search_ulistn(uid, 1)
#define getbcache(bid) (bcache + bid - 1)
#define moneyof(uid) SHM->money[uid - 1]
#define getbtotal(bid) SHM->total[bid - 1]
#define getbottomtotal(bid) SHM->n_bottom[bid-1]
extern unsigned int safe_sleep(unsigned int seconds);
extern void *attach_shm(int shmkey, int shmsize);
#define attach_SHM()	attach_check_SHM(SHM_VERSION, sizeof(SHM_t))
extern void attach_check_SHM(int version, int SHM_t_size);
extern void add_to_uhash(int n, const char *id);
extern void remove_from_uhash(int n);
extern int  dosearchuser(const char *userid, char *rightid);
extern int  searchuser(const char *userid, char *rightid);
extern void setuserid(int num, const char *userid);
extern userinfo_t *search_ulistn(int uid, int unum);
extern userinfo_t *search_ulist_pid(int pid);
extern userinfo_t *search_ulist_userid(const char *userid);
extern int  setumoney(int uid, int money);
extern int  deumoney(int uid, int money);
extern void touchbtotal(int bid);
extern void sort_bcache(void);
extern void reload_bcache(void);
extern void resolve_boards(void);
extern void addbrd_touchcache(void);
extern void reset_board(int bid);
extern void setbottomtotal(int bid);
extern void setbtotal(int bid);
extern void touchbpostnum(int bid, int delta);
extern int  getbnum(const char *bname);
extern void buildBMcache(int);
extern void reload_fcache(void);
extern void reload_pttcache(void);
extern void resolve_garbage(void);
extern void resolve_fcache(void);
extern void hbflreload(int bid);
extern int is_hidden_board_friend(int bid, int uid);
#ifdef USE_COOLDOWN
# define cooldowntimeof(uid) (SHM->cooldowntime[uid - 1] & 0xFFFFFFF0)
# define posttimesof(uid) (SHM->cooldowntime[uid - 1] & 0xF)
extern void add_cooldowntime(int uid, int min);
extern void add_posttimes(int uid, int times);
# endif

/* passwd */
extern int  passwd_init  (void);
extern void passwd_lock  (void);
extern void passwd_unlock(void);
extern int  passwd_update_money(int num);
extern int  passwd_update(int num, userec_t *buf);
extern int  passwd_query (int num, userec_t *buf);
extern int  passwd_load_user(const char *userid, userec_t *buf);
extern int  passwd_apply (void *data, int (*fptr)(void *, int, userec_t *));
extern int  checkpasswd  (const char *passwd, char *test);  // test will be destroyed
extern void logattempt   (const char *uid, char type, time4_t now, const char *fromhost);
extern char*genpasswd    (char *pw);

#endif
