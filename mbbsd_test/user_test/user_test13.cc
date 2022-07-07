#include "user_test.h"

TEST_F(UserTest, Showplans) {
  // load SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[user_test13] after initcuser\n");

  refresh();
  reset_oflush_buf();

  showplans("SYSOP10\0");

  refresh();

  unsigned char expected[] = ""
      "\x1B[8;1H\xA1m\xAD\xD3\xA4H\xA6W\xA4\xF9\xA1nSYSOP10 \xA5\xD8\xAB" "e\xA8S\xA6\xB3\xA6W\xA4\xF9\n\r" //《個人名片》SYSOP10 目前沒有名片
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);

}
