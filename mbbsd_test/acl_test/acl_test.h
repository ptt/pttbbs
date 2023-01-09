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

// acl
class AclTest : public ::testing::Test {
 protected:
  void SetUp() override {
    system("echo \"(start) pwd:\" && pwd");
    fprintf(stderr, "BBSHOME: " BBSHOME "\n");

    getcwd(pwd_path, sizeof(pwd_path));

    system("mkdir -p " BBSHOME);
    system("rm -r " BBSHOME "/home");
    system("rm -r " BBSHOME "/etc");
    system("rm  " BBSHOME "/.PASSWDS");
    system("rm " BBSHOME "/.BRD");

    system("ls -alR ./testcase/home1");
    system("cp -R ./testcase/home1 " BBSHOME "/home");
    system("cp -R ./testcase/etc " BBSHOME "/etc");
    system("cp -R ./testcase/etc1 " BBSHOME "/etc1");
    system("cp ./testcase/.PASSWDS1 " BBSHOME "/.PASSWDS");
    system("cp ./testcase/.BRD1 " BBSHOME "/.BRD");
    system("echo \"pwd:\" && pwd");

    // chdir
    chdir(BBSHOME);
    system("echo \"(after chdir BBSHOME) pwd:\" && pwd");
    setup_root_link((char *)BBSHOME);
    system("echo \"(after setup_root_link BBSHOME) pwd:\" && pwd");
    typeahead(TYPEAHEAD_NONE);

    // load uhash
    load_uhash();

    // load board
    check_SHM();
    reload_bcache();

    // init scr / io
    initscr();
    if(vin.buf == NULL) {
      init_io();
    } else {
      vbuf_clear(pvin);
      vbuf_clear(pvout);
    }
  }

  void TearDown() override {
    // reset scr / io
    reset_oflush_buf();

    // chdir
    chdir(pwd_path);
  }

  char pwd_path[TEST_BUFFER_SIZE];
};
