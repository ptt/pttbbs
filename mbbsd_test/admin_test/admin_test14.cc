#include "admin_test.h"

TEST_F(AdminTest, MakeBoardLink) {
  int the_class = 2;
  char bname[TEST_BUFFER_SIZE];
  sprintf(bname, "WhoAmI");
  int ret = make_board_link(bname, the_class);
  EXPECT_EQ(ret, 10);
  EXPECT_EQ(SHM->Bnumber, 13);

  sprintf(bname, "WhoAm~");
  boardheader_t *board = getbcache(13);
  EXPECT_STREQ(board->brdname, bname);
}
