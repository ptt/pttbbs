#include "user_test.h"

TEST_F(UserTest, UserDisplay) {
  // load SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[user_test5] after initcuser\n");

  refresh();
  reset_oflush_buf();

  userec_t user = {};
  int uid = passwd_load_user("SYSOP10\0", &user);
  EXPECT_EQ(uid, 26);
  user_display(&user, 0);

  unsigned char expected[] = ""
      "        \x1B[30;41m\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\x1B[m  " //┴┬┴┬┴┬
      "\x1B[1;30;45m    \xA8\xCF \xA5\xCE \xAA\xCC \xB8\xEA \xAE\xC6             " //    使 用 者 資 料
      "\x1B[m  \x1B[30;41m\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\x1B[m\n\r" //┴┬┴┬┴┬
      "        \xA5N\xB8\xB9\xBC\xCA\xBA\xD9: SYSOP10 ()\n\r" //        代號暱稱: SYSOP10 ()
      "        \xAFu\xB9\xEA\xA9m\xA6W:  \n\r" //        真實姓名:
      "        \xC2\xBE\xB7~\xBE\xC7\xBE\xFA: \n\r" //        職業學歷:
      "        \xA9~\xA6\xED\xA6" "a\xA7}: \n\r" //        居住地址:
      "        \xC1p\xB5\xB8\xABH\xBD" "c: devptt12346@gmail.com\n\r" //        聯絡信箱: devptt12346@gmail.com
      "           Ptt\xB9\xF4: 0 Ptt\xB9\xF4\n\r" //           Ptt幣: 0 Ptt幣
      "        \xACO\xA7_\xA6\xA8\xA6~: \xA4w\xBA\xA1" "18\xB7\xB3\n\r" //        是否成年: 已滿18歲
      "        \xB5\xF9\xA5U\xA4\xE9\xB4\xC1: 01/25/2021 04:17:24 Mon (\xA4w\xBA\xA1 -18652 \xA4\xD1)\n\r" //        註冊日期: 01/25/2021 04:17:24 Mon (已滿 -18652 天)
      "        \xA8p\xA4H\xABH\xBD" "c: 0 \xAB\xCA  (\xC1\xCA\xB6R\xABH\xBD" "c: 0 \xAB\xCA)\n\r" //        私人信箱: 0 封  (購買信箱: 0 封)
      "        \xA8\xCF\xA5\xCE\xB0O\xBF\xFD: \xB5n\xA4J\xA6\xB8\xBC\xC6 19 \xA6\xB8 / \xA4\xE5\xB3\xB9 0 \xBDg\n\r" //        使用記錄: 登入次數 19 次 / 文章 0 篇
      "        \xB0\xB1\xAF" "d\xB4\xC1\xB6\xA1: 0 \xA4p\xAE\xC9  0 \xA4\xC0\n\r" //        停留期間: 0 小時  0 分
      "        \xB0h\xA4\xE5\xBC\xC6\xA5\xD8: 0\n\r" //        退文數目: 0
      "        \x1B[30;41m\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\xA2r\xA2s\x1B[m\n\r" //┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬
      "\xA6p\xAAG\xADn\xB4\xA3\xAA@\xC5v\xAD\xAD\xA1" "A\xBD\xD0\xB0\xD1\xA6\xD2\xA5\xBB\xAF\xB8\xA4\xBD\xA7G\xC4\xE6\xBF\xEC\xB2z\xB5\xF9\xA5U" //如果要提昇權限，請參考本站公佈欄辦理註冊
      "\0";

  refresh();

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);
}
