#include "admin_test.h"

TEST_F(AdminTest, DeleteBoardLink) {
  int the_class = 2;
  char bname[TEST_BUFFER_SIZE];
  sprintf(bname, "WhoAmI");
  int ret = make_board_link(bname, the_class);
  EXPECT_EQ(ret, 10);
  EXPECT_EQ(SHM->Bnumber, 13);

  sprintf(bname, "WhoAm~");
  boardheader_t *board = getbcache(13);
  boardheader_t board0 = *board;
  EXPECT_STREQ(board0.brdname, bname);
  EXPECT_TRUE(board0.brdattr & BRD_SYMBOLIC);

  int bid = 13;
  delete_board_link(&board0, bid);

  EXPECT_STREQ(board->brdname, "\0");
}
