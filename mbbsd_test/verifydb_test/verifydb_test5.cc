#include "verifydb_test.h"

TEST_F(VerifydbTest, VerifydbFindVmethod) {
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

  char vkey_sms[] = "0912345678\0";
  ret_set = verifydb_set((char *)userid, generation, VMETHOD_SMS, vkey_sms, 1234567891);
  EXPECT_EQ(ret_set, 0);

  // get SYSOP10
  ret = verifydb_getuser((char *)userid, generation, &buf, &reply);
  EXPECT_EQ(ret, true);

  EXPECT_NE(reply->entry(), (flatbuffers::Vector<flatbuffers::Offset<VerifyDb::Entry>> *)NULL);
  count = 0;
  for (const auto *ent : *reply->entry()) {
    fprintf(stderr, "[verfiydb_test5] (%d) user_id: %s generation: %ld vmethod: %d vkey: %s ts: %ld\n", count, ent->userid()->c_str(), ent->generation(), ent->vmethod(), ent->vkey()->c_str(), ent->timestamp());
    EXPECT_STRNE(ent->userid()->c_str(), userid);
    count++;
  }
  EXPECT_EQ(count, 2);

  // find vmethod
  const char *ret_key_email = verifydb_find_vmethod(reply, VMETHOD_EMAIL);
  EXPECT_STREQ(ret_key_email, (char *)vkey_email);

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
