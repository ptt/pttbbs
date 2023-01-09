#include "register_test.h"

TEST_F(RegisterTest, CheckEmailAllowRejectLists) {
  char buf[20] = {};
  sprintf(buf, "  y\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  // load SYSOP2
  load_current_user("SYSOP2");
  fprintf(stderr, "[register_test7] after load current_user\n");

  char *errmsg = NULL;
  char *notice_file = NULL;
  char email0[] = "aaa@ntu.edu.tw\0";
  bool is_valid = check_email_allow_reject_lists(email0, (const char **)&errmsg, (const char **)&notice_file);
  EXPECT_EQ(is_valid, true);
  EXPECT_EQ((long)errmsg, NULL);
  EXPECT_EQ((long)notice_file, NULL);

  char email1[] = "aaa@yam.com\0";
  is_valid = check_email_allow_reject_lists(email1, (const char **)&errmsg, (const char **)&notice_file);
  EXPECT_EQ(is_valid, false);
  EXPECT_NE((long)errmsg, NULL);
  EXPECT_EQ((long)notice_file, NULL);
  EXPECT_STREQ(errmsg, "\xA6\xB9\xABH\xBD" "c\xA4w\xB3Q\xB8T\xA4\xEE\xA5\xCE\xA9\xF3\xB5\xF9\xA5U (\xA5i\xAF\xE0\xACO\xA7K\xB6O\xABH\xBD" "c)"); //此信箱已被禁止用於註冊 (可能是免費信箱)
  //EXPECT_STREQ(errmsg, "\xa9\xea\xba" "p\xa1" "A\xa5\xd8\xab" "e\xa4\xa3\xb1\xb5\xa8\xfc\xa6\xb9 Email \xaa\xba\xb5\xf9\xa5" "U\xa5\xd3\xbd\xd0\xa1" "C\0"); // 抱歉，目前不接受此 Email 的註冊申請。

  char email2[] = "aaa@yahoo.com\0";
  is_valid = check_email_allow_reject_lists(email2, (const char **)&errmsg, (const char **)&notice_file);
  EXPECT_EQ(is_valid, false);
  EXPECT_NE((long)errmsg, NULL);
  EXPECT_NE((long)notice_file, NULL);
  EXPECT_STREQ(errmsg, "\xa9\xea\xba" "p\xa1" "A\xa5\xd8\xab" "e\xa4\xa3\xb1\xb5\xa8\xfc\xa6\xb9 Email \xaa\xba\xb5\xf9\xa5" "U\xa5\xd3\xbd\xd0\xa1" "C\0"); // 抱歉，目前不接受此 Email 的註冊申請。
  EXPECT_STREQ(notice_file, FN_NOTIN_WHITELIST_NOTICE "\0");
}
