#include "verifydb_test.h"

TEST_F(VerifydbTest, VerifydbCountByVerify) {
  // verifydb get user

  char userid[] = "SYSOP10\0";
  char userid1[] = "SYSOP2\0";
  int64_t generation = 1611548244;
  int64_t generation1 = 1608069209;
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

  ret_set = verifydb_set((char *)userid1, generation1, VMETHOD_EMAIL, NULL, 1234567891);
  EXPECT_EQ(ret_set, 0);

  ret = verifydb_getuser((char *)userid1, generation1, &buf, &reply);
  EXPECT_EQ(ret, true);

  EXPECT_NE(reply->entry(), (flatbuffers::Vector<flatbuffers::Offset<VerifyDb::Entry>> *)NULL);
  count = 0;
  for (const auto *ent : *reply->entry()) {
    fprintf(stderr, "[verfiydb_test1] (%d) user_id: %s generation: %ld vmethod: %d vkey: %s ts: %ld\n", count, ent->userid()->c_str(), ent->generation(), ent->vmethod(), ent->vkey()->c_str(), ent->timestamp());
    count++;
  }
  EXPECT_EQ(count, 0);

  // set vkey
  char vkey[] = "test@test.test\0";
  ret_set = verifydb_set((char *)userid, generation, VMETHOD_EMAIL, vkey, 1234567891);
  EXPECT_EQ(ret_set, 0);

  ret_set = verifydb_set((char *)userid1, generation1, VMETHOD_EMAIL, vkey, 1234567892);
  EXPECT_EQ(ret_set, 0);

  int count_self = 0;
  int count_other = 0;
  ret = verifydb_count_by_verify(VMETHOD_EMAIL, vkey, &count_self, &count_other);
  EXPECT_EQ(ret, 0);

  EXPECT_EQ(count_self, 1);
  EXPECT_EQ(count_other, 1);

  // reset
  ret_set = verifydb_set((char *)userid1, generation1, VMETHOD_EMAIL, NULL, 1234567891);

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
