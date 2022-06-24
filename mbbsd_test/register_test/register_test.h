#pragma once

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
    system("rm -r " BBSHOME "/log");
    system("rm  " BBSHOME "/.PASSWDS");
    system("rm " BBSHOME "/.BRD");

    system("cp -R ./testcase/home1 " BBSHOME "/home");
    system("cp -R ./testcase/etc " BBSHOME "/etc");
    system("cp -R ./testcase/etc1 " BBSHOME "/etc1");
    system("cp ./testcase/.PASSWDS1 " BBSHOME "/.PASSWDS");
    system("cp ./testcase/.BRD1 " BBSHOME "/.BRD");
    system("mkdir -p " BBSHOME "/log");
    system("echo \"pwd:\" && pwd");

    // chdir
    chdir(BBSHOME);
    system("echo \"(after chdir BBSHOME) pwd:\" && pwd");
    setup_root_link((char *)BBSHOME);
    system("echo \"(after setup_root_link BBSHOME) pwd:\" && pwd");
    typeahead(TYPEAHEAD_NONE);

    // load uhash
    load_uhash();

    // init scr / io
    initscr();
    if(vin.buf == NULL) {
      init_io();
    }
  }

  void TearDown() override {
    // reset scr / io
    reset_oflush_buf();

    // reset SHM
    fprintf(stderr, "[register_test.TearDown] to reset_SHM\n");
    reset_SHM();

    //rm files
    if(dashf(FN_REQLIST)) {
      remove(FN_REQLIST);
    }

    // chdir
    chdir(pwd_path);
  }

  char pwd_path[TEST_BUFFER_SIZE];
};
