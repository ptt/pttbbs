#include "register_test.h"

TEST_F(RegisterTest, Regform2ValidatePage) {
  char buf[8192] = {};
  // load SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[register_test35] after initcuser\n");

  refresh();
  log_oflush_buf();
  reset_oflush_buf();

  sprintf(buf, "y\r\ntest check2\r\ntestcareer2\r\ntestaddr1234567\xA5\xAB" "02\r\ny\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  EXPECT_EQ(cuser.userlevel & PERM_LOGINOK, 0);

  u_manual_verification();

  refresh();
  reset_oflush_buf();

  // load SYSOP10
  initcuser("SYSOP10");
  fprintf(stderr, "[register_test35] after initcuser\n");

  refresh();
  reset_oflush_buf();

  sprintf(buf, "y\r\ntest check10\r\ntestcareer10\r\ntestaddr1234567\xA5\xAB" "10\r\ny\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  EXPECT_EQ(cuser.userlevel & PERM_LOGINOK, 0);

  u_manual_verification();

  refresh();
  reset_oflush_buf();

  // load SYSOP11
  initcuser("SYSOP11");
  fprintf(stderr, "[register_test35] after initcuser\n");

  refresh();
  reset_oflush_buf();

  sprintf(buf, "y\r\ntest check11\r\ntestcareer11\r\ntestaddr1234567\xA5\xAB" "11\r\ny\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  EXPECT_EQ(cuser.userlevel & PERM_LOGINOK, 0);

  u_manual_verification();

  refresh();
  log_oflush_buf();
  reset_oflush_buf();

  sprintf(buf, "yjyjy  \r\n  ");
  put_vin((unsigned char *)buf, strlen(buf));

  int ret = regform2_validate_page(1);
  EXPECT_EQ(ret, 0);

  unsigned char expected[] = ""
      "\x1B[H\x1B[J"
      ">  1. \x1B[1mSYSOP2       \x1B[m"
      "\x1B[1;31m        test check2 "
      "\x1B[1;32mtestcareer2                             \x1B[m\n\r"
      "      testaddr1234567\xA5\xAB" "02                               "
      "\x1B[0;33m127.0.0.1\x1B[m\n\r"
      "   2. \x1B[1mSYSOP10      \x1B[m"
      "\x1B[1;31m       test check10 "
      "\x1B[1;32mtestcareer10                            \x1B[m\n\r"
      "      testaddr1234567\xA5\xAB" "10                               "
      "\x1B[0;33m172.19.0.1\x1B[m\n\r"
      "   3. \x1B[1mSYSOP11      \x1B[m"
      "\x1B[1;31m       test check11 "
      "\x1B[1;32mtestcareer11                            \x1B[m\n\r"
      "      testaddr1234567\xA5\xAB" "11                               "
      "\x1B[0;33m172.19.0.1\x1B[m\n\r"
      "\x1B[7m                                                  (\xB4\xFA\xB8\xD5\xBC\xD2\xA6\xA1) \xA4w\xC5\xE3\xA5\xDC 3 \xA5\xF7\xB5\xF9\xA5U\xB3\xE6 \x1B[m" //                                                  (測試模式) 已顯示 3 份註冊單
      "\x1B[24;1H\x1B[0;34;46m \xBC" "f\xAE\xD6 " // 審核
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(y)"
      "\x1B[0;30;47m\xB1\xB5\xA8\xFC" //接受
      "\x1B[0;31;47m(n)"
      "\x1B[0;30;47m\xA9\xDA\xB5\xB4" //拒絕
      "\x1B[0;31;47m(d)"
      "\x1B[0;30;47m\xA5\xE1\xB1\xBC " //丟掉
      "\x1B[0;31;47m(s)"
      "\x1B[0;30;47m\xB8\xF5\xB9L" //跳過
      "\x1B[0;31;47m(u)"
      "\x1B[0;30;47m\xB4_\xAD\xEC " //復原
      "\x1B[0;31;47m(\xAA\xC5\xA5\xD5/PgDn)" //(空白/PgDn)
      "\x1B[0;30;47m\xC0x\xA6s+\xA4U\xAD\xB6 " //儲存+下頁
      "\x1B[0;31;47m(q/END)"
      "\x1B[0;30;47m\xB5\xB2\xA7\xF4   " //結束
      "\x1B[m\x1B[1;1H\x1B[1;30;40m   1. SYSOy2               test check2 testcareer2                             \x1B[m\x1B[K\n\r"
      "\x1B[1;30;40m      testaddr1234567\xA5\xAB" "02                               127.0.0.1"
      "\x1B[m\x1B[K\n\r"
      ">\r"
      " \x1B[5;1H>\r"
      ">[1;30;40m   3. SYSOy11             test check11 testcareer11                            "
      "\x1B[m\x1B[K\n\r"
      "\x1B[1;30;40m      testaddr1234567\xA5\xAB" "11                               172.19.0.1"
      "\x1B[m\x1B[K\x1B[5;1H>\r"
      ">[1;30;40m "
      "[1;30;40my  3. SYSOy11             test check11 testcareer11                            \x1B[m\x1B[K\n\r"
      "\x1B[1;30;40m      testaddr1234567\xA5\xAB" "11                               172.19.0.1"
      "\x1B[m\x1B[K\x1B[5;1H "
      "\x1B[24;1H\xA9|\xA5\xBC\xAB\xFC\xA9w\xAA\xBA 1 \xAD\xD3\xB6\xB5\xA5\xD8\xADn: (S\xB8\xF5\xB9L/y\xB3q\xB9L/n\xA9\xDA\xB5\xB4/e\xC4~\xC4\xF2\xBDs\xBF\xE8): " //尚未指定的 1 個項目要: (S跳過/y通過/n拒絕/e繼續編輯):
      "\x1B[0;7m   "
      "\x1B[m\x1B[K"
      "\x1B[24;55H\r\xA9|\xA5\xBC\xAB\xFC\xA9w\xAA\xBA 1 \xAD\xD3\xB6\xB5\xA5\xD8\xADn: (S\xB8\xF5\xB9L/y\xB3q\xB9L/n\xA9\xDA\xB5\xB4/e\xC4~\xC4\xF2\xBDs\xBF\xE8): "
      "\x1B[0;7m   "
      "\x1B[m\x1B[24;56H"
      "\x1B[H\x1B[J\x1B[1;37;46m\xA1i \xB4\xFA\xB8\xD5\xBC\xD2\xA6\xA1 \xA1j\x1B[m\n\r" //【 測試模式 】
      "\xB1z\xA5\xBF\xA6" "b\xB0\xF5\xA6\xE6\xB4\xFA\xB8\xD5\xBC\xD2\xA6\xA1\xA1" "A\xA9\xD2\xA5H\xAD\xE8\xBC" "f\xAA\xBA\xB5\xF9\xA5U\xB3\xE6\xA8\xC3\xA4\xA3\xB7|\xA5\xCD\xAE\xC4\xA1" "C\n\r" //您正在執行測試模式，所以剛審的註冊單並不會生效。
      "\xA4U\xAD\xB1\xA6" "C\xA5X\xAA\xBA\xACO\xAD\xE8\xA4~\xB1z\xBC" "f\xA7\xB9\xAA\xBA\xB5\xB2\xAAG:" //下面列出的是剛才您審完的結果:
      "\x1B[5;1H 1. SYSOP2       - y SYSOP11 01/01/1970 00:00:00 Thu\n\r"
      " 2. SYSOP10      - s \n\r"
      " 3. SYSOP11      - y SYSOP11 01/01/1970 00:00:00 Thu"
      "\x1B[24;1H\x1B[1;34;44m \xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e" // ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄
      "\x1B[1;37;44m \xBD\xD0\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2 " // 請按任意鍵繼續
      "\x1B[1;34;44m\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e  \x1B[m"
      "\x1B[H\x1B[J"
      "\x1B[6;1H\xB1z\xC0\xCB\xB5\xF8\xA4" "F 3 \xA5\xF7\xB5\xF9\xA5U\xB3\xE6\xA1" "C" //您檢視了 3 份註冊單。
      "\x1B[24;1H\x1B[1;34;44m \xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e"
      "\x1B[1;37;44m \xBD\xD0\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2 " // 請按任意鍵繼續
      "\x1B[1;34;44m\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e  \x1B[m"
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);
}
