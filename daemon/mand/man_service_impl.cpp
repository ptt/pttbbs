#include <memory>
#include <string>
#include <vector>
#include <grpc++/grpc++.h>
#include "file_header.hpp"
#include "man.grpc.pb.h"
#include "man.pb.h"
#include "man_directory.hpp"
#include "man_path.hpp"
#include "man_service_impl.hpp"
#include "util.hpp"
#include "daemon/boardd/evbuffer.hpp"
extern "C" {
#include "cmbbs.h"
#include "daemon/boardd/boardd.h"
}

using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;

using pttbbs::man::ManService;
using pttbbs::man::ListRequest;
using pttbbs::man::ListReply;
using pttbbs::man::ArticleRequest;
using pttbbs::man::ArticleReply;

namespace {

class ManServiceImpl final : public pttbbs::man::ManService::Service {
 public:
  ManServiceImpl(std::shared_ptr<ManDirectory> &man_directory)
      : man_directory_(man_directory) {}

  grpc::Status List(grpc::ServerContext *context,
                    const pttbbs::man::ListRequest *req,
                    pttbbs::man::ListReply *rep);

  grpc::Status Article(grpc::ServerContext *context,
                       const pttbbs::man::ArticleRequest *req,
                       pttbbs::man::ArticleReply *rep);

 private:
  DISABLE_COPY_AND_ASSIGN(ManServiceImpl);

  std::shared_ptr<ManDirectory> man_directory_;

  Status CheckPathPermission(const ManPath &path);
};

Status ManServiceImpl::CheckPathPermission(const ManPath &path) {
  if (!path.IsValid())
    return Status(StatusCode::INVALID_ARGUMENT, "Invalid path");
  if (!man_directory_->HasPermission(path))
    return Status(StatusCode::PERMISSION_DENIED, "Permission denied");
  return Status::OK;
}

Status ManServiceImpl::List(ServerContext *context, const ListRequest *req,
                            ListReply *rep) {
  ManPath path(req->board_name(), req->path());

  RETURN_ON_FAIL(CheckPathPermission(path));

  std::vector<FileHeader> entries;
  if (!man_directory_->ListDirectory(path, &entries))
    return Status(StatusCode::UNAVAILABLE, "Backend unavailable");

  for (const auto &e : entries) {
    if (!man_directory_->HasPermission(e))
      continue;
    auto r = rep->add_entries();
    ManPath p(path, e.FileName());
    r->set_board_name(p.BoardName());
    r->set_path(p.PathRelativeToBoard());
    r->set_title(e.Title());
    r->set_is_dir(IsDirectory(p.FullPath()));
  }
  rep->set_is_success(true);
  return Status::OK;
}

int ArticleSelectType(ArticleRequest::SelectType t) {
  switch (t) {
    case ArticleRequest::SELECT_FULL: return SELECT_TYPE_PART;
    case ArticleRequest::SELECT_HEAD: return SELECT_TYPE_HEAD;
    case ArticleRequest::SELECT_TAIL: return SELECT_TYPE_TAIL;
    default: return SELECT_TYPE_PART;
  }
}

Status ManServiceImpl::Article(ServerContext *context,
                               const ArticleRequest *req, ArticleReply *rep) {
  ManPath path(req->board_name(), req->path());

  RETURN_ON_FAIL(CheckPathPermission(path));

  select_spec_t spec;
  spec.type = ArticleSelectType(req->select_type());
  spec.cachekey_len = req->cache_key().size();
  spec.cachekey = spec.cachekey_len ? req->cache_key().c_str() : nullptr;
  spec.offset = req->offset();
  spec.maxlen = req->max_length();
  auto full_path = path.FullPath();
  spec.filename = full_path.c_str();

  select_result_t result;
  Evbuffer buf;
  if (select_article(buf.Get(), &result, &spec) < 0)
    return Status(StatusCode::INTERNAL, "select_article failed");
  if (!buf.ConvertUTF8())
    return Status(StatusCode::INTERNAL, "convert utf8 failed");

  rep->mutable_content()->assign(
      reinterpret_cast<const char *>(evbuffer_pullup(buf.Get(), -1)),
      evbuffer_get_length(buf.Get()));
  rep->set_cache_key(result.cachekey);
  rep->set_file_size(result.size);
  rep->set_selected_offset(result.sel_offset);
  rep->set_selected_size(result.sel_size);
  return Status::OK;
}

}  // namespace

std::unique_ptr<ManService::Service> CreateManServiceImpl(
    std::shared_ptr<ManDirectory> &man_directory) {
  return std::unique_ptr<ManService::Service>(
      new ManServiceImpl(man_directory));
}
