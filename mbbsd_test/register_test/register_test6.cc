#include "register_test.h"

TEST_F(RegisterTest, UserHasEmail) {
  char buf[20] = {};
  sprintf(buf, "  y\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  // load SYSOP2
  load_current_user("SYSOP2");
  fprintf(stderr, "[register_test6] after load current_user\n");

  userec_t user = {};
  strcpy(user.email, "TEST@TEST.EDU");

  bool ret = user_has_email(&user);
  EXPECT_EQ(ret, true);
  EXPECT_STREQ(user.email, "TEST@TEST.EDU\0");

  strcpy(user.email, "TEST.edu");
  ret = user_has_email(&user);
  EXPECT_EQ(ret, false);
  EXPECT_STREQ(user.email, "TEST.edu\0");
}
