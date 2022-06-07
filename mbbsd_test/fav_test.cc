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

  EXPECT_NE((fav_t *)NULL, fav_root);

  _log_fav(fav_root, 0);

  EXPECT_EQ(14, fav_root->nAllocs);
  EXPECT_EQ(6, fav_root->DataTail);
  EXPECT_EQ(3, fav_root->nBoards);
  EXPECT_EQ(2, fav_root->nLines);
  EXPECT_EQ(1, fav_root->nFolders);
  EXPECT_EQ(2, fav_root->lineID);
  EXPECT_EQ(1, fav_root->folderID);

  fav_type_t *ft0 = &fav_root->favh[0];
  EXPECT_EQ(FAVT_BOARD, ft0->type);
  EXPECT_EQ(FAVH_FAV, ft0->attr);
  fav_board_t *fboard0 = (fav_board_t *)ft0->fp;
  EXPECT_EQ(1, fboard0->bid);
  EXPECT_EQ(0, fboard0->lastvisit);
  EXPECT_EQ(0, fboard0->attr);

  fav_type_t *ft1 = &fav_root->favh[1];
  EXPECT_EQ(FAVT_LINE, ft1->type);
  EXPECT_EQ(FAVH_FAV, ft1->attr);
  fav_line_t *fline1 = (fav_line_t *)ft1->fp;
  EXPECT_EQ(1, fline1->lid);

  fav_type_t *ft2 = &fav_root->favh[2];
  EXPECT_EQ(FAVT_FOLDER, ft2->type);
  EXPECT_EQ(FAVH_FAV, ft2->attr);
  fav_folder_t *ffolder2 = (fav_folder_t *)ft2->fp;
  EXPECT_EQ(1, ffolder2->fid);
  EXPECT_STREQ("·sªº¥Ø¿ý", ffolder2->title);

  fav_t *fav2 = ffolder2->this_folder;
  EXPECT_EQ(9, fav2->nAllocs);
  EXPECT_EQ(1, fav2->DataTail);
  EXPECT_EQ(1, fav2->nBoards);
  EXPECT_EQ(0, fav2->nLines);
  EXPECT_EQ(0, fav2->nFolders);
  EXPECT_EQ(0, fav2->lineID);
  EXPECT_EQ(0, fav2->folderID);

  fav_type_t *ft2_0 = &fav2->favh[0];
  EXPECT_EQ(FAVT_BOARD, ft2_0->type);
  EXPECT_EQ(FAVH_FAV, ft2_0->attr);
  fav_board_t *fboard2_0 = (fav_board_t *)ft2_0->fp;
  EXPECT_EQ(1, fboard2_0->bid);
  EXPECT_EQ(0, fboard2_0->lastvisit);
  EXPECT_EQ(0, fboard2_0->attr);

  fav_type_t *ft3 = &fav_root->favh[3];
  EXPECT_EQ(FAVT_LINE, ft3->type);
  EXPECT_EQ(FAVH_FAV, ft3->attr);
  fav_line_t *fline3 = (fav_line_t *)ft3->fp;
  EXPECT_EQ(2, fline3->lid);

  fav_type_t *ft4 = &fav_root->favh[4];
  EXPECT_EQ(FAVT_BOARD, ft4->type);
  EXPECT_EQ(FAVH_FAV, ft4->attr);
  fav_board_t *fboard4 = (fav_board_t *)ft4->fp;
  EXPECT_EQ(9, fboard4->bid);
  EXPECT_EQ(0, fboard4->lastvisit);
  EXPECT_EQ(0, fboard4->attr);

  fav_type_t *ft5 = &fav_root->favh[5];
  EXPECT_EQ(FAVT_BOARD, ft5->type);
  EXPECT_EQ(FAVH_FAV, ft5->attr);
  fav_board_t *fboard5 = (fav_board_t *)ft5->fp;
  EXPECT_EQ(8, fboard5->bid);
  EXPECT_EQ(0, fboard5->lastvisit);
  EXPECT_EQ(0, fboard5->attr);
}
