#include "user_test.h"

TEST_F(UserTest, ShowplansUserec) {
  // load SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[user_test12] after initcuser\n");

  refresh();
  reset_oflush_buf();

  userec_t user = {};
  int unum = passwd_load_user("SYSOP10\0", &user);
  EXPECT_EQ(unum, 26);

  showplans_userec(&user);

  refresh();

  unsigned char expected[] = ""
      "\x1B[8;1H\xA1m\xAD\xD3\xA4H\xA6W\xA4\xF9\xA1nSYSOP10 \xA5\xD8\xAB" "e\xA8S\xA6\xB3\xA6W\xA4\xF9\n\r" //《個人名片》SYSOP10 目前沒有名片
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);
}
