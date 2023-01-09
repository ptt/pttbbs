#include "register_test.h"

TEST_F(RegisterTest, ChangeContactEmail) {
  char buf[8192] = {};
  sprintf(buf, "  y\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  // load SYSOP2
  load_current_user("SYSOP2");
  fprintf(stderr, "[register_test12] after load current_user\n");

  sprintf(buf, "test12@test.edu\r\ny\r\nv6JGyTsompzaN\r\ny");
  put_vin((unsigned char *)buf, strlen(buf));

  refresh();
  log_oflush_buf();
  reset_oflush_buf();

  change_contact_email();

  refresh();
  log_oflush_buf();

  unsigned char expected[] = ""
      "\x1B[H\x1B[J"
      "\x1B[1;37;46m\xA1i E-Mail \xA1j\x1B[m\n\r" //【 E-Mail 】
      "\xB1z\xA6n,\n\r" //您好,
      "  \xBD\xD0\xBF\xE9\xA4J\xB1z\xAA\xBA E-Mail , \xA7\xDA\xAD\xCC\xB7|\xB1H\xB5o\xA7t\xA6\xB3\xBB{\xC3\xD2\xBDX\xAA\xBA\xABH\xA5\xF3\xB5\xB9\xB1z\n\r" //  請輸入您的 E-Mail , 我們會寄發含有認證碼的信件給您
      "  \xA6\xAC\xA8\xEC\xAB\xE1\xBD\xD0\xA8\xEC\xBD\xD0\xBF\xE9\xA4J\xBB{\xC3\xD2\xBDX." //  收到後請到請輸入認證碼.
      "\x1B[6;1H**********************************************************\n\r"
      "* \xADY\xB9L\xA4[\xA5\xBC\xA6\xAC\xA8\xEC\xBD\xD0\xA8\xEC\xB6l\xA5\xF3\xA9U\xA7\xA3\xB1\xED\xC0\xCB\xAC" "d\xACO\xA7_\xB3Q\xB7\xED\xA7@\xA9U\xA7\xA3\xABH(SPAM)\xA4" "F,*\n\r" // 若過久未收到請到郵件垃圾桶檢查是否被當作垃圾信(SPAM)了,
      "* \xA5t\xA5~\xADY\xBF\xE9\xA4J\xAB\xE1\xB5o\xA5\xCD\xBB{\xC3\xD2\xBDX\xBF\xF9\xBB~\xBD\xD0\xA5\xFD\xBDT\xBB{\xBF\xE9\xA4J\xACO\xA7_\xAC\xB0\xB3\xCC\xAB\xE1\xA4@\xAB\xCA   *\n\r" // 另外若輸入後發生認證碼錯誤請先確認輸入是否為最後一封
      "* \xA6\xAC\xA8\xEC\xAA\xBA\xBB{\xC3\xD2\xABH\xA1" "A\xADY\xAFu\xAA\xBA\xA4\xB4\xB5M\xA4\xA3\xA6\xE6\xBD\xD0\xA6" "A\xAD\xAB\xB6\xF1\xA4@\xA6\xB8 E-Mail.       *\n\r" // 收到的認證信，若真的仍然不行請再重填一次 E-Mail.
      "**********************************************************"
      "\x1B[16;1H  \xAD\xEC\xA5\xFD\xB3]\xA9w\xA1G                              \n\r" //  原先設定：
      "\x1B[1m>>E-Mail Address"
      "\x1B[m\xA1G"
      "\x1B[0;7m                                                  \x1B[m\x1B[17;19H\r"
      "\x1B[1m>>E-Mail Address"
      "\x1B[m\xA1G"
      "\x1B[0;7mt                                                 \x1B[m\x1B[17;20H\r"
      "\x1B[1m>>E-Mail Address"
      "\x1B[m\xA1G"
      "\x1B[0;7mte                                                \x1B[m\x1B[17;21H\r"
      "\x1B[1m>>E-Mail Address"
      "\x1B[m\xA1G"
      "\x1B[0;7mtes                                               \x1B[m\x1B[17;22H\r"
      "\x1B[1m>>E-Mail Address"
      "\x1B[m\xA1G"
      "\x1B[0;7mtest                                              \x1B[m\x1B[17;23H\r"
      "\x1B[1m>>E-Mail Address"
      "\x1B[m\xA1G"
      "\x1B[0;7mtest1                                             \x1B[m\x1B[17;24H\r"
      "\x1B[1m>>E-Mail Address"
      "\x1B[m\xA1G"
      "\x1B[0;7mtest12                                            \x1B[m\x1B[17;25H\r"
      "\x1B[1m>>E-Mail Address"
      "\x1B[m\xA1G"
      "\x1B[0;7mtest12@                                           \x1B[m\x1B[17;26H\r"
      "\x1B[1m>>E-Mail Address"
      "\x1B[m\xA1G"
      "\x1B[0;7mtest12@t                                          \x1B[m\x1B[17;27H\r"
      "\x1B[1m>>E-Mail Address"
      "\x1B[m\xA1G"
      "\x1B[0;7mtest12@te                                         \x1B[m\x1B[17;28H\r"
      "\x1B[1m>>E-Mail Address"
      "\x1B[m\xA1G"
      "\x1B[0;7mtest12@tes                                        \x1B[m\x1B[17;29H\r"
      "\x1B[1m>>E-Mail Address"
      "\x1B[m\xA1G"
      "\x1B[0;7mtest12@test                                       \x1B[m\x1B[17;30H\r"
      "\x1B[1m>>E-Mail Address"
      "\x1B[m\xA1G"
      "\x1B[0;7mtest12@test.                                      \x1B[m\x1B[17;31H\r"
      "\x1B[1m>>E-Mail Address"
      "\x1B[m\xA1G"
      "\x1B[0;7mtest12@test.e                                     \x1B[m\x1B[17;32H\r"
      "\x1B[1m>>E-Mail Address"
      "\x1B[m\xA1G"
      "\x1B[0;7mtest12@test.ed                                    \x1B[m\x1B[17;33H\r"
      "\x1B[1m>>E-Mail Address"
      "\x1B[m\xA1G"
      "\x1B[0;7mtest12@test.edu                                   \x1B[m\x1B[17;34H"
      "\x1B[16;1H  E-Mail Address\xA1Gtest12@test.edu\x1B[K\n\r"
      "\x1B[K\x1B[19;1H\xA5\xBF\xA6" "b\xBDT\xBB{ email, \xBD\xD0\xB5y\xAD\xD4...\n\r" //正在確認 email, 請稍候...
      "\x1B[17;1H\xBD\xD0\xA6" "A\xA6\xB8\xBDT\xBB{\xA6\xB9 E-Mail \xA6\xEC\xB8m\xA5\xBF\xBDT\xB6\xDC? [y/N]" //請再次確認此 E-Mail 位置正確嗎? [y/N]
      "\x1B[0;7m   \x1B[m\x1B[19;1H\x1B[K"
      "\x1B[17;38H\r\xBD\xD0\xA6" "A\xA6\xB8\xBDT\xBB{\xA6\xB9 E-Mail \xA6\xEC\xB8m\xA5\xBF\xBDT\xB6\xDC? [y/N]"
      "\x1B[0;7my  \x1B[m\x1B[17;39H"
      "\x1B[16;1H\xBB{\xC3\xD2\xBDX\xA4w\xB1H\xB0" "e\xA6\xDC:\x1B[K\n\r" //認證碼已寄送至:
      "\x1B[1;33m  test12@test.edu\x1B[m\x1B[K\n\r"
      "\xBB{\xC3\xD2\xBDX\xC1`\xA6@\xC0\xB3\xA6\xB3\xA4Q\xA4T\xBDX, \xA8S\xA6\xB3\xAA\xC5\xA5\xD5\xA6r\xA4\xB8.\n\r"
      "\xADY\xB9L\xA4[\xA8S\xA6\xAC\xA8\xEC\xBB{\xC3\xD2\xBDX\xA5i\xBF\xE9\xA4J x \xAD\xAB\xB7s\xB1H\xB0" "e." //認證碼總共應有十三碼, 沒有空白字元.
      "\x1B[21;1H\xBD\xD0\xBF\xE9\xA4J\xBB{\xC3\xD2\xBDX\xA1G" //請輸入認證碼：
      "\x1B[0;7m              \x1B[m"
      "\x1B[21;15H\r\xBD\xD0\xBF\xE9\xA4J\xBB{\xC3\xD2\xBDX\xA1G"
      "\x1B[0;7mv             \x1B[m"
      "\x1B[21;16H\r\xBD\xD0\xBF\xE9\xA4J\xBB{\xC3\xD2\xBDX\xA1G"
      "\x1B[0;7mv6            \x1B[m"
      "\x1B[21;17H\r\xBD\xD0\xBF\xE9\xA4J\xBB{\xC3\xD2\xBDX\xA1G"
      "\x1B[0;7mv6J           \x1B[m"
      "\x1B[21;18H\r\xBD\xD0\xBF\xE9\xA4J\xBB{\xC3\xD2\xBDX\xA1G"
      "\x1B[0;7mv6JG          \x1B[m"
      "\x1B[21;19H\r\xBD\xD0\xBF\xE9\xA4J\xBB{\xC3\xD2\xBDX\xA1G"
      "\x1B[0;7mv6JGy         \x1B[m"
      "\x1B[21;20H\r\xBD\xD0\xBF\xE9\xA4J\xBB{\xC3\xD2\xBDX\xA1G"
      "\x1B[0;7mv6JGyT        \x1B[m"
      "\x1B[21;21H\r\xBD\xD0\xBF\xE9\xA4J\xBB{\xC3\xD2\xBDX\xA1G"
      "\x1B[0;7mv6JGyTs       \x1B[m"
      "\x1B[21;22H\r\xBD\xD0\xBF\xE9\xA4J\xBB{\xC3\xD2\xBDX\xA1G"
      "\x1B[0;7mv6JGyTso      \x1B[m"
      "\x1B[21;23H\r\xBD\xD0\xBF\xE9\xA4J\xBB{\xC3\xD2\xBDX\xA1G"
      "\x1B[0;7mv6JGyTsom     \x1B[m"
      "\x1B[21;24H\r\xBD\xD0\xBF\xE9\xA4J\xBB{\xC3\xD2\xBDX\xA1G"
      "\x1B[0;7mv6JGyTsomp    \x1B[m"
      "\x1B[21;25H\r\xBD\xD0\xBF\xE9\xA4J\xBB{\xC3\xD2\xBDX\xA1G"
      "\x1B[0;7mv6JGyTsompz   \x1B[m"
      "\x1B[21;26H\r\xBD\xD0\xBF\xE9\xA4J\xBB{\xC3\xD2\xBDX\xA1G"
      "\x1B[0;7mv6JGyTsompza  \x1B[m"
      "\x1B[21;27H\r\xBD\xD0\xBF\xE9\xA4J\xBB{\xC3\xD2\xBDX\xA1G"
      "\x1B[0;7mv6JGyTsompzaN \x1B[m"
      "\x1B[21;28H\x1B[24;1H"
      "\x1B[1;36;44m \xA1\xBB \xC1p\xB5\xB8\xABH\xBD" "c\xA7\xF3\xB7s\xA7\xB9\xA6\xA8\xA1" "C                                         " // ◆ 聯絡信箱更新完成。
      "\x1B[1;33;46m [\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2] \x1B[m\r\x1B[K" // [按任意鍵繼續]
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);
}
