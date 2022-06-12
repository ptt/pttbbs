#include "admin_test.h"

TEST_F(AdminTest, XFile) {
  char buf[TEST_BUFFER_SIZE] = {};

  bzero(buf, TEST_BUFFER_SIZE);
  sprintf(buf, "WhoAmI\r\nq\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  int ret = x_file();
  EXPECT_EQ(ret, 0);

  unsigned char expected[] = ""
      "\r\x1B[1;36;44m \xA1\xBB \xA5\xBC\xB3]\xA9w\xA5i\xBDs\xBF\xE8\xC0\xC9\xAE\xD7" // ◆ 未設定可編輯檔案
      "\xA6" "C\xAA\xED[etc/editable]\xA1" "A\xBD\xD0\xAC\xA2\xA8t\xB2\xCE\xAF\xB8\xAA\xF8\xA1" "C         " //列表[etc/editable]，請洽系統站長。
      "\x1B[1;33;46m [\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2] \x1B[m\x1B[K" // [按任意鍵繼續]
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);
}
