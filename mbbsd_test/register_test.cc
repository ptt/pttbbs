#include <gtest/gtest.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "bbs.h"
#include "testutil.h"
#include "testmock.h"
#include "mbbsd_s.c"

// accept-user-aggrement
class RegisterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    system("echo \"(start) pwd:\" && pwd");
    fprintf(stderr, "BBSHOME: " BBSHOME "\n");

    system("mkdir -p " BBSHOME);
    system("rm -r " BBSHOME "/home");
    system("rm -r " BBSHOME "/etc");
    system("rm  " BBSHOME "/.PASSWDS");
    system("rm " BBSHOME "/.BRD");

    system("cp -R ./testcase/home1 " BBSHOME "/home");
    system("cp -R ./testcase/etc " BBSHOME "/etc");
    system("cp -R ./testcase/etc1 " BBSHOME "/etc1");
    system("cp ./testcase/.PASSWDS1 " BBSHOME "/.PASSWDS");
    system("cp ./testcase/.BRD1 " BBSHOME "/.BRD");
    system("echo \"pwd:\" && pwd");

    chdir(BBSHOME);
    system("echo \"(after chdir BBSHOME) pwd:\" && pwd");
    setup_root_link((char *)BBSHOME);
    system("echo \"(after setup_root_link BBSHOME) pwd:\" && pwd");

    load_uhash();
    initscr();
    if(vin.buf == NULL) {
      init_io();
    }
  }

  void TearDown() override {
    reset_oflush_buf();
  }
};

TEST_F(RegisterTest, EnsureUserAgreementVerion) {
  char buf[20] = {};
  sprintf(buf, "  y\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  // fav load of SYSOP2
  load_current_user("SYSOP2");
  fprintf(stderr, "[register_test] after load current_user\n");

  put_vin((unsigned char *)buf, strlen(buf));

  system("rm -r " BBSHOME "/etc");
  system("cp -R " BBSHOME "/etc1 " BBSHOME "/etc");

  ensure_user_agreement_version();

  // refresh is in empty pvin, which never happened in test.
  // we need to manually do refresh to get the OFLUSH_BUF.
  refresh();
  log_oflush_buf();

  char expected[] = {
    '\x1B', '\x5B', '\x48', '\x1B', '\x5B', '\x4A', //\x1b[H\x1b[J (clearbuf)
    '\x54', '\x68', '\x69', '\x73', '\x20', '\x69', '\x73', '\x20', '\x75', '\x73', '\x65', '\x72', '\x20', '\x61',
    '\x67', '\x72', '\x65', '\x65', '\x6D', '\x65', '\x6E', '\x74', //This is user agreement
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D', //\x1b[K (cleolbuf)
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\x1B', '\x5B', '\x4B', '\x0A', '\x0D',
    '\xBD', '\xD0', '\xB0', '\xDD', '\xB1', '\x7A', // 請問您
    '\xB1', '\xB5', '\xA8', '\xFC', '\xB3', '\x6F', '\xA5', '\xF7', '\xA8', '\xCF', // 接受這份使
    '\xA5', '\xCE', '\xAA', '\xCC', '\xB1', '\xF8', '\xB4', '\xDA', '\xB6', '\xDC', '\x3F', // 用者條款嗎?
    '\x20', '\x28', '\x79', '\x65', '\x73', '\x2F', '\x6E', '\x6F', '\x29', '\x20', //  (yes/no)
    '\x1B', '\x5B', '\x30', '\x3B', '\x37', '\x6D', //\x1b[0;7m
    '\x79', '\x20', '\x20', //y
    '\x1B', '\x5B', '\x6D', '\x0D', // \x1b[m\n
    '\0',
  };

  EXPECT_STREQ((char *)expected, (char *)OFLUSH_BUF);
}
