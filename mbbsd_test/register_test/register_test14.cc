#include "register_test.h"

TEST_F(RegisterTest, RegformEstimateQueuesize) {
  char buf[8192] = {};

  // init SYSOP2
  initcuser("SYSOP2");
  fprintf(stderr, "[register_test14] after initcuser\n");

  refresh();
  log_oflush_buf();
  reset_oflush_buf();

  sprintf(buf, "y\r\ntest check\r\ntestcareer\r\ntestaddr1234567\xA5\xAB" "12\r\ny\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  u_manual_verification();

  refresh();
  log_oflush_buf();
  reset_oflush_buf();

  initcuser("SYSOP10");
  fprintf(stderr, "[register_test14] after initcuser\n");

  refresh();
  log_oflush_buf();
  reset_oflush_buf();

  sprintf(buf, "y\r\ntest check\r\ntestcareer\r\ntestaddr1234567\xA5\xAB" "12\r\ny\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  u_manual_verification();

  refresh();
  log_oflush_buf();
  reset_oflush_buf();

  initcuser("test61133");
  fprintf(stderr, "[register_test14] after initcuser\n");

  refresh();
  log_oflush_buf();
  reset_oflush_buf();

  sprintf(buf, "y\r\ntest check\r\ntestcareer\r\ntestaddr1234567\xA5\xAB" "12\r\ny\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  u_manual_verification();

  refresh();
  log_oflush_buf();
  reset_oflush_buf();

  int sz = regform_estimate_queuesize();
  EXPECT_EQ(sz, 1);
}
