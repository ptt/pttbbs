#include "register_test.h"

TEST_F(RegisterTest, Setupnewuser) {
  char buf[20] = {};
  sprintf(buf, "  y\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  // load SYSOP2
  //load_current_user("SYSOP2");
  //fprintf(stderr, "[register_test3] after load current_user\n");

  now = 1234567890;
  userec_t user = {};
  user.version = PASSWD_VERSION;
  user.userlevel = PERM_DEFAULT;
  user.uflag = UF_BRDSORT | UF_ADBANNER | UF_CURSOR_ASCII;
  user.firstlogin = user.lastlogin = now;
  user.pager = PAGER_ON;
  user.numlogindays = 1;
  sprintf(user.lasthost, "127.0.0.1");
  sprintf(user.userid, "testnew0");
  user.uflag |= UF_DBCS_AWARE | UF_DBCS_DROP_REPEAT;
  sprintf(user.email, "test@test.edu");

  char passbuf[] = "testpass\0";
  strlcpy(user.passwd, genpasswd(passbuf), PASSLEN);

  sprintf(user.nickname, "testnick0");
  sprintf(user.realname, "testreal0");
  sprintf(user.career, "testcareer0");
  sprintf(user.address, "testaddr0");
  user.over_18 = 1;

  int uid = dosearchuser("testnew0", NULL);
  EXPECT_EQ(uid, 0);

  setupnewuser(&user);

  uid = dosearchuser("testnew0", NULL);
  EXPECT_EQ(uid, 2);
}
