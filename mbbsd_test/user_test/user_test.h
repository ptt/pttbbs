#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "bbs.h"
#include "testutil.h"
#include "testmock.h"
#include "mbbsd_s.c"

class UserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    system("echo \"(start) pwd:\" && pwd");
    fprintf(stderr, "BBSHOME: " BBSHOME "\n");

    system("mkdir -p " BBSHOME);
    system("rm -r " BBSHOME "/home");
    system("rm -r " BBSHOME "/etc");
    system("rm -r " BBSHOME "/log");
    system("rm -r " BBSHOME "/tmp");
    system("rm  " BBSHOME "/.PASSWDS");
    system("rm " BBSHOME "/.BRD");

    system("cp -RL ./testcase/home1 " BBSHOME "/home");
    system("cp -RL ./testcase/etc1 " BBSHOME "/etc");
    system("cp ./testcase/.PASSWDS1 " BBSHOME "/.PASSWDS");
    system("cp ./testcase/.BRD1 " BBSHOME "/.BRD");
    system("mkdir -p " BBSHOME "/log");
    system("mkdir -p " BBSHOME "/tmp");
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
