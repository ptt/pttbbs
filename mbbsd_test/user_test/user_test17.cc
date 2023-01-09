#include "user_test.h"

TEST_F(UserTest, UList) {
  char buf[8192] = {};
  // load SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[user_test17] after initcuser\n");

  sprintf(buf, " ");
  put_vin((unsigned char *)buf, strlen(buf));

  now = 1655913600; //2022-06-22 16:00:00 UTC
  login_start_time = now;
  now = 1655913601; //2022-06-22 16:00:01 UTC

  user_login();

  refresh();
  reset_oflush_buf();

  sprintf(buf, "2\r\n ");
  put_vin((unsigned char *)buf, strlen(buf));

  int ret = u_list();
  EXPECT_EQ(ret, 0);

  refresh();

  unsigned char expected[] = ""
      "\x1B[3;1H\x1B[7m  \xA8\xCF\xA5\xCE\xAA\xCC\xA5N\xB8\xB9   \xBA\xEF\xB8\xB9\xBC\xCA\xBA\xD9                    \xA4W\xAF\xB8  \xA4\xE5\xB3\xB9    \xB3\xCC\xAA\xF1\xA5\xFA\xC1{\xA4\xE9\xB4\xC1     \x1B[m\n\r" //  使用者代號   綽號暱稱                    上站  文章    最近光臨日期
      "SYSOP          \xAF\xAB                             3     0    06/05/2022 05:20:3\n\r" //神
      "test           \xB4\xFA\xB8\xD5                          29    49    07/06/2021 01:38:4\n\r" //測試
      "test0                                         1     0    12/06/2020 20:21:5\n\r"
      "chhsiao123                                  200     0    01/05/2022 22:43:1\n\r"
      "s77485p        Happy                          1     0    12/15/2020 14:27:3\n\r"
      "SYSOP2                                       26     0    06/22/2022 16:00:0\n\r"
      "SYSOP3         string                        23     0    08/07/2021 02:58:5\n\r"
      "SYSOP5         string                        22     0    08/07/2021 02:59:1\n\r"
      "SYSOP4         string                        22     0    08/07/2021 02:59:0\n\r"
      "pttguest                                      1     0    12/27/2020 23:27:0\n\r"
      "SYSOP120                                      2     2    01/27/2021 23:40:0\n\r"
      "test1          test1                          9     0    08/07/2021 03:00:2\n\r"
      "test3          test3                          8     0    08/07/2021 03:00:3\n\r"
      "SYSOP6                                       21     0    08/07/2021 02:59:1\n\r"
      "SYSOP7                                       21     0    08/07/2021 02:59:2\n\r"
      "SYSOP8                                       20     0    08/07/2021 02:59:3\n\r"
      "SYSOP9                                       21     0    08/07/2021 02:59:4\n\r"
      "SYSOP10                                      19     0    08/07/2021 02:59:4\x1B[K\n\r"
      "SYSOP11                                      17     0    08/07/2021 02:59:5\n\r"
      "Aa123123                                      1     0    02/08/2021 05:47:5\n\r"
      "\x1B[34;46m  \xA4w\xC5\xE3\xA5\xDC 20/100 \xA4H(20%)  \x1B[31;47m  (Space)" //  已顯示 20/100 人(20%)
      "\x1B[30m \xAC\xDD\xA4U\xA4@\xAD\xB6  " // 看下一頁
      "\x1B[31m(Q)"
      "\x1B[30m \xC2\xF7\xB6}  \x1B[m\x1B[4;1H" // 離開
      "NetArmy        \xBA\xF4\xB8\xF4\xB3\xB0\xADx                       1     0    03/22/2021 11:37:2\x1B[K\n\r"
      "test11617                                     2     0    04/21/2021 14:58:0\x1B[K\n\r"
      "test119540                                    1     0    04/19/2021 15:24:1\x1B[K\n\r"
      "test77659                                     1     0    04/19/2021 15:24:1\x1B[K\n\r"
      "test61133                                     1     0    04/19/2021 15:24:1\x1B[K\n\r"
      "test72521                                     1     0    04/19/2021 15:24:1\x1B[K\n\r"
      "test5464                                      1     0    04/19/2021 15:24:1\x1B[K\n\r"
      "test32220                                     1     0    04/19/2021 15:24:1\x1B[K\n\r"
      "\x1B[K\n\x1B[K\n\x1B[K\n\x1B[K\n\x1B[K\n\x1B[K\n\x1B[K\n\x1B[K\n\x1B[K\n\x1B[K\n\x1B[K\n\x1B[K\n"
      "\x1B[34;46m  \xA4w\xC5\xE3\xA5\xDC 28/100 \xAA\xBA\xA8\xCF\xA5\xCE\xAA\xCC(\xA8t\xB2\xCE\xAE" "e\xB6q\xB5L\xA4W\xAD\xAD)  " //  已顯示 28/100 的使用者(系統容量無上限)
      "\x1B[31;47m  (\xBD\xD0\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2)  \x1B[m\x1B[K" //  (請按任意鍵繼續)
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);
}
