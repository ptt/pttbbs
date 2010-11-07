/* $Id$ */
#ifndef INCLUDE_PROTO_H
#define INCLUDE_PROTO_H

#ifdef __GNUC__
#define GCC_CHECK_FORMAT(a,b) __attribute__ ((format (printf, a, b)))
#define GCC_NORETURN          __attribute__ ((__noreturn__))
#define GCC_UNUSED            __attribute__ ((__unused__))
#else
#define GCC_CHECK_FORMAT(a,b)
#define GCC_NORETURN
#define GCC_UNUSED
#endif

/* admin */
int m_loginmsg(void);
int m_mod_board(char *bname);
int m_newbrd(int whatclass, int recover);
int m_user(void);
int search_user_bypwd(void);
int search_user_bybakpwd(void);
int m_board(void);
int m_register(void);
int cat_register(void);
unsigned int setperms(unsigned int pbits, const char * const pstring[]);
void setup_man(const boardheader_t * board, const boardheader_t * oldboard);
void delete_symbolic_link(boardheader_t *bh, int bid);
int make_symbolic_link(const char *bname, int gid);
int make_symbolic_link_interactively(int gid);
void merge_dir(const char *dir1, const char *dir2, int isoutter);

/* angel */
int a_changeangel(void);
int a_angelmsg(void);
int a_angelreport(void);
int a_angelreload(void);
int angel_reject_me(userinfo_t * uin);
void CallAngel(void);
void angel_toggle_pause();
void angel_load_my_fullnick(char *buf, int szbuf); // full nick!
const char *angel_get_nick();
void pressanykey_or_callangel(void);
#ifdef PLAY_ANGEL
#define PRESSANYKEY() pressanykey_or_callangel()
#else  // !PLAY_ANGEL
#define PRESSANYKEY() pressanykey()
#endif

/* announce */
int a_menu(const char *maintitle, const char *path, int lastlevel, int lastbid,
           char *trans_buffer, const char *backup_dir);
void a_copyitem(const char* fpath, const char* title, const char* owner, int mode);
int Announce(void);

/* acl */
time4_t is_user_banned_by_board(const char *user, const char *board);
time4_t is_banned_by_board(const char *board);
int ban_user_for_board(const char *user, const char *board, time4_t expire, const char *reason);
int edit_banned_list_for_board(const char *board);

/* assess */
int inc_badpost(const char *, int num);
int bad_comment(const char *fn);
int assign_badpost(const char *userid, fileheader_t *fhdr, const char *newpath, const char *comment);

/* bbs */
int is_file_owner(const fileheader_t *, const userec_t*);
void delete_allpost(const char *userid);
int del_range(int ent, const fileheader_t *fhdr, const char *direct, const char *backup_direct);
int cmpfowner(fileheader_t *fhdr);
int b_note_edit_bname(int bid);
int Read(void);
int CheckPostPerm(void);
int CheckModifyPerm(void);
int CheckPostRestriction(int);
void anticrosspost(void);
int Select(void);
void do_reply_title(int row, const char *title, char result[STRLEN]);
void outgo_post(const fileheader_t *fh, const char *board, const char *userid, const char *username);
int edit_title(int ent, fileheader_t *fhdr, const char *direct);
int whereami(void);
void set_board(void);
int do_post(void);
int ReadSelect(void);
int save_violatelaw(void);
int board_select(void);
int board_digest(void);
int do_limitedit(int ent, fileheader_t * fhdr, const char *direct);
#ifdef USE_COOLDOWN
int check_cooldown(boardheader_t *bp);
#endif

/* board */
#define setutmpbid(bid) currutmp->brc_id=bid;
int is_readonly_board(const char *bname);
int enter_board(const char *boardname);
int HasBoardPerm(boardheader_t *bptr);
void save_brdbuf(void);
void init_brdbuf(void);
int b_config();
// Board Selections
int New(void);
int Favorite(void);
int Class(void);
int TopBoards(void);

/* brc */
int brc_initialize(void);
void brc_finalize(void);
int brc_initial_board(const char *boardname);

// v3 api: add 'modified' tag
int brc_unread(int bid, const char *fname, time4_t modified);
int brc_unread_time(int bid, time4_t ftime,time4_t modified);
void brc_addlist(const char* fname, time4_t modified);
void brc_update(void);
void brc_toggle_all_read(int bid, int is_all_read);
void brc_toggle_read(int bid, time4_t newtime);

/* cache */
unsigned int getutmpmode(void);
void setutmpmode(unsigned int mode);
void purge_utmp(userinfo_t *uentp);
void getnewutmpent(const userinfo_t *up);
int  apply_ulist(int (*fptr)(const userinfo_t *));
char*getuserid(int num);
int  getuser(const char *userid, userec_t *xuser);
int searchnewuser(int mode);
int count_logins(int uid, int show);
void invalid_board_permission_cache(const char *board);
int is_BM_cache(int);
int apply_boards(int (*func)(boardheader_t *));
int haspostperm(const char *bname);
const char * postperm_msg(const char *bname);

/* cal */
const char* money_level(int money);
int pay(int money, const char *item, ...) GCC_CHECK_FORMAT(2,3); 
int pay_as_uid(int uid, int money, const char *item,...)GCC_CHECK_FORMAT(3,4);
int lockutmpmode(int unmode, int state);
int unlockutmpmode(void);
int x_file(void);
int give_money(void);
int p_sysinfo(void);
int give_money_ui(const char *userid);
int p_give(void);
int p_cloak(void);
int p_from(void);
int p_exmail(void);
int mail_redenvelop(const char* from, const char* to, int money, char *fpath);
void resolve_over18(void);
int resolve_over18_user(const userec_t *u);

/* card */
int g_card_jack(void);
int g_ten_helf(void);
int card_99(void);

/* ccw (common chat window) */
int ccw_talk(int fd, int destuid);	// common chat window: private talk
int ccw_chat(int fd);			// common chat window: chatroom

/* psb (panty and stocking browser) */
int psb_view_edit_history(const char *base, const char *subject,
                          int maxrev, int current_as_base);
int psb_recycle_bin(const char *base, const char *title);
int psb_admin_edit();

/* chc */
void chc(int s, ChessGameMode mode);
int chc_main(void);
int chc_personal(void);
int chc_watch(void);
ChessInfo* chc_replay(FILE* fp);

/* chicken */
int chicken_main(void);
int chickenpk(int fd);
int load_chicken(const char *uid, chicken_t *mychicken);
void chicken_query(const char *userid);
void show_chicken_data(chicken_t *thechicken, chicken_t *pkchicken);
void chicken_toggle_death(const char *uid);

/* dark */
int main_dark(int fd,userinfo_t *uin);

/* dice */
int dice_main(void);

/* edit */
int vedit(const char *fpath, int saveheader, int *islocal, char save_title[STRLEN]);
int vedit2(const char *fpath, int saveheader, int *islocal, char save_title[STRLEN], int flags);
int veditfile(const char *fpath);
void write_header(FILE *fp, const char *mytitle);
void addsignature(FILE *fp, int ifuseanony);
void auto_backup(void);
void restore_backup(void);
const char *ask_tmpbuf(int y);

/* emaildb */
#ifdef USE_EMAILDB
int emaildb_check_email (const char *email,  int email_len);
int emaildb_update_email(const char *userid, int userid_len, const char *email, int email_len);
#endif
#ifdef USE_REGCHECKD
int regcheck_ambiguous_userid_exist(const char *userid);
#endif

/* fav */
void fav_set_old_folder(fav_t *fp);
int get_data_number(fav_t *fp);
int get_current_fav_level(void);
fav_t *get_current_fav(void);
int get_item_type(fav_type_t *ft);
char *get_item_title(fav_type_t *ft);
char *get_folder_title(int fid);
void set_attr(fav_type_t *ft, int bit, char boolean);
void fav_sort_by_name(void);
void fav_sort_by_class(void);
int fav_load(void);
int fav_save(void);
void fav_remove_item(int id, char type);
fav_type_t *getadmtag(int bid);
fav_type_t *getboard(int bid);
fav_type_t *getfolder(int fid);
char getbrdattr(int bid);
time4_t getbrdtime(int bid);
void setbrdtime(int bid, time4_t t);
int fav_getid(fav_type_t *ft);
void fav_tag(int id, char type, char boolean);
void move_in_current_folder(int from, int to);
void fav_move(int from, int to);
fav_type_t *fav_add_line(void);
fav_type_t *fav_add_folder(void);
fav_type_t *fav_add_board(int bid);
fav_type_t *fav_add_admtag(int bid);
void fav_remove_all_tagged_item(void);
void fav_add_all_tagged_item(void);
void fav_remove_all_tag(void);
void fav_set_folder_title(fav_type_t *ft, char *title);
int fav_stack_full(void);
void fav_folder_in(int fid);
void fav_folder_out(void);
void fav_free(void);
int fav_v3_to_v4(void);
int is_set_attr(fav_type_t *ft, char bit);
void fav_cleanup(void);
void fav_clean_invisible(void);
fav_t *get_fav_folder(fav_type_t *ft);
fav_t *get_fav_root(void);
int updatenewfav(int mode);
void subscribe_newfav(void);
void reginit_fav(void);

/* friend */
void friend_edit(int type);
void friend_load(int type, int do_login);
int t_override(void);
int t_reject(void);
int t_fix_aloha();
void friend_add(const char *uident, int type, const char *des);
void friend_delete(const char *uident, int type);
void friend_delete_all(const char *uident, int type);
void friend_special(void);
void setfriendfile(char *fpath, int type);

/* gamble */
int ticket_main(void);
int openticket(int bid);
int ticket(int bid);

/* go */
void gochess(int s, ChessGameMode mode);
int gochess_main(void);
int gochess_personal(void);
int gochess_watch(void);
ChessInfo* gochess_replay(FILE* fp);

/* gomo */
void gomoku(int s, ChessGameMode mode);
int gomoku_main(void);
int gomoku_personal(void);
int gomoku_watch(void);
ChessInfo* gomoku_replay(FILE* fp);

/* guess */
int guess_main(void);

/* convert */
void set_converting_type(int which);

/* io */

// output 
int  ochar(int c);
void output(const char *s, int len);
void oflush(void);

// pager hotkeys processor
int process_pager_keys(int ch);

// input api (old flavor)
int  num_in_buf(void);			// vkey_is_ready() / vkey_is_typeahead()
int  wait_input(float f, int bIgnoreBuf);// vkey_poll
int  peek_input(float f, int c);	// vkey_prefetch() / vkey_is_prefeteched
// int  igetch(void);			// -> vkey()
// int  input_isfull();			// -> vkey_is_full()
// void drop_input(void);		// -> vkey_purge()
// void add_io(int fd, int timeout);	// -> vkey_attach / vkey_detach

// new input api
/* nios.c / io.c */
///////// virtual key: convert from cin(fd) -> buffer -> virtual key //////////
// timeout: in milliseconds. 0 for 'do not block' and INFTIM(-1) for 'forever'
///////////////////////////////////////////////////////////////////////////////
// initialization
void vkey_init(void);	     // initialize virtual key system
// key value retrieval
int  vkey(void);	     // receive and block infinite time for next key
int  vkey_peek(void);	     // peek one key from queue (KEY_INCOMPLETE if empty)
// status
int  vkey_poll(int timeout); // poll for timeout milliseconds. return 1 for ready oterwise 0
int  vkey_is_ready(void);    // determine if input buffer is ready for a key input
int  vkey_is_typeahead(void);// check if input buffer has data arrived (maybe not ready yet)
// additional fd to listen
int  vkey_attach(int fd);    // attach (and replace) additional fd to vkey API, returning previous attached fd
int  vkey_detach(void);      // detach any additional fd and return previous attached fd
// input buffer management
int  vkey_is_full(void);     // test if input buffer is full
void vkey_purge(void);		// discard clear all data in input buffer
int  vkey_prefetch(int timeout);// try to fetch data from fd to buffer unless timeout
int  vkey_is_prefetched(char c);// check if c (in raw data form) is already in prefetched buffer
/////////////////////////////////////////////////////////////////////////////
#ifdef  EXP_NIOS
#define USE_NIOS_VKEY
#endif

/* kaede */
char*Ptt_prints(char *str, size_t size, int mode);
void outmsg(const char *msg);
void out_lines(const char *str, int line, int col);
#define HAVE_EXPAND_ESC_STAR
int  expand_esc_star(char *buf, const char *src, int szbuf);

/* lovepaper */
int x_love(void);

/* mail */
int load_mailalert(const char *userid);
int sendalert(const char *userid, int alert);
int sendalert_uid(int uid, int alert);
int mail_muser(const userec_t muser, const char *title, const char *filename);
int mail_log2id(const char *id, const char *title, const char *srcfile, const char *owner, char newmail, char trymove);
int mail_id(const char* id, const char *title, const char *filename, const char *owner);
int m_read(void);
int doforward(const char *direct, const fileheader_t *fh, int mode);
int mail_reply(int ent, fileheader_t *fhdr, const char *direct);
int bsmtp(const char *fpath, const char *title, const char *rcpt, const char *from);
void hold_mail(const char *fpath, const char *receiver, const char *title);
void m_init(void);
int chkmailbox(void);
int mail_man(void);
int m_new(void);
int m_send(void);
int mail_list(void);
int setforward(void);
int m_internet(void);
int mail_mbox(void);
int built_mail_index(void);
int mail_all(void);
int invalidaddr(const char *addr);
int do_send(const char *userid, const char *title, const char *log_source);
int do_innersend(const char *userid, char *mfpath, const char *title, char *newtitle);
void my_send(const char *uident);
void setupmailusage(void);

/* mbbsd */
void show_call_in(int save, int which);
void write_request (int sig);
void mkuserdir(const char *userid);
void log_usies(const char *mode, const char *mesg);
void system_abort(void);
void abort_bbs(int sig) GCC_NORETURN;
void del_distinct(const char *fname, const char *line, int casesensitive);
void add_distinct(const char *fname, const char *line);
void u_exit(const char *mode);
void talk_request(int sig);
int establish_talk_connection(const userinfo_t *uip);
void my_talk(userinfo_t * uin, int fri_stat, char defact);
int query_file_money(const fileheader_t *pfh);

/* menu */
void showtitle(const char *title, const char *mid);
void adbanner(int i);
void adbanner_goodbye();
int main_menu(void);
int admin(void);
int Mail(void);
int Talk(void);
int User(void);
int Xyz(void);
int Play_Play(void);
int Name_Menu(void);
// ZA System
int  ZA_Waiting(void);
int  ZA_Select(void);
void ZA_Enter(void);
void ZA_Drop(void);

#ifdef MERGEBBS
/* merge */
int m_sob(void);
void m_sob_brd(char *bname,char *fromdir);
#endif

/* pager */
int more(const char *fpath, int promptend);
/* piaip's new pager, pmore.c */
int pmore (const char *fpath, int promptend);
int pmore2(const char *fpath, int promptend, void *ctx, 
	int (*key_handler)   (int key, void *ctx),
	int (*footer_handler)(int ratio, int width, void *ctx),
	int (*help_handler)  (int y,   void *ctx));
/* piaip's new telnet, telnet.c */
extern void telnet_init(int do_init_cmd);
extern ssize_t tty_read(unsigned char *buf, size_t max);
extern void telnet_turnoff_client_detect(void);

/* name */
typedef int (*gnc_comp_func)(int, const char*, int);
typedef int (*gnc_perm_func)(int);
typedef char* (*gnc_getname_func)(int);

extern void namecomplete2(const struct Vector *namelist, const char *prompt, char *data);
extern void namecomplete3(const struct Vector *namelist, const char *prompt, char *data, const char *defval);
extern int ShowVector(struct Vector *self, int row, int column, const char *prompt, int idx);
extern void ToggleVector(struct Vector *list, int *recipient, const char *listfile, const char *msg);

void usercomplete(const char *prompt, char *data);
void usercomplete2(const char *prompt, char *data, const char *defval);
int generalnamecomplete(const char *prompt, char *data, int len, size_t nmemb,
		       gnc_comp_func compar, gnc_perm_func permission,
		       gnc_getname_func getname);
int completeboard_compar(int where, const char *str, int len);
int completeboard_permission(int where);
int complete_board_and_group_permission(int where);
char *completeboard_getname(int where);
int completeutmp_compar(int where, const char *str, int len);
int completeutmp_permission(int where);
char *completeutmp_getname(int where);

#define CompleteBoard(MSG,BUF) \
    generalnamecomplete(MSG, BUF, sizeof(BUF), SHM->Bnumber, \
      	&completeboard_compar, &completeboard_permission, \
	&completeboard_getname)
#define CompleteBoardAndGroup(MSG,BUF) \
    generalnamecomplete(MSG, BUF, sizeof(BUF), SHM->Bnumber, \
	&completeboard_compar, &complete_board_and_group_permission, \
	&completeboard_getname)
#define CompleteOnlineUser(MSG,BUF) \
    generalnamecomplete(MSG, BUF, sizeof(BUF), SHM->UTMPnumber, \
	&completeutmp_compar, &completeutmp_permission, \
	&completeutmp_getname)

/* othello */
int othello_main(void);

/* page */
int main_railway(void);

/* read */
void i_read(int cmdmode, const char *direct, void (*dotitle)(), void (*doentry)(), const onekey_t *rcmdlist, int bidcache);
void fixkeep(const char *s, int first);
keeploc_t *getkeep(const char *s, int def_topline, int def_cursline);
int Tagger(time4_t chrono, int recno, int mode);
void EnumTagFhdr(fileheader_t *fhdr, char *direct, int locus);
void UnTagger (int locus);
/* record */
int stampfile_u(char *fpath, fileheader_t *fh);
int stampadir(char *fpath, fileheader_t * fh, int large_set);
int delete_files(const char* dirname, int (*filecheck)(), int record);
void set_safedel_fhdr(fileheader_t *fhdr, const char *newtitle);
#ifdef SAFE_ARTICLE_DELETE
#ifndef _BBS_UTIL_C_
void safe_delete_range(const char *fpath, int id1, int id2);
#endif
int safe_article_delete(int ent, const fileheader_t *fhdr, const char *direct, 
                        const char *newtitle);
int safe_article_delete_range(const char *direct, int from, int to);
#endif
int delete_file(const char *dirname, int size, int ent, int (*filecheck)());
int delete_range(const char *fpath, int id1, int id2);
int search_rec(const char* dirname, int (*filecheck)());
int append_record_forward(char *fpath, fileheader_t *record, int size, 
                          const char *origid);
int get_sum_records(const char* fpath, int size);
int substitute_ref_record(const char* direct, fileheader_t *fhdr, int ent);
int getindex(const char *fpath, fileheader_t *fh, int start);
int rotate_text_logfile(const char *filename, off_t max_size, 
                        float keep_ratio);
int rotate_bin_logfile(const char *filename, off_t record_size, 
                       off_t max_size, float keep_ratio); 
int delete_file_content(const char *direct, const fileheader_t *fh,
                        const char *backup_direct,
                        char *newpath, size_t sznewpath);
#define DELETE_FILE_CONTENT_SUCCESS (0)
#define DELETE_FILE_CONTENT_FAILED  (-1)
#define DELETE_FILE_CONTENT_BACKUP_FAILED  (1)
#define IS_DELETE_FILE_CONTENT_OK(x) ((x) != DELETE_FILE_CONTENT_FAILED)

/* register */
int u_register(void);
int bad_user_id(const char *userid);
int getnewuserid(void);
int setupnewuser(const userec_t *user);
int regform_estimate_queuesize();
void new_register(void);
void check_register(void);
int  check_regmail(char *email); // check and prompt for invalid reason; will str_lower() mail domain.
void delregcodefile(void);

/* reversi */
void reversi(int s, ChessGameMode mode);
int reversi_main(void);
int reversi_personal(void);
int reversi_watch(void);
ChessInfo* reversi_replay(FILE* fp);

/* screen/pfterm (ncurses-like) */
void initscr	(void);
int  resizeterm	(int rows, int cols);
void getyx	(int *py, int *px);
void move	(int y, int x);
void clear	(void);
void clrtoeol	(void);
void clrtobot	(void);
void clrtoln	(int ln);
void newwin	(int nlines, int ncols, int y, int x);
void refresh	(void);
void doupdate	(void);
int  typeahead  (int fd);
void redrawwin	(void);
void scroll	(void);
void rscroll	(void);
int  instr	(char *str);
int  innstr	(char *str, int n);
void scr_dump	(screen_backup_t *buf);
void scr_restore(const screen_backup_t *buf);
// non-curses
void outc(unsigned char ch);
void outs(const char *s);
void outns(const char *str, int n);
void outstr(const char *str); // prepare and print a complete non-ANSI string.
int  inansistr(char *str, int n);
void move_ansi(int y, int x);
void getyx_ansi(int *py, int *px);
void region_scroll_up(int top, int bottom);

#ifndef USE_PFTERM
# define SOLVE_ANSI_CACHE() {}
#else  // !USE_PFTERM
# define SOLVE_ANSI_CACHE() { outs(" \b"); }
#endif // !USE_PFTERM

#define HAVE_GRAYOUT
void grayout(int start, int end, int level);

/* AIDS */
typedef uint64_t aidu_t;
aidu_t fn2aidu(char *fn);
char *aidu2aidc(char *buf, aidu_t aidu);
char *aidu2fn(char *buf, aidu_t aidu);
aidu_t aidc2aidu(char *aidc);
int search_aidu(char *bfile, aidu_t aidu);
/* end of AIDS */

/* stuff */
#define isprint2(ch) ((ch & 0x80) || isprint(ch))
#define not_alpha(ch) (ch < 'A' || (ch > 'Z' && ch < 'a') || ch > 'z')
#define not_alnum(ch) (ch < '0' || (ch > '9' && ch < 'A') || (ch > 'Z' && ch < 'a') || ch > 'z')
#define pressanykey() vmsg(NULL)
void setuserfile(char *buf, const char *fname);
void setbdir(char *buf, const char *boardname);
void setaidfile(char *buf, const char *bn, aidu_t aidu);
void setcalfile(char *buf, char *userid);
int  log_user(const char *fmt, ...) GCC_CHECK_FORMAT(1,2);
void syncnow(void);
void wait_penalty(int sec);
void cursor_clear(int row, int column);
void cursor_show(int row, int column);
int  cursor_key(int row, int column);
void printdash(const char *mesg, int msglen);
int  show_file(const char *filename, int y, int lines, int mode);
int  show_80x24_screen(const char *filename);
void show_help(const char * const helptext[]);
void show_helpfile(const char * helpfile);
int  search_num(int ch, int max);
char*subject(char *title);
int  str_checksum(const char *str);
int  userid_is_BM(const char *userid, const char *list);
int  is_uBM(const char *list, const char *id);
time4_t  gettime(int line, time4_t dt, const char* head);
unsigned DBCS_StringHash(const char *s);
// vgets/getdata utilities
int  getdata(int line, int col, const char *prompt, char *buf, int len, int echo);
int  getdata_str(int line, int col, const char *prompt, char *buf, int len, int echo, const char *defaultstr);
int  getdata_buf(int line, int col, const char *prompt, char *buf, int len, int echo);

#ifndef CRITICAL_MEMORY
    #define MALLOC(p)  malloc(p)
    #define FREE(p)    free(p)
#else
    void *MALLOC(int size);
    void FREE(void *ptr);
#endif

/* syspost */
int post_msg(const char* bname, const char* title, const char *msg, const char* author);
int post_file(const char *bname, const char *title, const char *filename, const char *author);
void post_newboard(const char *bgroup, const char *bname, const char *bms);
void post_violatelaw(const char *crime, const char *police, const char *reason, const char *result);
void post_change_perm(int oldperm, int newperm, const char *sysopid, const char *userid);
void post_policelog(const char *bname, const char *atitle, const char *action, const char *reason, const int toggle);

/* talk */
#define iswritable(uentp)    \
        (iswritable_stat(uentp, friend_stat(currutmp, uentp)))
#define isvisible(me, uentp) \
        (isvisible_stat(currutmp, uentp, friend_stat(me, uentp)))
     
int iswritable_stat(const userinfo_t *uentp, int fri_stat);
int isvisible_stat(const userinfo_t * me, const userinfo_t * uentp, int fri_stat);
int cmpwatermtime(const void *a, const void *b);
void getmessage(msgque_t msg);
void my_write2(void);
int t_idle(void);
void check_water_init(void);
const char *modestring(const userinfo_t * uentp, int simple);
int t_users(void);
int my_write(pid_t pid, const char *hint, const char *id, int flag, userinfo_t *);
void t_display_new(void);
void talkreply(void);
int t_pager(void);
int t_query(void);
int t_qchicken(void);
int t_talk(void);
int t_chat(void);
int t_display(void);
int my_query(const char *uident);
int logout_friend_online(userinfo_t*);
void login_friend_online(int do_login);
int isvisible_uid(int tuid);
int friend_stat(const userinfo_t *me, const userinfo_t * ui);
int call_in(const userinfo_t *uentp, int fri_stat);
int make_connection_to_somebody(userinfo_t *uin, int timeout);
int query_online(const char *userid);

/* term */
void init_tty(void);
int  term_init(void);
void term_resize(int w, int h);
void bell(void);

/* timecap (time capsule) */
int timecapsule_add_revision(const char *filename);
int timecapsule_get_max_revision_number(const char *filename);
int timecapsule_get_max_archive_number(const char *filename, size_t szrefblob);
int timecapsule_archive(const char *filename, const void *ref, size_t szref);
int timecapsule_get_by_revision(const char *filename, int rev, char *rev_path,
                                size_t sz_rev_path);
int timecapsule_archive_new_revision(const char *filename,
                                     const void *ref, size_t szref,
                                     char *archived_path, size_t sz_archived_path);
int timecapsule_get_archive_by_index(const char *filename, int idx,
                                     void *refblob, size_t szrefblob);
int timecapsule_get_archive_blobs(const char *filename, int idx, int nblobs,
                                  void *blobsptr, size_t szblob);

/* ordersong */
int  ordersong(void);
int  topsong(void);

/* user */
int kill_user(int num, const char *userid);
int u_editcalendar(void);
int u_set_mind();
void user_display(const userec_t *u, int real);
int isvalidemail(char *email);
void uinfo_query(const char *uid, int real, int unum);
int showsignature(char *fname, int *j, SigInfo *psi);
int u_cancelbadpost(void);
void kick_all(const char *user);
void violate_law(userec_t * u, int unum);
void mail_violatelaw(const char* crime, const char* police, const char* reason, const char* result);
int u_info(void);
void showplans(const char *uid);
void showplans_userec(userec_t *u);
int u_loginview(void);
int u_ansi(void);
int u_editplan(void);
int u_editsig(void);
int u_cloak(void);
int u_list(void);

/* vote */
void b_suckinfile(FILE *fp, const char *fname);
void b_suckinfile_invis(FILE * fp, const char *fname, const char *boardname);
int b_results(void);
int b_vote(void);
int b_vote_maintain(void);
void auto_close_polls(void);

/* voteboard */
int do_voteboard(int);
void do_voteboardreply(const fileheader_t *fhdr);

/* xyz */
int m_sysop(void);
int x_boardman(void);
int x_login(void);
int x_week(void);
int x_issue(void);
int x_today(void);
int x_yesterday(void);
int x_user100(void);
int x_birth(void);
int x_history(void);
int x_weather(void);
int x_stock(void);
int x_mrtmap(void);
int Goodbye(void);

/* BBS-LUA */
int bbslua(const char *fpath);
int bbslua_isHeader(const char *ps, const char *pe);


/* recycle */
// public
int RcyAddFile(const fileheader_t *fhdr, int bid, const char *fpath);
int RcyAddDir (const fileheader_t *fhdr, int bid, const char *direct);
int RcyRecycleBin(void);

/* passwd */
int  initcuser		(const char *userid);
void passwd_force_update(int flag);
int  passwd_sync_update (int num, userec_t * buf);
int  passwd_sync_query  (int num, userec_t * buf);

// current user help utilities
#define reload_money()  pwcuReloadMoney()
#define demoney(money)  pwcuDeMoney(money)
int pwcuBitEnableLevel	(unsigned int mask);
int pwcuBitDisableLevel	(unsigned int mask);
int pwcuIncNumPost	(void);
int pwcuDecNumPost	(void);
int pwcuSetGoodPost	(unsigned int newgp);
int pwcuViolateLaw	(void);
int pwcuSaveViolateLaw	(void);
int pwcuCancelBadpost	(void);
int pwcuAddExMailBox	(int m);
int pwcuToggleOutMail	(void);
int pwcuSetLoginView	(unsigned int bits);
int pwcuSetLastSongTime (time4_t clk);
int pwcuSetMyAngel	(const char *angel_uid);
int pwcuSetNickname	(const char *nickname);
int pwcuChessResult	(int sigType, ChessGameResult);
int pwcuSetChessEloRating(uint16_t elo_rating);
int pwcuSaveUserFlags	(void);
int pwcuRegCompleteJustify    (const char *justify);
int pwcuRegSetTemporaryJustify(const char *justify, const char *email);
int pwcuRegisterSetInfo (const char *rname,
			 const char *addr,
			 const char *career,
			 const char *phone,
			 const char *email,
			 int         mobile,
			 uint8_t     year,
			 uint8_t     month,
			 uint8_t     day,
			 uint8_t     is_foreign);

// non-important based variables (only save on exit)
int pwcuSetSignature	(unsigned char newsig);
int pwcuSetPagerUIType	(unsigned int  uitype);
int pwcuToggleSortBoard (void);
int pwcuToggleFriendList(void);
int pwcuToggleUserFlag	(unsigned int mask);	// not saved until pwcuSaveUserFlags
int pwcuToggleUserFlag2	(unsigned int mask);	// not saved until pwcuSaveUserFlags

// session management
int pwcuLoginSave	(void);
int pwcuExitSave	(void);
int pwcuReload		(void);
int pwcuReloadMoney	(void);
int pwcuDeMoney		(int money);

// initialization
void pwcuInitZero	(void);
void pwcuInitGuestPerm	(void);
void pwcuInitGuestInfo	(void);
int  pwcuInitAdminPerm	(void);

/* calendar */
int calendar(void);
int ParseDate(const char *date, int *year, int *month, int *day);
int ParseDateTime(const char *date, int *year, int *month, int *day,
		  int *hour, int *min, int *sec);

int verify_captcha(const char *reason);

#endif
