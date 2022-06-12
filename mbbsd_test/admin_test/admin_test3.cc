#include "admin_test.h"

TEST_F(AdminTest, UpgradePasswd) {
  userec_t user = {};
  int ret = upgrade_passwd(&user);
  EXPECT_EQ(ret, 1);

  user.version = PASSWD_VERSION;
  ret = upgrade_passwd(&user);
  EXPECT_EQ(ret, 1);
}
