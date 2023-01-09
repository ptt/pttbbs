#include "verifydb_test.h"

TEST_F(VerifydbTest, VerifydbCheckVmethodUnused) {
  // verifydb get user

  char userid[] = "SYSOP10\0";
  int64_t generation = 1611548244;
  Bytes buf = {};
  const VerifyDb::GetReply *reply = NULL;

  // reset
  int ret_set = verifydb_set((char *)userid, generation, VMETHOD_EMAIL, NULL, 1234567891);
  EXPECT_EQ(ret_set, 0);

  ret_set = verifydb_set((char *)userid, generation, VMETHOD_SMS, NULL, 1234567891);
  EXPECT_EQ(ret_set, 0);

  bool ret = verifydb_getuser((char *)userid, generation, &buf, &reply);
  EXPECT_EQ(ret, true);

  EXPECT_NE(reply->entry(), (flatbuffers::Vector<flatbuffers::Offset<VerifyDb::Entry>> *)NULL);
  int count = 0;
  for (const auto *ent : *reply->entry()) {
    fprintf(stderr, "[verfiydb_test5] (%d) user_id: %s generation: %ld vmethod: %d vkey: %s ts: %ld\n", count, ent->userid()->c_str(), ent->generation(), ent->vmethod(), ent->vkey()->c_str(), ent->timestamp());
    count++;
  }
  EXPECT_EQ(count, 0);

  // set vkey
  char vkey_email[] = "test@test.test\0";
  ret_set = verifydb_set((char *)userid, generation, VMETHOD_EMAIL, vkey_email, 1234567891);
  EXPECT_EQ(ret_set, 0);

  char buf_vin[TEST_BUFFER_SIZE] = {};

  bzero(buf_vin, TEST_BUFFER_SIZE);
  sprintf(buf_vin, "y\r\n");
  put_vin((unsigned char *)buf_vin, strlen(buf_vin));

  bool is_unused = verifydb_check_vmethod_unused((char *)userid, generation, VMETHOD_EMAIL);
  EXPECT_EQ(is_unused, false);

  refresh();
  log_oflush_buf();

  unsigned char expected[] = ""
      "\r\x1B[1;36;44m"
      " \xA1\xBB \xB1z\xA4w\xB8g\xA8\xCF\xA5\xCE\xB9L\xA6\xB9\xA4\xE8\xA6\xA1\xBB{\xC3\xD2\xA1" "C                                   " // ◆ 您已經使用過此方式認證。
      "\x1B[1;33;46m"
      " [\xAB\xF6\xA5\xF4\xB7N\xC1\xE4\xC4~\xC4\xF2] " // [按任意鍵繼續]
      "\x1B[m\x1B[K\r\x1B[K"
      "\0";

  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected);

  // reset scr / io
  reset_oflush_buf();

  is_unused = verifydb_check_vmethod_unused((char *)userid, generation, VMETHOD_SMS);
  EXPECT_EQ(is_unused, true);

  refresh();
  log_oflush_buf();

  unsigned char expected2[] = ""
      "\0";
  EXPECT_STREQ((char *)OFLUSH_BUF, (char *)expected2);

  // reset
  ret_set = verifydb_set((char *)userid, generation, VMETHOD_EMAIL, NULL, 1234567891);
  EXPECT_EQ(ret_set, 0);

  ret_set = verifydb_set((char *)userid, generation, VMETHOD_SMS, NULL, 1234567891);
  EXPECT_EQ(ret_set, 0);

  ret = verifydb_getuser((char *)userid, generation, &buf, &reply);
  EXPECT_EQ(ret, true);

  EXPECT_NE(reply->entry(), (flatbuffers::Vector<flatbuffers::Offset<VerifyDb::Entry>> *)NULL);
  count = 0;
  for (const auto *ent : *reply->entry()) {
    fprintf(stderr, "[verfiydb_test1] (%d) user_id: %s generation: %ld vmethod: %d vkey: %s ts: %ld\n", count, ent->userid()->c_str(), ent->generation(), ent->vmethod(), ent->vkey()->c_str(), ent->timestamp());
    count++;
  }
  EXPECT_EQ(count, 0);
}
