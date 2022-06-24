#pragma once

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "mbbsd_s.h"

#define TEST_BUFFER_SIZE 8192

  //admin.c
  int upgrade_passwd(userec_t *puser);

  //mbbsd.c
  void do_aloha(const char *hello);
  void check_bad_clients();
  bool parse_argv(int argc, char *argv[], struct ProgramOption *option);

  //register.c
#define FN_NOTIN_WHITELIST_NOTICE "etc/whitemail.notice"
#define FN_REGISTER_LOG  "register.log"
#define FN_REJECT_NOTIFY "justify.reject"

#define FN_REGFORM	"regform"	// registration form in user home
#define FN_REGFORM_LOG	"regform.log"	// regform history in user home
#define FN_REQLIST	"reg.wait"	// request list file, in global directory (replacing fn_register)

#define FN_REG_METHODS	"etc/reg.methods"

  int check_and_expire_account(int uid, const userec_t * urec, int expireRange);
  int query_adbanner_usong_pref_changed(const userec_t *u, char force_yn);
  void u_manual_verification(void);

  void regform2_validate_single(const char *xuid);
  int regform2_validate_page(int dryrun);

  // load uhash
  void load_uhash();

  // reset_SHM
  void reset_SHM();

  // check SHM
  void check_SHM();

  // setup root link
  int setup_root_link(char *bbshome);

  // mbbsd/acl
  int unban_user_for_board(const char *user, const char *board);
  int get_user_banned_status_by_board(const char *user, const char *board,
                                      size_t szreason, char *reason);

#ifdef __cplusplus
}
#endif // __cplusplus
