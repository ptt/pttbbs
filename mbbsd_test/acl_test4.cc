#include "acl_test.h"

TEST_F(AclTest, EditUserAclForBoard) {
  char buf[TEST_BUFFER_SIZE] = {};
  sprintf(buf, "  y\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  // load SYSOP2
  load_current_user("SYSOP2");
  fprintf(stderr, "[AclTest.EditUserAclForBoard] after load current_user\n");
  reset_oflush_buf();

  char user0[] = "SYSOP10\0";
  char board0[] = "WhoAmI\0";

  vkey_purge();
  bzero(buf, TEST_BUFFER_SIZE);
  sprintf(buf, "w\r\n3m\r\ntest\r\ny\r\nq\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  time4_t the_now = now;
  char board[] = "WhoAmI\0";
  int ret = edit_user_acl_for_board(user0, board);
  fprintf(stderr, "[AclTest.EditUserAclForBoard]: after edit_user_acl_for_board\n");
  EXPECT_EQ(0, ret);
  reset_oflush_buf();

  time4_t ret_t = is_user_banned_by_board(user0, board0);
  EXPECT_LT(the_now + 3 * 28 * 86400, ret_t);

  vkey_purge();
  bzero(buf, TEST_BUFFER_SIZE);
  sprintf(buf, "u\r\ntest\r\ny\r\nq\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  ret = edit_user_acl_for_board(user0, board);
  fprintf(stderr, "[AclTest.EditUserAclForBoard]: after edit_user_acl_for_board\n");
  EXPECT_EQ(0, ret);

  ret_t = is_user_banned_by_board(user0, board0);
  EXPECT_EQ(0, ret_t);
}
