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
