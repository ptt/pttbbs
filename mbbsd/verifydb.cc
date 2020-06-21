extern "C" {
#include "bbs.h"
#include "daemons.h"
}

#ifdef USE_VERIFYDB

#include <vector>

#include "flatbuffers/flatbuffers.h"
#include "verifydb.fbs.h"

namespace {

using Bytes = std::vector<uint8_t>;

bool verifydb_transact(const void *msg, size_t len, Bytes *out) {
  int fd = toconnect(REGMAILD_ADDR);
  if (fd < 0)
    return false;

  // Send request.
  verifydb_message_t rep = {};
  rep.header.cb = sizeof(rep.header) + len;
  rep.header.operation = VERIFYDB_MESSAGE;
  if (towrite(fd, &rep.header, sizeof(rep.header)) != sizeof(rep.header) ||
      towrite(fd, msg, len) != len)
    return false;

  // Read reply.
  if (toread(fd, &rep.header, sizeof(rep.header)) != sizeof(rep.header))
    return false;

  out->resize(rep.header.cb - sizeof(rep.header));
  if (toread(fd, &out->front(), out->size()) != out->size())
    return false;

  return true;
}

bool is_entry_user_present(const VerifyDb::Entry *entry) {
  const char *userid = "";
  if (entry->userid())
    userid = entry->userid()->c_str();
  if (!is_validuserid(userid))
    return false;

  userec_t u;
  if (passwd_load_user(userid, &u) < 0)
    return false;

  return entry->generation() == u.firstlogin;
}

}  // namespace

int verifydb_count_by_verify(int32_t vmethod, const char *vkey, int *count_self,
                             int *count_other) {
  // Create request.
  flatbuffers::FlatBufferBuilder fbb;
  auto entry = VerifyDb::CreateEntryDirect(fbb, nullptr, 0, vmethod, vkey);
  auto match_fields = fbb.CreateVector<int8_t>({VerifyDb::Field_Verify});
  auto req = VerifyDb::CreateMessage(
      fbb, VerifyDb::AnyMessage_GetRequest,
      VerifyDb::CreateGetRequest(fbb, entry, match_fields).Union());
  fbb.Finish(req);

  // Transact.
  Bytes out;
  if (!verifydb_transact(fbb.GetBufferPointer(), fbb.GetSize(), &out))
    return -1;

  // Verify reply.
  auto verifier = flatbuffers::Verifier(&out.front(), out.size());
  if (!VerifyDb::VerifyMessageBuffer(verifier))
    return -1;

  // Expect reply is a GetReply message.
  const auto *reply = VerifyDb::GetMessage(&out.front())->message_as_GetReply();
  if (!reply || !reply->ok() || !reply->entry())
    return -1;

  *count_self = 0;
  *count_other = 0;
  for (const auto *entry : *reply->entry()) {
    if (!is_entry_user_present(entry))
      continue;

    if (strcasecmp(cuser.userid, entry->userid()->c_str()) == 0)
      (*count_self)++;
    else
      (*count_other)++;
  }
  return 0;
}

int verifydb_set(const char *userid, int64_t generation, int32_t vmethod,
                 const char *vkey, int64_t timestamp) {
  // Create request.
  flatbuffers::FlatBufferBuilder fbb;
  auto entry = VerifyDb::CreateEntryDirect(fbb, userid, generation, vmethod,
                                           vkey, timestamp);
  auto req = VerifyDb::CreateMessage(
      fbb, VerifyDb::AnyMessage_PutRequest,
      VerifyDb::CreatePutRequest(fbb, entry, VerifyDb::Conflict_Replace)
          .Union());
  fbb.Finish(req);

  // Transact.
  Bytes out;
  if (!verifydb_transact(fbb.GetBufferPointer(), fbb.GetSize(), &out))
    return -1;

  // Verify reply.
  auto verifier = flatbuffers::Verifier(&out.front(), out.size());
  if (!VerifyDb::VerifyMessageBuffer(verifier))
    return -1;

  // Expect reply is a PutReply message.
  const auto *reply = VerifyDb::GetMessage(&out.front())->message_as_PutReply();
  if (!reply || !reply->ok())
    return -1;
  return 0;
}

#endif
