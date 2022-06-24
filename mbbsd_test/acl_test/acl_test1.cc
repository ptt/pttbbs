#include "acl_test.h"

TEST_F(AclTest, BanUserForBoard) {
  char buf[TEST_BUFFER_SIZE] = {};
  sprintf(buf, "  y\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  // load SYSOP10
  load_current_user("SYSOP10\0");
  fprintf(stderr, "[AclTest.BanUserForBoard] after load current_user\n");
  vkey_purge();

  char user0[] = "SYSOP10\0";
  char board0[] = "WhoAmI\0";
  time4_t expire = now + 1;
  char reason[] = "test\0";

  int ret = ban_user_for_board(user0, board0, expire, reason);

  EXPECT_EQ(ret, 1);

  time4_t ret_t = is_user_banned_by_board(user0, board0);
  EXPECT_EQ(ret_t, expire);

  ret_t = is_banned_by_board(board0);
  EXPECT_EQ(ret_t, expire);

  bzero(buf, TEST_BUFFER_SIZE);
  ret = get_user_banned_status_by_board(user0, board0, TEST_BUFFER_SIZE, buf);
  EXPECT_EQ(ret, expire);
  EXPECT_STREQ(buf, reason);

  ret = unban_user_for_board(user0, board0);
  EXPECT_EQ(ret, 1);

  ret_t = is_user_banned_by_board(user0, board0);
  EXPECT_EQ(ret_t, 0);

  ret_t = is_banned_by_board(board0);
  EXPECT_EQ(ret_t, 0);
}
