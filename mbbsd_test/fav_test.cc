#include <gtest/gtest.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "bbs.h"
#include "testutil.h"
#include "testmock.h"
#include "mbbsd_s.c"

// fav-load
class FavLoadTest : public ::testing::Test {
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

    // init scr / io
    initscr();
    if(vin.buf == NULL) {
      init_io();
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

static void _log_fav(fav_t *fp, int level);

static void _log_fav_type(fav_type_t *favh, int level) {
  if(favh == NULL) {
    fprintf(stderr, "[_log_fav_type](%d): NULL\n", level);
    return;
  }

  fprintf(stderr, "[_log_fav_type](%d): type: %d attr: %d\n", level, favh->type, favh->attr);

  fav_board_t *fav_board = NULL;
  fav_folder_t *fav_folder = NULL;
  fav_line_t *fav_line = NULL;

  if(favh->fp == NULL) {
    fprintf(stderr, "[_log_fav_type](%d): fp NULL\n", level);
    return;
  }

  switch(favh->type) {
    case FAVT_BOARD:
      fav_board = (fav_board_t *)favh->fp;
      fprintf(stderr, "[_log_fav_type](%d, BOARD): bid: %d lastvisit: %d attr: %d\n", level, fav_board->bid, fav_board->lastvisit, fav_board->attr);
      break;
    case FAVT_LINE:
      fav_line = (fav_line_t *)favh->fp;
      fprintf(stderr, "[_log_fav_type](%d, LINE): lid: %d\n", level, fav_line->lid);
      break;
    case FAVT_FOLDER:
      fav_folder = (fav_folder_t *)favh->fp;
      fprintf(stderr, "[_log_fav_type](%d, FOLDER): fid: %d title: %s\n", level, fav_folder->fid, fav_folder->title);
      _log_fav(fav_folder->this_folder, level+1);
      break;
  }
}

static void _log_fav(fav_t *fp, int level) {
  if(fp == NULL) {
    fprintf(stderr, "[_log_fav](%d): NULL\n", level);
    return;
  }
  fprintf(stderr, "[_log_fav](%d): nAllocs: %d DataTail: %d nBoards: %d nLines: %d nFolders: %d lineID: %d folderID: %d\n", level, fp->nAllocs, fp->DataTail, fp->nBoards, fp->nLines, fp->nFolders, fp->lineID, fp->folderID);

  fav_type_t *ft = NULL;
  for(int i = 0; i < fp->DataTail; i++) {
    ft = &fp->favh[i];
    _log_fav_type(ft, level);
  }
}

TEST_F(FavLoadTest, Basic) {
  char buf[20] = {};
  sprintf(buf, "  y\r\n");
  put_vin((unsigned char *)buf, strlen(buf));

  // fav load of SYSOP2
  load_current_user("SYSOP2");
  fprintf(stderr, "after load current_user\n");

  fav_load();
  fav_t *fav_root = get_fav_root();

  EXPECT_NE(fav_root, (fav_t *)NULL);

  _log_fav(fav_root, 0);

  EXPECT_EQ(fav_root->nAllocs, 14);
  EXPECT_EQ(fav_root->DataTail, 6);
  EXPECT_EQ(fav_root->nBoards, 3);
  EXPECT_EQ(fav_root->nLines, 2);
  EXPECT_EQ(fav_root->nFolders, 1);
  EXPECT_EQ(fav_root->lineID, 2);
  EXPECT_EQ(fav_root->folderID, 1);

  fav_type_t *ft0 = &fav_root->favh[0];
  EXPECT_EQ(ft0->type, FAVT_BOARD);
  EXPECT_EQ(ft0->attr, FAVH_FAV);
  fav_board_t *fboard0 = (fav_board_t *)ft0->fp;
  EXPECT_EQ(fboard0->bid, 1);
  EXPECT_EQ(fboard0->lastvisit, 0);
  EXPECT_EQ(fboard0->attr, 0);

  fav_type_t *ft1 = &fav_root->favh[1];
  EXPECT_EQ(ft1->type, FAVT_LINE);
  EXPECT_EQ(ft1->attr, FAVH_FAV);
  fav_line_t *fline1 = (fav_line_t *)ft1->fp;
  EXPECT_EQ(fline1->lid, 1);

  fav_type_t *ft2 = &fav_root->favh[2];
  EXPECT_EQ(ft2->type, FAVT_FOLDER);
  EXPECT_EQ(ft2->attr, FAVH_FAV);
  fav_folder_t *ffolder2 = (fav_folder_t *)ft2->fp;
  EXPECT_EQ(ffolder2->fid, 1);
  EXPECT_STREQ(ffolder2->title, "·sªº¥Ø¿ý");

  fav_t *fav2 = ffolder2->this_folder;
  EXPECT_EQ(fav2->nAllocs, 9);
  EXPECT_EQ(fav2->DataTail, 1);
  EXPECT_EQ(fav2->nBoards, 1);
  EXPECT_EQ(fav2->nLines, 0);
  EXPECT_EQ(fav2->nFolders, 0);
  EXPECT_EQ(fav2->lineID, 0);
  EXPECT_EQ(fav2->folderID, 0);

  fav_type_t *ft2_0 = &fav2->favh[0];
  EXPECT_EQ(ft2_0->type, FAVT_BOARD);
  EXPECT_EQ(ft2_0->attr, FAVH_FAV);
  fav_board_t *fboard2_0 = (fav_board_t *)ft2_0->fp;
  EXPECT_EQ(fboard2_0->bid, 1);
  EXPECT_EQ(fboard2_0->lastvisit, 0);
  EXPECT_EQ(fboard2_0->attr, 0);

  fav_type_t *ft3 = &fav_root->favh[3];
  EXPECT_EQ(ft3->type, FAVT_LINE);
  EXPECT_EQ(ft3->attr, FAVH_FAV);
  fav_line_t *fline3 = (fav_line_t *)ft3->fp;
  EXPECT_EQ(fline3->lid, 2);

  fav_type_t *ft4 = &fav_root->favh[4];
  EXPECT_EQ(ft4->type, FAVT_BOARD);
  EXPECT_EQ(ft4->attr, FAVH_FAV);
  fav_board_t *fboard4 = (fav_board_t *)ft4->fp;
  EXPECT_EQ(fboard4->bid, 9);
  EXPECT_EQ(fboard4->lastvisit, 0);
  EXPECT_EQ(fboard4->attr, 0);

  fav_type_t *ft5 = &fav_root->favh[5];
  EXPECT_EQ(ft5->type, FAVT_BOARD);
  EXPECT_EQ(ft5->attr, FAVH_FAV);
  fav_board_t *fboard5 = (fav_board_t *)ft5->fp;
  EXPECT_EQ(fboard5->bid, 8);
  EXPECT_EQ(fboard5->lastvisit, 0);
  EXPECT_EQ(fboard5->attr, 0);
}
