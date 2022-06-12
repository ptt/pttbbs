#include "admin_test.h"

TEST_F(AdminTest, MergeDir) {
  char dir1[TEST_BUFFER_SIZE] = {};
  char dir2[TEST_BUFFER_SIZE] = {};

  setbfile(dir1, "WhoAmI\0", FN_DIR);
  setbfile(dir2, "Note\0", FN_DIR);

  setbtotal(10);
  setbtotal(8);

  for(int i = 0; i <= 10; i++) {
    fprintf(stderr, "[admin_test9] (%d/%d): total: %d\n", i, 10, SHM->total[i]);
  }

  EXPECT_EQ(SHM->total[9], 2);
  EXPECT_EQ(SHM->total[7], 116);

  merge_dir(dir1, dir2, 0);

  setbtotal(10);
  EXPECT_EQ(SHM->total[9], 118);
}
