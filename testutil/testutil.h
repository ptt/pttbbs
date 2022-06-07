#pragma once

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define TEST_BUFFER_SIZE 8192

  // load uhash
  void load_uhash();

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
