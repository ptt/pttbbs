#include "user_test.h"

TEST_F(UserTest, UInfo) {
  char buf[8192] = {};
  // load SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[user_test11] after initcuser\n");

  sprintf(buf, " ");
  put_vin((unsigned char *)buf, strlen(buf));

  now = 1655913600; //2022-06-22 16:00:00 UTC
  login_start_time = now;
  now = 1655913601; //2022-06-22 16:00:01 UTC

  user_login();

  refresh();
  reset_oflush_buf();

  sprintf(buf, "0\r\n  ");
  put_vin((unsigned char *)buf, strlen(buf));

  int ret = u_info();

  refresh();

  EXPECT_EQ(ret, 0);

  unsigned char expected[] = ""

      "\x1B[3;1H        "
      "\x1B[30;41m\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s" //┴┬┴┬┴┬
      "\x1B[m  \x1B[1;30;45m    \xA8\xCF \xA5\xCE \xAA\xCC \xB8\xEA \xAE\xC6             " //    使 用 者 資 料
      "\x1B[m  \x1B[30;41m\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\x1B[m\n\r" //┴┬┴┬┴┬
      "        \xA5N\xB8\xB9\xBC\xCA\xBA\xD9: SYSOP2 ()\n\r" //代號暱稱: SYSOP2 ()
      "        \xAFu\xB9\xEA\xA9m\xA6W:  \n\r" //真實姓名:
      "        \xC2\xBE\xB7~\xBE\xC7\xBE\xFA: \n\r" //職業學歷:
      "        \xA9~\xA6\xED\xA6" "a\xA7}: \n\r" //居住地址:
      "        \xC1p\xB5\xB8\xABH\xBD" "c: \n\r" //聯絡信箱:
      "           Ptt\xB9\xF4: 0 Ptt\xB9\xF4\n\r" //Ptt幣: 0 Ptt幣
      "        \xACO\xA7_\xA6\xA8\xA6~: \xA4w\xBA\xA1" "18\xB7\xB3\n\r" //是否成年: 已滿18歲
      "        \xB5\xF9\xA5U\xA4\xE9\xB4\xC1: 12/15/2020 21:53:29 Tue (\xA4w\xBA\xA1 564 \xA4\xD1)\n\r" //註冊日期: 12/15/2020 21:53:29 Tue (已滿 564 天)
      "        \xA8p\xA4H\xABH\xBD" "c: 0 \xAB\xCA  (\xC1\xCA\xB6R\xABH\xBD" "c: 0 \xAB\xCA)\n\r" //私人信箱: 0 封  (購買信箱: 0 封)
      "        \xA8\xCF\xA5\xCE\xB0O\xBF\xFD: \xB5n\xA4J\xA6\xB8\xBC\xC6 26 \xA6\xB8 / \xA4\xE5\xB3\xB9 0 \xBDg\n\r" //使用記錄: 登入次數 26 次 / 文章 0 篇
      "        \xB0\xB1\xAF" "d\xB4\xC1\xB6\xA1: 269 \xA4p\xAE\xC9 35 \xA4\xC0\n\r" //停留期間: 269 小時 35 分
      "        \xB0h\xA4\xE5\xBC\xC6\xA5\xD8: 0\n\r" //退文數目: 0
      "        \x1B[30;41m\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\x1B[m\n\r" //┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬
      "\xA6p\xAAG\xADn\xB4\xA3\xAA@\xC5v\xAD\xAD\xA1" "A\xBD\xD0\xB0\xD1\xA6\xD2\xA5\xBB\xAF\xB8\xA4\xBD\xA7G\xC4\xE6\xBF\xEC\xB2z\xB5\xF9\xA5U" //如果要提昇權限，請參考本站公佈欄辦理註冊
      "\x1B[21;1H\x1B[K\x1B[24;1H\xBD\xD0\xBF\xEF\xBE\xDC (1)\xAD\xD7\xA7\xEF\xB8\xEA\xAE\xC6 (2)\xB3]\xA9w\xB1K\xBDX (M)\xC1p\xB5\xB8\xABH\xBD" "c (V)\xBB{\xC3\xD2\xB8\xEA\xAE\xC6 [0]\xB5\xB2\xA7\xF4 " //請選擇 (1)修改資料 (2)設定密碼 (M)聯絡信箱 (V)認證資料 [0]結束
      "\x1B[0;7m   \x1B[m\x1B[24;64H\r\xBD\xD0\xBF\xEF\xBE\xDC (1)\xAD\xD7\xA7\xEF\xB8\xEA\xAE\xC6 (2)\xB3]\xA9w\xB1K\xBDX (M)\xC1p\xB5\xB8\xABH\xBD" "c (V)\xBB{\xC3\xD2\xB8\xEA\xAE\xC6 [0]\xB5\xB2\xA7\xF4 "
      "\x1B[0;7m0  \x1B[m\x1B[24;65H\r"
      "\0";

  int n_remove_array = 8;
  int to_remove_array[] = {323, 324, 325, 438, 439, 440, 447, 448};

  for(int i = 0; i < n_remove_array; i++) {
    expected[to_remove_array[i]] = OFLUSH_BUF[to_remove_array[i]];
  }

  fprintf(stderr, "[user_test11] expected: %s\n", expected);

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);
}
