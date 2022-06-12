#pragma once

#include <gtest/gtest.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "bbs.h"
#include "testutil.h"
#include "testmock.h"
#include "mbbsd_s.c"

// admin
class AdminTest : public ::testing::Test {
 protected:
  void SetUp() override {
    system("echo \"(start) pwd:\" && pwd");
    fprintf(stderr, "BBSHOME: " BBSHOME "\n");

    getcwd(pwd_path, sizeof(pwd_path));

    system("mkdir -p " BBSHOME);
    system("rm -r " BBSHOME "/home");
    system("rm -r " BBSHOME "/boards");
    system("rm -r " BBSHOME "/man");
    system("rm -r " BBSHOME "/etc");
    system("rm  " BBSHOME "/.PASSWDS");
    system("rm " BBSHOME "/.BRD");

    system("cp -R ./testcase/home1 " BBSHOME "/home");
    system("cp -R ./testcase/boards1 " BBSHOME "/boards");
    system("cp -R ./testcase/man1 " BBSHOME "/man");
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
    fprintf(stderr, "[admin_test]: to reload_bcache\n");
    reload_bcache();
    fprintf(stderr, "[admin_test]: after reload_bcache\n");

    // init scr / io
    initscr();
    if(vin.buf == NULL) {
      init_io();
    } else {
      vbuf_clear(pvin);
      vbuf_clear(pvout);
    }

    int argc = 2;

    // parse argv
    char **argv = (char **)malloc(sizeof(char *) * argc);
    for(int i = 0; i < argc; i++) {
      argv[i] = (char *)malloc(sizeof(char) * TEST_BUFFER_SIZE);
    }

    sprintf(argv[0], "mbbsd");
    sprintf(argv[1], "-h127.0.0.1");

    for(int i = 0; i < argc; i++) {
      fprintf(stderr, "[admin_test1.Mloginmsg: (%d/%d) argv: %s\n", i, argc, argv[i]);
    }

    ProgramOption opt = {};
    fprintf(stderr, "[admin_test1.Mloginmsg] to parse_argv\n");
    parse_argv(argc, argv, &opt);
    fprintf(stderr, "[admin_test1.Mloginmsg] after parse_argv\n");

    for(int i = 0; i < argc; i++) {
      free(argv[i]);
    }
    free(argv);

    login_start_time = time(NULL);

    // load SYSOP2
    char buf[TEST_BUFFER_SIZE] = {};
    sprintf(buf, "  y\r\n");
    put_vin((unsigned char *)buf, strlen(buf));

    load_current_user("SYSOP2");
    vkey_purge();
    fprintf(stderr, "admin_test1.MLoginmsg: after load_current_user\n");
    sprintf(buf, "y");
    put_vin((unsigned char *)buf, strlen(buf));
    user_login();
    vkey_purge();
    reset_oflush_buf();
  }

  void TearDown() override {
    // reset scr / io
    reset_oflush_buf();

    // reset_SHM (load uhash)
    reset_SHM();

    // chdir
    chdir(pwd_path);
  }

  char pwd_path[TEST_BUFFER_SIZE];
};
