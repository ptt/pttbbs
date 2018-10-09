#ifndef LIBBBS_H_
#define LIBBBS_H_

#include "pttstruct.h" /* for fileheader_t */

/* name.c */
int is_validuserid(const char *id);

/* path.c */
/* XXX set*() all assume buffer size = PATHLEN */
void setdirpath(char *buf, const char *direct, const char *fname);
void setbpath (char *buf, const char *boardname);
void setbfile (char *buf, const char *boardname, const char *fname);
void setbnfile(char *buf, const char *boardname, const char *fname, int n);
void setapath(char *buf, const char *boardname);
void setadir (char *buf, const char *path);
void sethomepath(char *buf, const char *userid);
void sethomedir (char *buf, const char *userid);
void sethomeman (char *buf, const char *userid);
void sethomefile(char *buf, const char *userid, const char *fname);
void setuserhashedfile(char *buf, const char *filename);
// setbdir
// setuserfile

/* money.c */
const char* money_level(int money);

/* string.c */  
void obfuscate_ipstr(char *s);
bool is_valid_brdname(const char *brdname);
const char *subject_ex(const char *title, int *ptype);
const char *subject(const char *title);

/* time.c */
const char *Now(void);	// m3 flavor time string

/* fhdr_stamp.c */
int stampfile(char *fpath, fileheader_t * fh);
int stampdir(char *fpath, fileheader_t * fh);
int stamplink(char *fpath, fileheader_t * fh);
int stampfile_u(char *fpath, fileheader_t *fh);	// does not zero existing data in fh

/* log.c */
int log_payment(const char *filename, int money, int oldm, int newm,
                       const char *reason, time4_t now);

/* banip.c */
typedef in_addr_t IPv4;     // derived from in_addr.s_addr
typedef void BanIpList;
const char *in_banip_list(const BanIpList *list, const char *ip);
const char *in_banip_list_addr(const BanIpList *list, IPv4 addr);
BanIpList *load_banip_list(const char *filename, FILE *err);
BanIpList *free_banip_list(BanIpList *list);
BanIpList *cached_banip_list(const char *basefile, const char *cachefile);

/* cache.c */
#define search_ulist(uid) search_ulistn(uid, 1)
#define getbcache(bid) (bcache + bid - 1)
#define moneyof(uid) SHM->money[uid - 1]
#define getbtotal(bid) SHM->total[bid - 1]
#define getbottomtotal(bid) SHM->n_bottom[bid-1]
unsigned int safe_sleep(unsigned int seconds);
void *attach_shm(int shmkey, int shmsize);
#define attach_SHM()	attach_check_SHM(SHM_VERSION, sizeof(SHM_t))
void attach_check_SHM(int version, int SHM_t_size);
void add_to_uhash(int n, const char *id);
void remove_from_uhash(int n);
int  dosearchuser(const char *userid, char *rightid);
int  searchuser(const char *userid, char *rightid);
void setuserid(int num, const char *userid);
userinfo_t *search_ulistn(int uid, int unum);
userinfo_t *search_ulist_pid(int pid);
userinfo_t *search_ulist_userid(const char *userid);
int  setumoney(int uid, int money);
int  deumoney(int uid, int money);
void touchbtotal(int bid);
void sort_bcache(void);
void reload_bcache(void);
void resolve_boards(void);
int  num_boards(void);
void addbrd_touchcache(void);
void reset_board(int bid);
void resolve_board_group(int gid, int type);
void setbottomtotal(int bid);
void setbtotal(int bid);
void touchbpostnum(int bid, int delta);
int  getbnum(const char *bname);
void buildBMcache(int);
int parseBMlist(const char *input, int uids[MAX_BMs]);
void reload_fcache(void);
void reload_pttcache(void);
void resolve_garbage(void);
void resolve_fcache(void);
void hbflreload(int bid);
int is_hidden_board_friend(int bid, int uid);
#ifdef USE_COOLDOWN
# define cooldowntimeof(uid) (SHM->cooldowntime[uid - 1] & 0xFFFFFFF0)
# define posttimesof(uid) (SHM->cooldowntime[uid - 1] & 0xF)
void add_cooldowntime(int uid, int min);
void add_posttimes(int uid, int times);
# endif

/* passwd */
int  passwd_init  (void);
void passwd_lock  (void);
void passwd_unlock(void);
int  passwd_update_money(int num);
int  passwd_update(int num, userec_t *buf);
int  passwd_query (int num, userec_t *buf);
int  passwd_load_user(const char *userid, userec_t *buf);
int  passwd_apply (void *data, int (*fptr)(void *, int, userec_t *));
int passwd_fast_apply(void *ctx, int(*fptr)(void *ctx, int, userec_t *));
int passwd_require_secure_connection(const userec_t *u);
int  checkpasswd  (const char *passwd, char *test);  // test will be destroyed
void logattempt   (const char *uid, char type, time4_t now, const char *fromhost);
char*genpasswd    (char *pw);

/* record */
int substitute_fileheader(const char *dir_path, const void *srcptr, const void *destptr, int id);
int delete_fileheader(const char *dir_path, const void *rptr, int id);

/* search.c */
typedef struct fileheader_predicate_t {
    int mode;
    char keyword[TTLEN + 1];
    int recommend;
    int money;
} fileheader_predicate_t;

void select_read_name(char *buf, size_t size, const char *base,
                      const fileheader_predicate_t *pred);

int match_fileheader_predicate(const fileheader_t *fh, void *arg);

int select_read_build(const char *src_direct, const char *dst_direct,
                      int src_direct_has_reference, time4_t resume_from,
                      int dst_count,
                      int (*match)(const fileheader_t *fh, void *arg),
                      void *arg);

int select_read_should_build(const char *dst_direct, int bid,
                             time4_t *resume_from, int *count);

#endif
