#include "user_test.h"

TEST_F(UserTest, KillUser) {
  // load SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[user_test2] after initcuser\n");

  userec_t user = {};
  int uid = passwd_load_user("SYSOP10\0", &user);
  EXPECT_EQ(uid, 26);

  char homepath[8192] = {};
  sethomepath(homepath, "SYSOP10\0");
  fprintf(stderr, "[user_test2] homepath: %s\n", homepath);
  EXPECT_NE(dashd(homepath), 0);

  int ret = kill_user(uid, "SYSOP10\0");

  EXPECT_EQ(ret, 0);

  uid = passwd_load_user("SYSOP10\0", &user);
  EXPECT_EQ(uid, -1);

  EXPECT_EQ(dashd(homepath), 0);
}
