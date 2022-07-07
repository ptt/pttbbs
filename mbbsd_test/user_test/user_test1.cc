#include "user_test.h"

TEST_F(UserTest, BanUsermail) {
  char buf[8192] = {};

  initcuser("SYSOP10\0");
  sprintf(buf, "y\r\ntest check10\r\ntestcareer10\r\ntestaddr1234567\xA5\xAB" "10\r\ny\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  EXPECT_EQ(cuser.userlevel & PERM_LOGINOK, 0);

  u_manual_verification();

  sprintf(buf, "e\r\ny  \r\n  ");
  put_vin((unsigned char *)buf, strlen(buf));

  m_register();

  refresh();
  reset_oflush_buf();

  initcuser("SYSOP10\0");

  EXPECT_EQ(cuser.userlevel & PERM_LOGINOK, PERM_LOGINOK);

  initcuser("SYSOP2\0");

  refresh();
  reset_oflush_buf();

  userec_t user = {};
  int uid = passwd_load_user("SYSOP10\0", &user);
  EXPECT_EQ(uid, 26);
  strcpy(user.email, "TEST@TEST.EDU");

  ban_usermail(&user, NULL);

  refresh();

  char buf2[8192] = {};

  off_t sz = dashs("etc/banemail");

  int fd = open("etc/banemail", O_RDONLY);
  read(fd, buf2, sz);
  close(fd);

  EXPECT_THAT(buf2, testing::HasSubstr("\n# SYSOP10: (\xAF\xB8\xAA\xF8\xA7\xD1\xA4" "F\xA5\xB4) (by SYSOP2)\nATEST@TEST.EDU")); //站長忘了打
}
