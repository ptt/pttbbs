#include "user_test.h"

TEST_F(UserTest, ViolateLaw) {
  char buf[8192] = {};
  // load SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[user_test8] after initcuser\n");

  userec_t user = {};
  int uid = passwd_load_user("SYSOP10\0", &user);
  EXPECT_EQ(uid, 26);

  refresh();
  reset_oflush_buf();

  sprintf(buf, "2\r\ny\r\ny");
  put_vin((unsigned char *)buf, strlen(buf));

  violate_law(&user, uid);

  refresh();

  unsigned char expected[] = ""
      "\x1B[3;1H(1)Cross-post (2)\xB6\xC3\xB5o\xBCs\xA7i\xABH (3)\xB6\xC3\xB5o\xB3s\xC2\xEA\xABH\n\r" //(1)Cross-post (2)亂發廣告信 (3)亂發連鎖信
      "(4)\xC4\xCC\xC2Z\xAF\xB8\xA4W\xA8\xCF\xA5\xCE\xAA\xCC (8)\xA8\xE4\xA5L\xA5H\xBB@\xB3\xE6\xB3" "B\xB8m\xA6\xE6\xAC\xB0\n\r"
      "(9)\xAC\xE5 id \xA6\xE6\xAC\xB0\n\r"
      "(0)\xB5\xB2\xA7\xF4"
      "\x1B[0;7m   \x1B[m"
      "\x1B[6;8H\r(0)\xB5\xB2\xA7\xF4"
      "\x1B[0;7m2  \x1B[m"
      "\x1B[6;9H\x1B[8;1H\xBD\xD0\xB1z\xBDT\xA9w(Y/N)\xA1H[N] "
      "\x1B[0;7m   \x1B[m"
      "\x1B[8;20H\r\xBD\xD0\xB1z\xBDT\xA9w(Y/N)\xA1H[N] "
      "\x1B[0;7my  \x1B[m"
      "\x1B[8;21H\x1B[24;1H\x1B[1;34;44m \xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e"
      "\x1B[1;37;44m \xBD\xD0\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2 "
      "\x1B[1;34;44m\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e  \x1B[m\r\x1B[K"
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);
}
