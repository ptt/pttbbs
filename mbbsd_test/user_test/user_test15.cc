#include "user_test.h"

TEST_F(UserTest, UEditsig) {
  char buf[8192] = {};
  // load SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[user_test15] after initcuser\n");

  refresh();
  reset_oflush_buf();

  sprintf(buf, "n\r\nd\r\n2\r\n ");
  put_vin((unsigned char *)buf, strlen(buf));

  int ret = u_editsig();
  EXPECT_EQ(ret, 0);

  refresh();

  unsigned char expected[] = ""
      "\x1B[H\x1B[J"
      "\xC3\xB1\xA6W\xC0\xC9 (E)\xBDs\xBF\xE8 (D)\xA7R\xB0\xA3 (N)\xC2\xBD\xAD\xB6 (Q)\xA8\xFA\xAE\xF8\xA1H[Q] " //簽名檔 (E)編輯 (D)刪除 (N)翻頁 (Q)取消？[Q]
      "\x1B[0;7m    \x1B[m\x1B[3;1H\x1B[36m"
      "\xA1i \xC3\xB1\xA6W\xC0\xC9.2 \xA1j\x1B[m\n\r" //【 簽名檔.2 】
      "this is signature.2\n\r"
      "this is the 2nd line. (sig.2)\n\r"
      "this is the 3rd line. (sig.2)\n\r"
      "4th line. (sig.2)\n\r"
      "5th line. (sig.2)\n\r"
      "6th line. (sig.2)\n\r"
      "\x1B[36m\xA1i \xC3\xB1\xA6W\xC0\xC9.3 \xA1j\x1B[m\n\r" //【 簽名檔.3 】
      "this is signature.3\n\r"
      "this is the 2nd line.\n\r"
      "this is the 3rd line.\n\r"
      "4th line.\n\r"
      "5th line.\n\r"
      "6th line.\n\r"
      "\x1B[36m\xA1i \xC3\xB1\xA6W\xC0\xC9.4 \xA1j\x1B[m\n\r" //【 簽名檔.4 】
      "this is signature.4\n\r"
      "this is the 2nd line. (sig.4)\n\r"
      "this is the 3rd line. (sig.4)\n\r"
      "4th line. (sig.4)\n\r"
      "5th line. (sig.4)\n\r"
      "6th line. (sig.4)\x1B[1;45H\r"
      "\xC3\xB1\xA6W\xC0\xC9 (E)\xBDs\xBF\xE8 (D)\xA7R\xB0\xA3 (N)\xC2\xBD\xAD\xB6 (Q)\xA8\xFA\xAE\xF8\xA1H[Q] " //簽名檔 (E)編輯 (D)刪除 (N)翻頁 (Q)取消？[Q]
      "\x1B[0;7mn   \x1B[m\x1B[1;46H\x1B[H\x1B[J"
      "\xC3\xB1\xA6W\xC0\xC9 (E)\xBDs\xBF\xE8 (D)\xA7R\xB0\xA3 (N)\xC2\xBD\xAD\xB6 (Q)\xA8\xFA\xAE\xF8\xA1H[Q] "
      "\x1B[0;7m    \x1B[m\x1B[3;1H"
      "\x1B[36m\xA1i \xC3\xB1\xA6W\xC0\xC9.5 \xA1j\x1B[m\n\r" //【 簽名檔.5 】
      "this is signature.5\n\r"
      "this is the 2nd line. (sig.5)\n\r"
      "this is the 3rd line. (sig.5)\n\r"
      "4th line. (sig.5)\n\r"
      "5th line. (sig.5)\n\r"
      "6th line. (sig.5)\x1B[1;45H\r"
      "\xC3\xB1\xA6W\xC0\xC9 (E)\xBDs\xBF\xE8 (D)\xA7R\xB0\xA3 (N)\xC2\xBD\xAD\xB6 (Q)\xA8\xFA\xAE\xF8\xA1H[Q] "
      "\x1B[0;7md   \x1B[m\x1B[1;46H\n\r"
      "\xBD\xD0\xBF\xEF\xBE\xDC\xC3\xB1\xA6W\xC0\xC9(1-9)\xA1H[1] " //請選擇簽名檔(1-9)？[1]
      "\x1B[0;7m    \x1B[m\x1B[2;24H\r"
      "\xBD\xD0\xBF\xEF\xBE\xDC\xC3\xB1\xA6W\xC0\xC9(1-9)\xA1H[1] "
      "\x1B[0;7m2   \x1B[m\x1B[2;25H\n\r"
      "\xA7R\xB0\xA3\xA7\xB9\xB2\xA6" //刪除完畢
      "\x1B[24;1H\x1B[1;34;44m \xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e" // ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄
      "\x1B[1;37;44m \xBD\xD0\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2 " // 請按任意鍵繼續
      "\x1B[1;34;44m\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e  \x1B[m\r\x1B[K" //▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);

  reset_oflush_buf();

  sprintf(buf, "q\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  ret = u_editsig();
  EXPECT_EQ(ret, 0);

  refresh();

  unsigned char expected1[] = ""
      "\x1B[H\x1B[J"
      "\xC3\xB1\xA6W\xC0\xC9 (E)\xBDs\xBF\xE8 (D)\xA7R\xB0\xA3 (Q)\xA8\xFA\xAE\xF8\xA1H[Q] " //簽名檔 (E)編輯 (D)刪除 (Q)取消？[Q]
      "\x1B[0;7m    \x1B[m\x1B[3;1H"
      "\x1B[36m\xA1i \xC3\xB1\xA6W\xC0\xC9.3 \xA1j\x1B[m\n\r" //【 簽名檔.3 】
      "this is signature.3\n\r"
      "this is the 2nd line.\n\r"
      "this is the 3rd line.\n\r"
      "4th line.\n\r"
      "5th line.\n\r"
      "6th line.\n\r"
      "\x1B[36m\xA1i \xC3\xB1\xA6W\xC0\xC9.4 \xA1j\x1B[m\n\r"
      "this is signature.4\n\r"
      "this is the 2nd line. (sig.4)\n\r"
      "this is the 3rd line. (sig.4)\n\r"
      "4th line. (sig.4)\n\r"
      "5th line. (sig.4)\n\r"
      "6th line. (sig.4)\n\r"
      "\x1B[36m\xA1i \xC3\xB1\xA6W\xC0\xC9.5 \xA1j\x1B[m\n\r"
      "this is signature.5\n\r"
      "this is the 2nd line. (sig.5)\n\r"
      "this is the 3rd line. (sig.5)\n\r"
      "4th line. (sig.5)\n\r"
      "5th line. (sig.5)\n\r"
      "6th line. (sig.5)\x1B[1;37H\r"
      "\xC3\xB1\xA6W\xC0\xC9 (E)\xBDs\xBF\xE8 (D)\xA7R\xB0\xA3 (Q)\xA8\xFA\xAE\xF8\xA1H[Q] "
      "\x1B[0;7mq   \x1B[m\x1B[1;38H\n\r"
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected1);
}
