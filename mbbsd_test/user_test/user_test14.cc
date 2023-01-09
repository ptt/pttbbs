#include "user_test.h"

TEST_F(UserTest, Showsignature) {
  // load SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[user_test14] after initcuser\n");

  refresh();
  reset_oflush_buf();

  char filename[8192] = {};
  int j = 0;
  SigInfo si = {};
  int ret = showsignature(filename, &j, &si);

  EXPECT_EQ(t_lines, 24);
  EXPECT_EQ(ret, 4);
  EXPECT_STREQ(filename, "home/S/SYSOP2/sig.9\0");
  EXPECT_EQ(j, 18);

  EXPECT_EQ(si.total, 4);
  EXPECT_EQ(si.max, 4);
  EXPECT_EQ(si.show_start, 0);
  EXPECT_EQ(si.show_max, 3);
}
