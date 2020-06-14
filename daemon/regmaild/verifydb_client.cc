#include <vector>

#include "flatbuffers/flatbuffers.h"
#include "verifydb.fbs.h"

extern "C" {
#include "bbs.h"
#include "daemons.h"

bool verifydb_client_getuser(const char *userid);
bool verifydb_client_getverify(int32_t vmethod, const char *vkey);
bool verifydb_client_set(const char *userid, int64_t generation,
                         int32_t vmethod, const char *vkey, int64_t timestamp);
}

namespace {

using Bytes = std::vector<uint8_t>;

bool verifydb_transact(const void *msg, size_t len, Bytes *out) {
  int fd;

  if ((fd = toconnect(REGMAILD_ADDR)) < 0) {
    perror("toconnect");
    return false;
  }

  // Send request.
  verifydb_message_t rep = {};
  rep.header.cb = sizeof(rep.header) + len;
  rep.header.operation = VERIFYDB_MESSAGE;
  if (towrite(fd, &rep.header, sizeof(rep.header)) != sizeof(rep.header) ||
      towrite(fd, msg, len) != len) {
    perror("towrite");
    return false;
  }

  // Read reply.
  if (toread(fd, &rep.header, sizeof(rep.header)) != sizeof(rep.header)) {
    perror("toread");
    return false;
  }
  out->resize(rep.header.cb - sizeof(rep.header));
  if (toread(fd, &out->front(), out->size()) != out->size()) {
    perror("toread");
    return false;
  }
  return true;
}

bool print(const VerifyDb::GetReply *rep) {
  printf("ok: %s\n", rep->ok() ? "true" : "false");

  const auto *entries = rep->entry();
  if (entries != nullptr && entries->size() > 0) {
    printf("%d entries\n", entries->size());

    for (const auto &ent : *entries) {
      const char *userid = ent->userid()->c_str();
      if (userid == nullptr)
        userid = "";

      const char *vkey = ent->vkey()->c_str();
      if (vkey == nullptr)
        vkey = "";

      printf("entry: userid [%s] generation [%ld] vmethod [%d] vkey [%s] "
             "timestamp [%ld]\n",
             userid, ent->generation(), ent->vmethod(), vkey, ent->timestamp());
    }
  } else {
    printf("0 entries\n");
  }
  return true;
}

bool print(const VerifyDb::PutReply *rep) {
  printf("put: %s\n", rep->ok() ? "ok" : "failed");
  if (rep->message())
    printf("message: %s\n", rep->message()->c_str());
  return true;
}

bool print_message(const Bytes& out) {
  auto verifier = flatbuffers::Verifier(&out.front(), out.size());
  if (!VerifyDb::VerifyMessageBuffer(verifier)) {
    fprintf(stderr, "malformed response\n");
    return false;
  }

  const auto* msg = VerifyDb::GetMessage(out.data());
  switch (msg->message_type()) {
  case VerifyDb::AnyMessage_GetReply:
    return print(msg->message_as_GetReply());

  case VerifyDb::AnyMessage_PutReply:
    return print(msg->message_as_PutReply());

  default:
    fprintf(stderr, "unknown message type\n");
    return false;
  }
}

}  // namespace

bool verifydb_client_getuser(const char *userid) {
  flatbuffers::FlatBufferBuilder fbb;
  auto entry = VerifyDb::CreateEntryDirect(fbb, userid);
  auto match_fields = fbb.CreateVector<int8_t>({VerifyDb::Field_UserId});
  auto req = VerifyDb::CreateMessage(
      fbb, VerifyDb::AnyMessage_GetRequest,
      VerifyDb::CreateGetRequest(fbb, entry, match_fields).Union());
  fbb.Finish(req);

  Bytes out;
  return verifydb_transact(fbb.GetBufferPointer(), fbb.GetSize(), &out) &&
         print_message(out);
}

bool verifydb_client_getverify(int32_t vmethod, const char *vkey) {
  flatbuffers::FlatBufferBuilder fbb;
  auto entry = VerifyDb::CreateEntryDirect(fbb, nullptr, 0, vmethod, vkey);
  auto match_fields = fbb.CreateVector<int8_t>({VerifyDb::Field_Verify});
  auto req = VerifyDb::CreateMessage(
      fbb, VerifyDb::AnyMessage_GetRequest,
      VerifyDb::CreateGetRequest(fbb, entry, match_fields).Union());
  fbb.Finish(req);

  Bytes out;
  return verifydb_transact(fbb.GetBufferPointer(), fbb.GetSize(), &out) &&
         print_message(out);
}

bool verifydb_client_set(const char *userid, int64_t generation,
                         int32_t vmethod, const char *vkey, int64_t timestamp) {
  flatbuffers::FlatBufferBuilder fbb;
  auto entry = VerifyDb::CreateEntryDirect(fbb, userid, generation, vmethod,
                                           vkey, timestamp);
  auto req = VerifyDb::CreateMessage(
      fbb, VerifyDb::AnyMessage_PutRequest,
      VerifyDb::CreatePutRequest(fbb, entry, VerifyDb::Conflict_Replace)
          .Union());
  fbb.Finish(req);

  Bytes out;
  return verifydb_transact(fbb.GetBufferPointer(), fbb.GetSize(), &out) &&
         print_message(out);
}
