#include "user_test.h"

TEST_F(UserTest, UEditplan) {
  char buf[8192] = {};
  // load SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[user_test16] after initcuser\n");

  sprintf(buf, " ");
  put_vin((unsigned char *)buf, strlen(buf));

  now = 1655913600; //2022-06-22 16:00:00 UTC
  login_start_time = now;
  now = 1655913601; //2022-06-22 16:00:01 UTC

  user_login();

  refresh();
  reset_oflush_buf();

  sprintf(buf, "q\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  reset_oflush_buf();

  int ret = u_editplan();
  EXPECT_EQ(ret, 0);

  refresh();

  unsigned char expected[] = ""
      "\x1B[23;1H\xA6W\xA4\xF9 (D)\xA7R\xB0\xA3 (E)\xBDs\xBF\xE8 [Q]\xA8\xFA\xAE\xF8\xA1H[Q] " //名片 (D)刪除 (E)編輯 [Q]取消？[Q]
      "\x1B[0;7m   \x1B[m"
      "\x1B[23;35H\r\xA6W\xA4\xF9 (D)\xA7R\xB0\xA3 (E)\xBDs\xBF\xE8 [Q]\xA8\xFA\xAE\xF8\xA1H[Q] "
      "\x1B[0;7mq  \x1B[m\x1B[23;36H\n\r"
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);

  fprintf(stderr, "[user_test16] to delete plan.\n");

  sprintf(buf, "d\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  reset_oflush_buf();

  ret = u_editplan();
  EXPECT_EQ(ret, 0);

  refresh();

  unsigned char expected1[] = ""
      "\x1B[23;1H\xA6W\xA4\xF9 (D)\xA7R\xB0\xA3 (E)\xBDs\xBF\xE8 [Q]\xA8\xFA\xAE\xF8\xA1H[Q] " //名片 (D)刪除 (E)編輯 [Q]取消？[Q]
      "\x1B[0;7m   \x1B[m"
      "\x1B[23;35H\r\xA6W\xA4\xF9 (D)\xA7R\xB0\xA3 (E)\xBDs\xBF\xE8 [Q]\xA8\xFA\xAE\xF8\xA1H[Q] "
      "\x1B[0;7md  \x1B[m"
      "\x1B[23;36H\n\r\xA6W\xA4\xF9\xA7R\xB0\xA3\xA7\xB9\xB2\xA6" //名片刪除完畢
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected1);

  fprintf(stderr, "[user_test16] to edit plan.\n");

  sprintf(buf, "e\r\nthis is the plan.\r\n%cs\r\ny\r\n", Ctrl('X'));
  put_vin((unsigned char *)buf, strlen(buf));

  reset_oflush_buf();

  ret = u_editplan();
  EXPECT_EQ(ret, 0);

  refresh();

  unsigned char expected2[] = ""
      "\x1B[23;1H\xA6W\xA4\xF9 (D)\xA7R\xB0\xA3 (E)\xBDs\xBF\xE8 [Q]\xA8\xFA\xAE\xF8\xA1H[Q] " //名片 (D)刪除 (E)編輯 [Q]取消？[Q]
      "\x1B[0;7m   \x1B[m"
      "\x1B[23;35H\r\xA6W\xA4\xF9 (D)\xA7R\xB0\xA3 (E)\xBDs\xBF\xE8 [Q]\xA8\xFA\xAE\xF8\xA1H[Q] "
      "\x1B[0;7me  \x1B[m"
      "\x1B[23;36H\x1B[1;1H\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\x1B[K\n\r"
      "\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA " //說明
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB " //插入符號/範本
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  1:  1 \x1B[m" //離開  ║插入│aipr║  1:  1
      "\x1B[1;1Ht\x1B[m\x1B[K"
      "\x1B[24;1H\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA " //說明
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB " //插入符號/範本
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  1:  2 \x1B[m" //離開  ║插入│aipr║  1:  2
      "\x1B[1;2H\rth\x1B[m\x1B[K"
      "\x1B[24;1H\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA "
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB "
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  1:  3 \x1B[m"
      "\x1B[1;3H\rthi\x1B[m\x1B[K" //thi
      "\x1B[24;1H\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA "
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB "
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  1:  4 \x1B[m"
      "\x1B[1;4H\rthis\x1B[m\x1B[K" //this
      "\x1B[24;1H\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA "
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB "
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  1:  5 \x1B[m"
      "\x1B[1;5H\rthis \x1B[m\x1B[K" //this
      "\x1B[24;1H\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA "
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB "
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  1:  6 \x1B[m"
      "\x1B[1;6H\rthis i\x1B[m\x1B[K" //this i
      "\x1B[24;1H\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA "
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB "
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  1:  7 \x1B[m"
      "\x1B[1;7H\rthis is\x1B[m\x1B[K" //this is
      "\x1B[24;1H\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA "
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB "
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  1:  8 \x1B[m"
      "\x1B[1;8H\rthis is \x1B[m\x1B[K" //this is
      "\x1B[24;1H\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA "
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB "
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  1:  9 \x1B[m"
      "\x1B[1;9H\rthis is t\x1B[m\x1B[K" //this is t
      "\x1B[24;1H\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA "
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB "
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  1: 10 \x1B[m"
      "\x1B[1;10H\r"
      "this is th\x1B[m\x1B[K" //this is th
      "\x1B[24;1H\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA "
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB "
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  1: 11 \x1B[m"
      "\x1B[1;11H\r"
      "this is the\x1B[m\x1B[K" //this is the
      "\x1B[24;1H\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA "
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB "
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  1: 12 \x1B[m"
      "\x1B[1;12H\r"
      "this is the \x1B[m\x1B[K" //this is the
      "\x1B[24;1H\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA "
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB "
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  1: 13 \x1B[m"
      "\x1B[1;13H\r"
      "this is the p\x1B[m\x1B[K" //this is the p
      "\x1B[24;1H\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA "
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB "
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  1: 14 \x1B[m"
      "\x1B[1;14H\r"
      "this is the pl\x1B[m\x1B[K" //this is the pl
      "\x1B[24;1H\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA "
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB "
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  1: 15 \x1B[m"
      "\x1B[1;15H\r"
      "this is the pla\x1B[m\x1B[K" //this is the pla
      "\x1B[24;1H\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA "
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB "
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  1: 16 \x1B[m"
      "\x1B[1;16H\r"
      "this is the plan\x1B[m\x1B[K" //this is the plan
      "\x1B[24;1H\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA "
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB "
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  1: 17 \x1B[m"
      "\x1B[1;17H\r"
      "this is the plan.\x1B[m\x1B[K" //this is the plan.
      "\x1B[24;1H\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA "
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB "
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  1: 18 \x1B[m"
      "\x1B[1;18H\r"
      "this is the plan.\x1B[K\x1B[K\n\r" //this is the plan.
      "\x1B[K\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "~\x1B[K\n\r"
      "\x1B[0;34;46m \xBDs\xBF\xE8\xA4\xE5\xB3\xB9 " // 編輯文章
      "\x1B[0;30;47m "
      "\x1B[0;31;47m(^Z/F1)"
      "\x1B[0;30;47m\xBB\xA1\xA9\xFA " //說明
      "\x1B[0;31;47m(^P/^G)"
      "\x1B[0;30;47m\xB4\xA1\xA4J\xB2\xC5\xB8\xB9/\xBD" "d\xA5\xBB " //插入符號/範本
      "\x1B[0;31;47m(^X/^Q)"
      "\x1B[0;30;47m\xC2\xF7\xB6}  \xF9\xF8\xB4\xA1\xA4J\xA2xaipr\xF9\xF8  2:  1 \x1B[m" //離開  ║插入│aipr║  2:  1
      "\x1B[2;1H\x1B[H\x1B[J\x1B[1;37;46m\xA1i \xC0\xC9\xAE\xD7\xB3" "B\xB2z \xA1j\x1B[m\n\r" //【 檔案處理 】
      "[S]\xC0x\xA6s (U)\xA4W\xB6\xC7\xB8\xEA\xAE\xC6 (A)\xA9\xF1\xB1\xF3 (E)\xC4~\xC4\xF2 (R/W/D)\xC5\xAA\xBCg\xA7R\xBC\xC8\xA6s\xC0\xC9\n\r" //[S]儲存 (U)上傳資料 (A)放棄 (E)繼續 (R/W/D)讀寫刪暫存檔
      "\xBDT\xA9w\xADn\xC0x\xA6s\xC0\xC9\xAE\xD7\xB6\xDC\xA1H " //確定要儲存檔案嗎？
      "\x1B[0;7m  \x1B[m"
      "\x1B[3;20H\r"
      "\xBDT\xA9w\xADn\xC0x\xA6s\xC0\xC9\xAE\xD7\xB6\xDC\xA1H "
      "\x1B[0;7ms \x1B[m"
      "\x1B[3;21H\n\r"
      "\xA6W\xA4\xF9\xA7\xF3\xB7s\xA7\xB9\xB2\xA6" //名片更新完畢
      "\x1B[24;1H\x1B[1;34;44m \xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e" // ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄
      "\x1B[1;37;44m \xBD\xD0\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2 " // 請按任意鍵繼續
      "\x1B[1;34;44m\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e\xA2" "e  \x1B[m\r\x1B[K" //▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected2);
}
