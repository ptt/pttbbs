#include "verifydb_test.h"

TEST_F(VerifydbTest, VerifydbGetuser) {
  // verifydb get user

  char userid[] = "SYSOP10\0";
  int64_t generation = 1234567890;
  Bytes buf = {};
  const VerifyDb::GetReply *reply = NULL;

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

  char vkey[] = "test@test.test\0";
  ret_set = verifydb_set((char *)userid, generation, VMETHOD_EMAIL, vkey, 1234567891);
  EXPECT_EQ(ret_set, 0);

  ret = verifydb_getuser((char *)userid, generation, &buf, &reply);
  EXPECT_EQ(ret, true);

  EXPECT_NE(reply->entry(), (flatbuffers::Vector<flatbuffers::Offset<VerifyDb::Entry>> *)NULL);
  count = 0;
  for (const auto *ent : *reply->entry()) {
    fprintf(stderr, "[verfiydb_test1] (%d) user_id: %s generation: %ld vmethod: %d vkey: %s ts: %ld\n", count, ent->userid()->c_str(), ent->generation(), ent->vmethod(), ent->vkey()->c_str(), ent->timestamp());
    count++;
  }
  EXPECT_EQ(count, 1);

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
