#include <functional>
#include <vector>
#include <set>

#include "flatbuffers/flatbuffers.h"
#include "verifydb_util.h"
#include "verifydb.fbs.h"

extern "C" {
#include <sqlite3.h>

#include "bbs.h"
#include "daemons.h"

extern sqlite3 *g_Db;

void verifydb_message_handler(const verifydb_message_t *vm, int fd);
}

namespace {

class SqliteStmt {
public:
  SqliteStmt(sqlite3 *db) : db_(db) {}

  ~SqliteStmt() {
    if (!ok())
      fprintf(stderr, "sqlite3: %d %s\n", ret_, errmsg());

    if (stmt_ != nullptr) {
      ret_ = sqlite3_finalize(stmt_);
      if (ret_ != SQLITE_OK)
        fprintf(stderr, "sqlite3_finalize error: %d %s\n", ret_, errmsg());
    }
  }

  SqliteStmt(const SqliteStmt &) = delete;
  SqliteStmt &operator=(SqliteStmt &) = delete;

  bool prepare(const char *sql) {
    ret_ = sqlite3_prepare(db_, sql, -1, &stmt_, nullptr);
    return ok();
  }

  bool bind_text(int pos, const char *text, int len,
                 void (*dtor)(void *) = nullptr) {
    ret_ = sqlite3_bind_text(stmt_, pos, text, len, dtor);
    return ok();
  }
  bool bind_int64(int pos, sqlite3_int64 num) {
    ret_ = sqlite3_bind_int64(stmt_, pos, num);
    return ok();
  }

  const unsigned char* column_text(int icol) {
    return sqlite3_column_text(stmt_, icol);
  }
  sqlite3_int64 column_int64(int icol) {
    return sqlite3_column_int64(stmt_, icol);
  }

  int step() { return sqlite3_step(stmt_); }

  bool ok() const { return ret_ == SQLITE_OK; }

  const char *errmsg() const { return sqlite3_errmsg(db_); }

private:
  sqlite3 *db_ = nullptr;         // Not owned
  sqlite3_stmt *stmt_ = nullptr;  // Owned
  int ret_ = SQLITE_OK;
};

void write_message(int fd, const flatbuffers::DetachedBuffer &buf) {
  size_t len = buf.size();
  // Write header.
  verifydb_message_t rep = {};
  rep.header.cb = sizeof(rep.header) + len;
  rep.header.operation = VERIFYDB_MESSAGE;
  if (towrite(fd, &rep.header, sizeof(rep.header)) != sizeof(rep.header))
    perror("towrite");
  // Write message.
  if (towrite(fd, buf.data(), len) != len)
    perror("towrite");
}

flatbuffers::DetachedBuffer make_get_reply_error(const char *message) {
  flatbuffers::FlatBufferBuilder fbb;
  auto reply = VerifyDb::CreateGetReplyDirect(fbb, false, nullptr, message);
  fbb.Finish(VerifyDb::CreateMessage(fbb, VerifyDb::AnyMessage_GetReply,
                                     reply.Union()));
  return fbb.Release();
}

flatbuffers::DetachedBuffer
verifydb_message_get_request_handler(const VerifyDb::GetRequest *req) {
  auto ent = req->entry();
  if (!ent)
    return make_get_reply_error("entry must be set");

  const char *req_userid = CStrOrEmpty(ent->userid());
  const char *req_vkey = CStrOrEmpty(ent->vkey());

  fprintf(stderr, "get: userid [%s] generation [%ld] vmethod [%d] vkey [%s]\n",
          req_userid, ent->generation(), ent->vmethod(), req_vkey);

  std::set<VerifyDb::Field> match_fields;
  if (const auto mfs = req->match_field(); mfs)
    for (const auto mf : *mfs)
      match_fields.insert(VerifyDb::Field(mf));

  std::string sql = "SELECT userid, generation, vmethod, vkey, timestamp FROM "
                    "verifydb WHERE TRUE";
  if (match_fields.count(VerifyDb::Field_UserId))
    sql += " AND userid = lower(?)";
  if (match_fields.count(VerifyDb::Field_Generation))
    sql += " AND generation = ?";
  if (match_fields.count(VerifyDb::Field_Verify))
    sql += " AND vmethod = ? AND vkey = ?";
  sql += " ORDER BY userid, generation, vmethod, timestamp";

  SqliteStmt stmt(g_Db);
  bool ok = stmt.prepare(sql.c_str());
  int idx = 0;
  if (match_fields.count(VerifyDb::Field_UserId))
    ok = ok && stmt.bind_text(++idx, req_userid, -1, SQLITE_STATIC);
  if (match_fields.count(VerifyDb::Field_Generation))
    ok = ok && stmt.bind_int64(++idx, ent->generation());
  if (match_fields.count(VerifyDb::Field_Verify))
    ok = ok && stmt.bind_int64(++idx, ent->vmethod()) &&
         stmt.bind_text(++idx, req_vkey, -1, SQLITE_STATIC);
  if (!ok)
    return make_get_reply_error(stmt.errmsg());

  flatbuffers::FlatBufferBuilder fbb;
  std::vector<flatbuffers::Offset<VerifyDb::Entry>> entries;

  while (stmt.step() == SQLITE_ROW) {
    const char *userid = StrOrEmpty((const char *)stmt.column_text(0));
    const int64_t generation = stmt.column_int64(1);
    const int32_t vmethod = stmt.column_int64(2);
    const char *vkey = StrOrEmpty((const char *)stmt.column_text(3));
    const int64_t timestamp = stmt.column_int64(4);

    entries.push_back(VerifyDb::CreateEntryDirect(fbb, userid, generation,
                                                  vmethod, vkey, timestamp));

    fprintf(stderr,
            "get-entry: userid [%s] generation [%ld] vmethod [%d] vkey [%s] "
            "timestamp [%ld]\n",
            userid, generation, vmethod, vkey, timestamp);
  }

  auto reply =
      VerifyDb::CreateGetReply(fbb, true, fbb.CreateVector(entries), 0);
  fbb.Finish(VerifyDb::CreateMessage(fbb, VerifyDb::AnyMessage_GetReply,
                                     reply.Union()));
  return fbb.Release();
}

flatbuffers::DetachedBuffer make_put_reply_error(const char *message) {
  flatbuffers::FlatBufferBuilder fbb;
  auto reply = VerifyDb::CreatePutReplyDirect(fbb, false, message);
  fbb.Finish(VerifyDb::CreateMessage(fbb, VerifyDb::AnyMessage_PutReply,
                                     reply.Union()));
  return fbb.Release();
}

flatbuffers::DetachedBuffer
verifydb_message_put_request_handler(const VerifyDb::PutRequest *req) {
  const VerifyDb::Entry *ent = req->entry();
  if (!ent)
    return make_put_reply_error("entry must be set");
  const auto *userid = CStrOrEmpty(ent->userid());
  const auto *vkey = CStrOrEmpty(ent->vkey());

  bool ok;
  SqliteStmt stmt(g_Db);
  if (!*vkey) {
    ok = stmt.prepare(
        "DELETE FROM verifydb "
        "WHERE userid = lower(?) AND generation = ? AND vmethod = ?;");
    ok = ok && stmt.bind_text(1, userid, -1, SQLITE_STATIC);
    ok = ok && stmt.bind_int64(2, ent->generation());
    ok = ok && stmt.bind_int64(3, ent->vmethod());

    fprintf(stderr, "put: userid [%s] generation [%ld] vmethod [%d] (delete)\n",
            userid, ent->generation(), ent->vmethod());
  } else {
    std::string sql = "INSERT OR";
    switch (req->conflict()) {
      case VerifyDb::Conflict_Ignore:
        sql += " IGNORE";
        break;
      case VerifyDb::Conflict_Replace:
        sql += " REPLACE";
        break;
      default:
        return make_put_reply_error("unknown conflict resolution scheme");
    }
    sql += " INTO verifydb "
           "(userid, generation, vmethod, vkey, timestamp) VALUES "
           "(lower(?),?,?,?,?);";
    ok = stmt.prepare(sql.c_str());
    ok = ok && stmt.bind_text(1, userid, -1, SQLITE_STATIC);
    ok = ok && stmt.bind_int64(2, ent->generation());
    ok = ok && stmt.bind_int64(3, ent->vmethod());
    ok = ok && stmt.bind_text(4, vkey, -1, SQLITE_STATIC);

    int64_t timestamp = ent->timestamp();
    if (!timestamp)
      timestamp = time(nullptr);
    ok = ok && stmt.bind_int64(5, timestamp);

    fprintf(stderr,
            "put: userid [%s] generation [%ld] vmethod [%d] vkey [%s] "
            "timestamp [%ld]\n",
            userid, ent->generation(), ent->vmethod(), vkey, timestamp);
  }
  ok = ok && stmt.step() == SQLITE_DONE;
  if (!ok)
    return make_put_reply_error(stmt.errmsg());

  flatbuffers::FlatBufferBuilder fbb;
  auto rep =
      VerifyDb::CreatePutReplyDirect(fbb, ok, ok ? nullptr : stmt.errmsg());
  fbb.Finish(
      VerifyDb::CreateMessage(fbb, VerifyDb::AnyMessage_PutReply, rep.Union()));
  return fbb.Release();
}

}  // namespace

void verifydb_message_handler(const verifydb_message_t *vm, int fd) {
  size_t len = vm->header.cb - sizeof(vm->header);
  auto verifier = flatbuffers::Verifier(
      reinterpret_cast<const uint8_t *>(vm->message), len);
  if (!VerifyDb::VerifyMessageBuffer(verifier))
    return;

  auto *message = VerifyDb::GetMessage(vm->message);

  switch (message->message_type()) {
  case VerifyDb::AnyMessage_GetRequest:
    write_message(fd, verifydb_message_get_request_handler(
                          message->message_as_GetRequest()));
    break;

  case VerifyDb::AnyMessage_PutRequest:
    write_message(fd, verifydb_message_put_request_handler(
                          message->message_as_PutRequest()));
    break;

  default:
    // Bad request. Ignore.
    break;
  }
}
