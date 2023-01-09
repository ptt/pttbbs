#include "admin_test.h"

TEST_F(AdminTest, SetupMan) {
  boardheader_t board = {};
  sprintf(board.brdname, "WhoAmI");
  setup_man(&board, NULL);

  char expected_filename[] = "man/boards/W/WhoAmI";
  EXPECT_TRUE(dashd(expected_filename));
}
