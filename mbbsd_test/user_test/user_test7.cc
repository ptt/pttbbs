#include "user_test.h"

TEST_F(UserTest, KickAll) {
  char buf[8192] = {};
  // load SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[user_test7] after initcuser\n");

  refresh();
  reset_oflush_buf();
}
