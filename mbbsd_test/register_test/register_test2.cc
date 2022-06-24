#include "register_test.h"

TEST_F(RegisterTest, CheckAndExpireAccount) {
  char buf[20] = {};
  sprintf(buf, "  y\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  // load SYSOP2
  load_current_user("SYSOP2\0");
  fprintf(stderr, "[register_test2] after load current_user\n");

  int uid = searchuser("test0\0", NULL);
  EXPECT_GT(uid, 0);

  userec_t user;
  passwd_sync_query(uid, &user);

  now = 1656000000; //2022-06-23 16:00:00 UTC

  int ret = check_and_expire_account(uid, &user, 0);
  EXPECT_LT(ret, 0);
  fprintf(stderr, "[register_test2] ret: %d\n", ret);

  uid = searchuser("test0\0", NULL);
  EXPECT_EQ(uid, 0);

  now = 0;
}
