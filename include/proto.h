/* $Id$ */
#ifndef INCLUDE_PROTO_H
#define INCLUDE_PROTO_H

#ifdef __GNUC__
#define GCC_CHECK_FORMAT(a,b) __attribute__ ((format (printf, a, b)))
#else
#define GCC_CHECK_FORMAT(a,b)
#endif

/* admin */
int m_loginmsg();
int m_mod_board(char *bname);
int m_newbrd(int recover);
int scan_register_form(char *regfile, int automode, int neednum);
int m_user();
int search_user_bypwd();
int search_user_bybakpwd();
int m_board();
int m_register();
int cat_register();
unsigned int setperms(unsigned int pbits, char *pstring[]);
void setup_man(boardheader_t * board);
int make_symbolic_link(char *bname, int gid);
int make_symbolic_link_interactively(int gid);

/* announce */
int a_menu(char *maintitle, char *path, int lastlevel);
void a_copyitem(char* fpath, char* title, char* owner, int mode);
int Announce();
void gem(char* maintitle, item_t* path, int update);
#ifdef BLOG
void BlogMain(int);
#endif

/* args */
void initsetproctitle(int argc, char **argv, char **envp);
void setproctitle(const char* format, ...) GCC_CHECK_FORMAT(1,2);

/* assess */
int inc_goodpost(int uid, int num);
int inc_badpost(int uid, int num);
int inc_goodsale(int uid, int num);
int inc_badsale(int uid, int num);
void set_assess(int uid, unsigned char num, int type);

/* bbcall */
int main_bbcall();

/* bbs */
void make_blist();
int invalid_brdname(char *brd);
int del_range(int ent, fileheader_t *fhdr, char *direct);
int cmpfowner(fileheader_t *fhdr);
int b_note_edit_bname(int bid);
int Read();
int CheckPostPerm(void);
void anticrosspost();
int Select();
void do_reply_title(int row, char *title);
int cmpfmode(fileheader_t *fhdr);
int cmpfilename(fileheader_t *fhdr);
int getindex(char *fpath, char *fname, int size);
void outgo_post(fileheader_t *fh, char *board);
int edit_title(int ent, fileheader_t *fhdr, char *direct);
int whereami(int ent, fileheader_t *fhdr, char *direct);
void set_board();
int do_post();
void ReadSelect();
int save_violatelaw();
int board_select();
int board_etc();
int board_digest();

/* board */
#define setutmpbid(bid) currutmp->brc_id=bid;
int HasPerm(boardheader_t *bptr);
int New();
int Boards();
int root_board();
void save_brdbuf(void);
void init_brdbuf(void);
int validboard(int bid);
#ifdef CRITICAL_MEMORY
void sigfree(int);
#endif

/* brc */
int brc_initialize();
void brc_finalize();
int brc_unread(const char *fname, int bnum, const time_t *blist);
int brc_unread_time(time_t ftime, int bnum, const time_t *blist);
int brc_initial_board(const char *boardname);
void brc_update();
int brc_read_record(int bid, int *num, time_t *list);
time_t * brc_find_record(int bid, int *num);
void brc_trunc(int bid, time_t ftime);
void brc_addlist(const char* fname);

/* cache */
int moneyof(int uid);
int getuser(char *userid);
void setuserid(int num, char *userid);
int searchuser(char *userid);
int getbnum(const char *bname);
void reset_board(int bid);
void touch_boards();
void addbrd_touchcache();
void setapath(char *buf, char *boardname);
void setutmpmode(unsigned int mode);
void setadir(char *buf, char *path);
boardheader_t *getbcache(int bid);
int apply_boards(int (*func)(boardheader_t *));
int haspostperm(char *bname);
void inbtotal(int bid, int add);
void setbtotal(int bid);
void setbottomtotal(int bid);
unsigned int safe_sleep(unsigned int seconds);
int apply_ulist(int (*fptr)(userinfo_t *));
userinfo_t *search_ulistn(int uid, int unum);
void purge_utmp(userinfo_t *uentp);
userinfo_t *search_ulist(int uid);
int count_multi();
void resolve_utmp();
void attach_uhash();
void getnewutmpent(userinfo_t *up);
void resolve_garbage();
void resolve_boards();
void resolve_fcache();
void sem_init(int semkey,int *semid);
void sem_lock(int op,int semid);
int count_ulist();
char *u_namearray(char buf[][IDLEN + 1], int *pnum, char *tag);
char *getuserid(int num);
int searchnewuser(int mode);
int count_logins(int uid, int show);
void remove_from_uhash(int n);
void add_to_uhash(int n, char *id);
int setumoney(int uid, int money);
int getbtotal(int bid);
userinfo_t *search_ulist_pid(int pid);
userinfo_t *search_ulist_userid(char *userid);
int moneyof(int uid);
void hbflreload(int bid);
int hbflcheck(int bid, int uid);
int updatemdcache(const char *cpath, const char *fpath);
char *cachepath(const char *fpath);
int mdcacheopen(char *fpath);
void touchdircache(int bid);
int get_fileheader_cache(int bid, char *direct, fileheader_t *headers, 
			 int recbase, int nlines);
void *attach_shm(int shmkey, int shmsize);
void attach_SHM(void);
void sort_utmp(void);
int is_BM_cache(int);
void buildBMcache(int);
void reload_bcache(void);

/* cal */
int give_tax(int money);
int vice(int money, char* item);
int inumoney(char *tuser, int money);
int cal();
#define reload_money()  cuser->money=moneyof(usernum)
int demoney(int money);
int deumoney(int uid, int money);
int lockutmpmode(int unmode, int state);
int unlockutmpmode();
int p_touch_boards();
int x_file();
int give_money();
int p_sysinfo();
int p_give();
int p_cloak();
int p_from();
int ordersong();
int p_exmail();
void mail_redenvelop(char* from, char* to, int money, char mode);

/* card */
int g_card_jack();
int g_ten_helf();
int card_99();

/* chat */
int t_chat();

/* chc */
void chc(int s, int mode);
int chc_main(void);
int chc_personal(void);
int chc_watch(void);

/* chicken */
int show_file(char *filename, int y, int lines, int mode);
void ch_buyitem(int money, char *picture, int *item, int haveticket);
int chicken_main();
int chickenpk(int fd);
void time_diff(chicken_t *thechicken);
int isdeadth(chicken_t *thechicken);
void show_chicken_data(chicken_t *thechicken, chicken_t *pkchicken);
int reload_chicken();

/* dark */
int main_dark(int fd,userinfo_t *uin);

/* dice */
int IsSNum(char *a);
int dice_main();
int IsNum(char *a, int n);

/* edit */
int vedit(char *fpath, int saveheader, int *islocal);
void write_header(FILE *fp);
void addsignature(FILE *fp, int ifuseanony);
void auto_backup();
void restore_backup();
char *ask_tmpbuf(int y);
char *strcasestr(const char* big, const char* little);
void editlock(char *fpath);
void editunlock(char *fpath);
int iseditlocking(char *fpath, char *action);

/* fav */
void fav_set_old_folder(fav_t *fp);
int get_data_number(fav_t *fp);
int get_current_fav_level(void);
fav_t *get_current_fav(void);
int get_item_type(fav_type_t *ft);
char *get_item_title(fav_type_t *ft);
char *get_folder_title(int fid);
void set_attr(fav_type_t *ft, int bit, char bool);
void fav_sort_by_name(void);
void fav_sort_by_class(void);
int fav_load(void);
int fav_save(void);
void fav_remove_item(short id, char type);
fav_type_t *getadmtag(short bid);
fav_type_t *getboard(short bid);
fav_type_t *getfolder(short fid);
char getbrdattr(short bid);
time_t getbrdtime(short bid);
void setbrdtime(short bid, time_t t);
int fav_getid(fav_type_t *ft);
void fav_tag(short id, char type, char bool);
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
void fav_folder_in(short fid);
void fav_folder_out(void);
void fav_free(void);
int fav_v3_to_v4(void);
int is_set_attr(fav_type_t *ft, char bit);
void fav_cleanup(void);
void fav_clean_invisible(void);
char current_fav_at_root(void);
fav_t *get_fav_folder(fav_type_t *ft);
fav_t *get_fav_root(void);
void updatenewfav(int mode);
void subscribe_newfav(void);

/* friend */
void friend_edit(int type);
void friend_load();
int t_override();
int t_reject();
void friend_add(char *uident, int type, char *des);
void friend_delete(char *uident, int type);
void friend_special(void);
void setfriendfile(char *fpath, int type);

/* gamble */
int ticket_main();
int openticket(int bid);
int ticket(int bid);

/* gomo */
int gomoku(int fd);
int getstyle(int x, int y, int color, int limit);

/* guess */
int guess_main();

/* indict */
int x_dict();
int use_dict(char *dict,char *database);

/* convert */
void set_converting_type(int which);

/* io */
int getdata(int line, int col, char *prompt, char *buf, int len, int echo);
int igetch();
int getdata_str(int line, int col, char *prompt, char *buf, int len, int echo, char *defaultstr);
int getdata_buf(int line, int col, char *prompt, char *buf, int len, int echo);
int i_get_key();
void add_io(int fd, int timeout);
int igetkey();
void oflush();
int strip_ansi(char *buf, char *str, int mode);
int oldgetdata(int line, int col, char *prompt, char *buf, int len, int echo);
void output(char *s, int len);
void init_alarm();
int num_in_buf();
int ochar(int c);
int rget(int x,char *prompt);
char getans(char *prompt);

/* kaede */
int Rename(char* src, char* dst);
int Copy(char *src, char *dst);
int Link(char* src, char* dst);
char *Ptt_prints(char *str, int mode);
char *my_ctime(const time_t *t, char *ans, int len);

/* lovepaper */
int x_love();

/* mail */
int load_mailalert(char *userid);
int mail_muser(userec_t muser, char *title, char *filename);
int mail_id(char* id, char *title, char *filename, char *owner);
int m_read();
int doforward(char *direct, fileheader_t *fh, int mode);
int mail_reply(int ent, fileheader_t *fhdr, char *direct);
int bsmtp(char *fpath, char *title, char *rcpt, int method);
void hold_mail(char *fpath, char *receiver);
int chkmail(int rechk);
void m_init();
int chkmailbox();
int mail_man();
int m_new();
int m_send();
int mail_list();
int setforward();
int m_internet();
int mail_mbox();
int built_mail_index();
int mail_all();
int invalidaddr(char *addr);
int do_send(char *userid, char *title);
void my_send(char *uident);

/* mbbsd */
void show_call_in(int save, int which);
void write_request (int sig);
void log_usies(char *mode, char *mesg);
void log_user(char *msg);
void system_abort();
void abort_bbs(int sig);
void del_distinct(char *fname, char *line);
void add_distinct(char *fname, char *line);
void show_last_call_in(int save);
void u_exit(char *mode);
void talk_request(int sig);
int reply_connection_request(userinfo_t *uip);
void my_talk(userinfo_t * uin, int fri_stat, char defact);

/* menu */
void showtitle(char *title, char *mid);
int egetch();
void movie(int i);
void domenu(int cmdmode, char *cmdtitle, int cmd, commands_t cmdtable[]);
int admin(void);
int Mail(void);
int Talk(void);
int User(void);
int Xyz(void);
int Play_Play(void);
int Name_Menu(void);

/* more */
int more(char *fpath, int promptend);

/* name */
void usercomplete(char *prompt, char *data);
void namecomplete(char *prompt, char *data);
void AddNameList(char *name);
void FreeNameList();
void CreateNameList();
int chkstr(char *otag, char *tag, char *name);
int InNameList(char *name);
void ShowNameList(int row, int column, char *prompt);
int RemoveNameList(char *name);
void ToggleNameList(int *reciper, char *listfile, char *msg);
void allboardcomplete(char *prompt, char *data, int len);
int generalnamecomplete(char *prompt, char *data, int len, size_t nmemb,
		       int (*compar)(int, char *, int),
		       int (*permission)(int), char* (*getname)(int));
int completeboard_compar(int where, char *str, int len);
int completeboard_permission(int where);
char *completeboard_getname(int where);
int completeutmp_compar(int where, char *str, int len);
int completeutmp_permission(int where);
char *completeutmp_getname(int where);

/* osdep */
int cpuload(char *str);
double swapused(long *total, long *used);

/* othello */
int othello_main();

/* page */
int main_railway();

/* read */
void z_download(char *fpath);
void i_read(int cmdmode, char *direct, void (*dotitle)(), void (*doentry)(), onekey_t *rcmdlist, int bidcache);
void fixkeep(char *s, int first);
keeploc_t *getkeep(char *s, int def_topline, int def_cursline);
int Tagger(time_t chrono, int recno, int mode);
void EnumTagFhdr(fileheader_t *fhdr, char *direct, int locus);
void UnTagger (int locus);
/* record */
int substitute_record(char *fpath, void *rptr, int size, int id);
int lock_substitute_record(char *fpath, void *rptr, int size, int id, int);
int get_record(char *fpath, void *rptr, int size, int id);
int get_record_keep(char *fpath, void *rptr, int size, int id, int *fd);
void prints(char *fmt, ...) GCC_CHECK_FORMAT(1,2);
int append_record(char *fpath, fileheader_t *record, int size);
int stampfile(char *fpath, fileheader_t *fh);
void stampdir(char *fpath, fileheader_t *fh);
int get_num_records(char *fpath, int size);
int get_records(char *fpath, void *rptr, int size, int id, int number);
void stamplink(char *fpath, fileheader_t *fh);
int delete_record(char fpath[], int size, int id);
int delete_files(char* dirname, int (*filecheck)(), int record);
#ifdef SAFE_ARTICLE_DELETE
int safe_article_delete(int ent, fileheader_t *fhdr, char *direct);
int safe_article_delete_range(char *direct, int from, int to);
#endif
int delete_file(char *dirname, int size, int ent, int (*filecheck)());
int delete_range(char *fpath, int id1, int id2);
int apply_record(char *fpath, int (*fptr)(), int size);
int search_rec(char* dirname, int (*filecheck)());
int append_record_forward(char *fpath, fileheader_t *record, int size);
int get_sum_records(char* fpath, int size);

/* register */
int getnewuserid();
int bad_user_id(char *userid);
void new_register();
int checkpasswd(char *passwd, char *test);
void check_register();
char *genpasswd(char *pw);

/* screen */
void move(int y, int x);
void outs(char *str);
void clrtoeol();
void clear();
void refresh();
void clrtobot();
void mprints(int y, int x, char *str);
void outmsg(char *msg);
void region_scroll_up(int top, int bottom);
void outc(unsigned char ch);
void redoscr();
void clrtoline(int line);
void standout();
void standend();
int edit_outs(char *text);
void outch(unsigned char c);
void rscroll();
void scroll();
void getyx(int *y, int *x);
void initscr();
void out_lines(char *str, int line);

/* stuff */
time_t gettime(int line, time_t dt, char* head);
void setcalfile(char *buf, char *userid);
void stand_title(char *title);
void pressanykey();
int  vmsg (const char *fmt,...) GCC_CHECK_FORMAT(1,2);
void trim(char *buf);
void bell();
void setbpath(char *buf, char *boardname);
int dashf(char *fname);
void sethomepath(char *buf, char *userid);
void sethomedir(char *buf, char *userid);
char *Cdate(time_t *clock);
void sethomefile(char *buf, char *userid, char *fname);
int log_file(char *filename, char *buf, int flags);
void str_lower(char *t, char *s);
int strstr_lower(char *str, char *tag);
int cursor_key(int row, int column);
int search_num(int ch, int max);
void setuserfile(char *buf, char *fname);
int is_BM(char *list);
long dasht(char *fname);
int dashd(char *fname);
int invalid_pname(char *str);
void setbdir(char *buf, char *boardname);
void setbfile(char *buf, char *boardname, char *fname);
void setbnfile(char *buf, char *boardname, char *fname, int n);
int dashl(char *fname);
char *subject(char *title);
int not_alnum(char ch);
void setdirpath(char *buf, char *direct, char *fname);
int str_checksum(char *str);
void show_help(char *helptext[]);
int belong(char *filelist, char *key);
char *Cdatedate(time_t *clock);
int isprint2(char ch);
void sethomeman(char *buf, char *userid);
off_t dashs(char *fname);
void cursor_clear(int row, int column);
void cursor_show(int row, int column);
void printdash(char *mesg);
char *Cdatelite(time_t *clock);
int not_alpha(char ch);
int valid_ident(char *ident);
int userid_is_BM(char *userid, char *list);
int is_uBM(char *list, char *id);
inline int *intbsearch(int key, int *base0, int nmemb);
int qsort_intcompar(const void *a, const void *b);
#ifndef CRITICAL_MEMORY
    #define MALLOC(p)  malloc(p)
    #define FREE(p)    free(p)
#else
    void *MALLOC(int size);
    void FREE(void *ptr);
#endif
#ifdef OUTTACACHE
int tobind(int port);
int toconnect(char *host, int port);
int toread(int fd, void *buf, int len);
int towrite(int fd, void *buf, int len);
#endif

/* syspost */
int post_msg(char* bname, char* title, char *msg, char* author);
int post_file(char *bname, char *title, char *filename, char *author);
void post_newboard(char *bgroup, char *bname, char *bms);
void post_violatelaw(char *crime, char *police, char *reason, char *result);
void post_change_perm(int oldperm, int newperm, char *sysopid, char *userid);
void give_money_post(char *userid, int money); 

/* talk */
#define iswritable(uentp)    \
        (iswritable_stat(uentp, friend_stat(currutmp, uentp)))
#define isvisible(me, uentp) \
        (isvisible_stat(currutmp, uentp, friend_stat(me, uentp)))
     
int iswritable_stat(userinfo_t *uentp, int fri_stat);
int isvisible_stat(userinfo_t * me, userinfo_t * uentp, int fri_stat);
int cmpwatermtime(const void *a, const void *b);
//void water_scr(water_t *tw, int which, char type);
void getmessage(msgque_t msg);
void my_write2(void);
int t_idle();
char *modestring(userinfo_t * uentp, int simple);
int t_users();
int cmpuids(int uid, userinfo_t * urec);
int my_write(pid_t pid, char *hint, char *id, int flag, userinfo_t *);
void t_display_new();
void talkreply();
int t_monitor();
int t_pager();
int t_query();
int t_qchicken();
int t_talk();
int t_display();
int my_query(char *uident);
int logout_friend_online();
void login_friend_online();
int isvisible_uid(int tuid);
int friend_stat(userinfo_t *me, userinfo_t * ui);
int call_in(userinfo_t *uentp, int fri_stat);
int make_connection_to_somebody(userinfo_t *uin, int timeout);

/* tmpjack */
int reg_barbq();
int p_ticket_main();
int j_ticket_main();

/* term */
void init_tty();
int term_init();
void save_cursor();
void restore_cursor();
void do_move(int destcol, int destline);
void scroll_forward();
void change_scroll_range(int top, int bottom);

/* topsong */
void sortsong();
int topsong();

/* user */
int kill_user(int num);
int u_editcalendar();
void user_display(userec_t *u, int real);
void uinfo_query(userec_t *u, int real, int unum);
int showsignature(char *fname, int *j);
void mail_violatelaw(char* crime, char* police, char* reason, char* result);
void showplans(char *uid);
int u_info();
int u_loginview();
int u_ansi();
int u_editplan();
int u_editsig();
int u_switchproverb();
int u_editproverb();
int u_cloak();
int u_register();
int u_list();

/* vote */
void b_suckinfile(FILE *fp, char *fname);
int b_results();
int b_vote();
int b_vote_maintain();
int b_closepolls();

/* vice */
int vice_main();

/* voteboard */
int do_voteboard();
void do_voteboardreply(fileheader_t *fhdr);

/* xyz */
int m_sysop();
int x_boardman();
int x_note();
int x_login();
int x_week();
int x_issue();
int x_today();
int x_yesterday();
int x_user100();
int x_birth();
int x_90();
int x_89();
int x_88();
int x_87();
int x_86();
int x_history();
int x_weather();
int x_stock();
int x_mrtmap();
int note();
int Goodbye();

/* toolkit */
unsigned StringHash(unsigned char *s);

/* passwd */
int passwd_init();
int passwd_update(int num, userec_t *buf);
int passwd_index_update(int num, userec_t *buf);
int passwd_query(int num, userec_t *buf);
int passwd_index_query(int num, userec_t *buf);
int passwd_apply(int (*fptr)(int, userec_t *));
void passwd_lock();
void passwd_unlock();
int passwd_update_money(int num);
int passwd_query_money(int num);
int initcuser(char *userid);
int freecuser();


/* calendar */
int calendar();

/* util */
void touchbtotal(int bid);

/* util_cache.c */
void reload_pttcache(void);

#endif
