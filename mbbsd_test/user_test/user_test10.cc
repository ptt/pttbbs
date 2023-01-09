#include "user_test.h"

TEST_F(UserTest, UInfoQuery) {
  char buf[8192] = {};
  // load SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[user_test10] after initcuser\n");

  refresh();
  reset_oflush_buf();

  userec_t user = {};
  int uid = passwd_load_user("SYSOP10\0", &user);
  EXPECT_EQ(uid, 26);

  sprintf(buf, "0\r\n  ");
  put_vin((unsigned char *)buf, strlen(buf));

  reset_oflush_buf();

  uinfo_query("SYSOP10\0", 1, uid);

  refresh();

  unsigned char expected[] = ""
      "\x1B[24;1H(1)\xA7\xEF\xB8\xEA\xAE\xC6(2)\xB1K\xBDX(3)\xC5v\xAD\xAD(4)\xAC\xE5\xB1" "b(5)\xA7\xEFID(6)\xC3" "d\xAA\xAB(7)\xBC" "f\xA7P(8)\xB0h\xA4\xE5(M)\xABH\xBD" "c(V)\xBB{\xC3\xD2 " //(1)改資料(2)密碼(3)權限(4)砍帳(5)改ID(6)寵物(7)審判(8)退文(M)信箱(V)認證 [0]結束
      "\x1B[0;7m   \x1B[m"
      "\x1B[24;74H\r(1)\xA7\xEF\xB8\xEA\xAE\xC6(2)\xB1K\xBDX(3)\xC5v\xAD\xAD(4)\xAC\xE5\xB1" "b(5)\xA7\xEFID(6)\xC3" "d\xAA\xAB(7)\xBC" "f\xA7P(8)\xB0h\xA4\xE5(M)\xABH\xBD" "c(V)\xBB{\xC3\xD2 "
      "\x1B[0;7m0  \x1B[m"
      "\x1B[24;75H\r"
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);
}
