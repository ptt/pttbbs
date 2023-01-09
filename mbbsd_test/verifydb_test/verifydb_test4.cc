#include "verifydb_test.h"

TEST_F(VerifydbTest, VerifydbChangeUserid) {
  // verifydb get user

  char userid[] = "SYSOP10\0";
  int64_t generation = 1611548244;
  Bytes buf = {};
  const VerifyDb::GetReply *reply = NULL;

  // reset
  int ret_set = verifydb_set((char *)userid, generation, VMETHOD_EMAIL, NULL, 1234567891);
  EXPECT_EQ(ret_set, 0);

  bool ret = verifydb_getuser((char *)userid, generation, &buf, &reply);
  EXPECT_EQ(ret, true);

  EXPECT_NE(reply->entry(), (flatbuffers::Vector<flatbuffers::Offset<VerifyDb::Entry>> *)NULL);
  int count = 0;
  for (const auto *ent : *reply->entry()) {
    fprintf(stderr, "[verfiydb_test1] (%d) user_id: %s generation: %ld vmethod: %d vkey: %s ts: %ld\n", count, ent->userid()->c_str(), ent->generation(), ent->vmethod(), ent->vkey()->c_str(), ent->timestamp());
    count++;
  }
  EXPECT_EQ(count, 0);

  // set vkey
  char vkey[] = "test@test.test\0";
  ret_set = verifydb_set((char *)userid, generation, VMETHOD_EMAIL, vkey, 1234567891);
  EXPECT_EQ(ret_set, 0);

  // change userid
  char userid1[] = "SYSOP11\0";
  ret = verifydb_change_userid((char *)userid, (char *)userid1, generation);
  EXPECT_EQ(ret, 0);

  // get SYSOP10
  ret = verifydb_getuser((char *)userid, generation, &buf, &reply);
  EXPECT_EQ(ret, true);

  EXPECT_NE(reply->entry(), (flatbuffers::Vector<flatbuffers::Offset<VerifyDb::Entry>> *)NULL);
  count = 0;
  for (const auto *ent : *reply->entry()) {
    fprintf(stderr, "[verfiydb_test1] (%d) user_id: %s generation: %ld vmethod: %d vkey: %s ts: %ld\n", count, ent->userid()->c_str(), ent->generation(), ent->vmethod(), ent->vkey()->c_str(), ent->timestamp());
    EXPECT_STRNE(ent->userid()->c_str(), userid);
    count++;
  }
  EXPECT_EQ(count, 0);

  // get SYSOP11
  ret = verifydb_getuser((char *)userid1, generation, &buf, &reply);
  EXPECT_EQ(ret, true);

  EXPECT_NE(reply->entry(), (flatbuffers::Vector<flatbuffers::Offset<VerifyDb::Entry>> *)NULL);
  count = 0;
  char expected_userid1[] = "sysop11\0";
  for (const auto *ent : *reply->entry()) {
    fprintf(stderr, "[verfiydb_test1] (%d) user_id: %s generation: %ld vmethod: %d vkey: %s ts: %ld\n", count, ent->userid()->c_str(), ent->generation(), ent->vmethod(), ent->vkey()->c_str(), ent->timestamp());
    EXPECT_STREQ(ent->userid()->c_str(), expected_userid1);
    EXPECT_STREQ(ent->vkey()->c_str(), vkey);
    count++;
  }
  EXPECT_EQ(count, 1);

  // reset
  ret_set = verifydb_set((char *)userid1, generation, VMETHOD_EMAIL, NULL, 1234567891);

  ret_set = verifydb_set((char *)userid, generation, VMETHOD_EMAIL, NULL, 1234567891);
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
