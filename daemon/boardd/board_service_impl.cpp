#include <memory>
#include <string>
#include <vector>
#include <grpc++/grpc++.h>
#include "daemon/boardd/bbs++.hpp"
#include "daemon/boardd/board.grpc.pb.h"
#include "daemon/boardd/board.pb.h"
#include "daemon/boardd/evbuffer.hpp"
#include "daemon/boardd/macros.hpp"
#include "daemon/boardd/strings.hpp"
extern "C" {
#include "cmbbs.h"
#include "daemon/boardd/boardd.h"
#include "var.h"
}

using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;

using pttbbs::api::Board;
using pttbbs::api::BoardRef;
using pttbbs::api::BoardReply;
using pttbbs::api::BoardRequest;
using pttbbs::api::BoardService;
using pttbbs::api::Content;
using pttbbs::api::ContentReply;
using pttbbs::api::ContentRequest;
using pttbbs::api::HotboardReply;
using pttbbs::api::HotboardRequest;
using pttbbs::api::ListReply;
using pttbbs::api::ListRequest;
using pttbbs::api::PartialOptions;
using pttbbs::api::Post;
using pttbbs::api::SearchFilter;
using pttbbs::api::SearchReply;
using pttbbs::api::SearchRequest;

namespace {

class BoardServiceImpl final : public pttbbs::api::BoardService::Service {
 public:
  BoardServiceImpl() {}

  Status Board(ServerContext *context, const BoardRequest *req,
               BoardReply *rep) override;

  Status List(ServerContext *context, const ListRequest *req,
              ListReply *rep) override;

  Status Hotboard(ServerContext *context, const HotboardRequest *req,
                  HotboardReply *rep) override;

  Status Content(ServerContext *context, const ContentRequest *req,
                 ContentReply *rep) override;

  Status Search(ServerContext *context, const SearchRequest *req,
                SearchReply *rep) override;

 private:
  DISABLE_COPY_AND_ASSIGN(BoardServiceImpl);
};

Status ResolveRef(const BoardRef &ref, int *bid, const boardheader_t **bp) {
  switch (ref.ref_case()) {
    case BoardRef::kBid:
      *bid = ref.bid();
      break;
    case BoardRef::kName:
      *bid = boards::Resolve(ref.name()).value_or(0);
      break;
    default:
      return Status(StatusCode::INVALID_ARGUMENT, "Unknown ref");
  }
  auto bptr = boards::Get(*bid);
  if (!bptr || !boards::IsPublic(bptr.value())) {
    return Status(StatusCode::NOT_FOUND, "No such board");
  }
  *bp = bptr.value();
  return Status::OK;
}

Board AsBoard(int bid, const boardheader_t *bp) {
  Board b;
  b.set_bid(bid);
  b.set_name(strings::b2u(bp->brdname));
  b.set_title(strings::b2u(bp->title + 7));
  b.set_bclass(strings::b2u(std::string(bp->title, 4)));
  b.set_raw_moderators(strings::b2u(bp->BM));
  b.set_parent(bp->parent);
  b.set_num_users(bp->nuser);
  b.set_num_posts(
      records::Count<fileheader_t>(paths::bfile(bp->brdname, FN_DIR)));
  b.set_attributes(bp->brdattr);
  for (const auto &child : boards::Children(bid)) {
    b.add_children(child);
  }
  return b;
}

Status BoardServiceImpl::Board(ServerContext *context, const BoardRequest *req,
                               BoardReply *rep) {
  for (const auto &ref : req->ref()) {
    int bid;
    const boardheader_t *bp;
    RETURN_ON_FAIL(ResolveRef(ref, &bid, &bp));
    AsBoard(bid, bp).Swap(rep->add_boards());
  }
  return Status::OK;
}

Post AsPost(size_t index, const fileheader_t &fh) {
  Post p;
  p.set_index(index);
  p.set_filename(fh.filename);
  p.set_raw_date(strings::b2u(fh.date));
  p.set_num_recommends(fh.recommend);
  p.set_filemode(fh.filemode);
  p.set_owner(strings::b2u(fh.owner));
  p.set_title(strings::b2u(fh.title));
  constexpr int64_t one_sec = 1000000000LL;
  p.set_modified_nsec(fh.modified * one_sec);
  return p;
}

Status BoardServiceImpl::List(ServerContext *context, const ListRequest *req,
                              ListReply *rep) {
  int bid;
  const boardheader_t *bp;
  RETURN_ON_FAIL(ResolveRef(req->ref(), &bid, &bp));
  // Posts.
  if (req->include_posts()) {
    std::vector<fileheader_t> fhs;
    size_t offset = records::Get<fileheader_t>(
        paths::bfile(bp->brdname, FN_DIR), req->offset(), req->length(), &fhs);
    for (auto &fh : fhs) {
      DBCS_safe_trim(fh.title);
      AsPost(offset++, fh).Swap(rep->add_posts());
    }
  }
  // Bottoms.
  if (req->include_bottoms()) {
    std::vector<fileheader_t> fhs;
    size_t offset = records::Get<fileheader_t>(
        paths::bfile(bp->brdname, FN_DIR ".bottom"), 0, -1, &fhs);
    for (auto &fh : fhs) {
      DBCS_safe_trim(fh.title);
      AsPost(offset++, fh).Swap(rep->add_bottoms());
    }
  }
  return Status::OK;
}

Status BoardServiceImpl::Hotboard(ServerContext *context,
                                  const HotboardRequest *req,
                                  HotboardReply *rep) {
#if HOTBOARDCACHE
  for (int i = 0; i < SHM->nHOTs; i++) {
    int bid = SHM->HBcache[i] + 1;
    auto bp = boards::Get(bid);
    if (bp && boards::IsPublic(bp.value())) {
      AsBoard(bid, bp.value()).Swap(rep->add_boards());
    }
  }
  return Status::OK;
#else
  return Status(StatusCode::UNIMPLEMENTED, "Hotboard cache is not built in");
#endif
}

int SelectType(PartialOptions::SelectType t) {
  switch (t) {
    case PartialOptions::SELECT_FULL: return SELECT_TYPE_PART;
    case PartialOptions::SELECT_HEAD: return SELECT_TYPE_HEAD;
    case PartialOptions::SELECT_TAIL: return SELECT_TYPE_TAIL;
    case PartialOptions::SELECT_PART: return SELECT_TYPE_PART;
    default: return SELECT_TYPE_PART;
  }
}

Status SelectStatus(int code) {
  switch (code) {
    case SELECT_OK:
      return Status::OK;
    case SELECT_ENOTFOUND:
      return Status(StatusCode::NOT_FOUND, "file not found");
    case SELECT_ECHANGED:
      return Status(StatusCode::NOT_FOUND, "file changed");
    default:
      return Status(StatusCode::INTERNAL, "select_article failed");
  }
}

Status DoSelect(const std::string &fn, const std::string &consistency_token,
                const PartialOptions &popt, Content *content) {
  select_spec_t spec;
  spec.type = SelectType(popt.select_type());
  spec.cachekey_len = consistency_token.size();
  spec.cachekey = spec.cachekey_len ? consistency_token.c_str() : nullptr;
  if (popt.select_type() == PartialOptions::SELECT_FULL) {
    spec.offset = 0;
    spec.maxlen = -1;
  } else {
    spec.offset = popt.offset();
    spec.maxlen = popt.max_length();
  }
  spec.filename = fn.c_str();

  select_result_t result;
  Evbuffer buf;
  RETURN_ON_FAIL(SelectStatus(select_article(buf.Get(), &result, &spec)));
  if (!buf.ConvertUTF8())
    return Status(StatusCode::INTERNAL, "convert utf8 failed");

  content->mutable_content()->assign(
      reinterpret_cast<const char *>(evbuffer_pullup(buf.Get(), -1)),
      evbuffer_get_length(buf.Get()));
  content->set_consistency_token(result.cachekey);
  content->set_offset(result.sel_offset);
  content->set_length(result.sel_size);
  content->set_total_length(result.size);
  return Status::OK;
}

Status BoardServiceImpl::Content(ServerContext *context,
                                 const ContentRequest *req, ContentReply *rep) {
  if (!paths::IsValidFilename(req->filename())) {
    return Status(StatusCode::NOT_FOUND, "invalid filename");
  }
  int bid;
  const boardheader_t *bp;
  RETURN_ON_FAIL(ResolveRef(req->board_ref(), &bid, &bp));
  RETURN_ON_FAIL(DoSelect(paths::bfile(bp->brdname, req->filename()),
                          req->consistency_token(), req->partial_options(),
                          rep->mutable_content()));
  return Status::OK;
}

void SetPredicateKeyword(fileheader_predicate_t *pred,
                         const std::string &utf8_str) {
  strlcpy(pred->keyword, strings::u2b(utf8_str).c_str(), sizeof(pred->keyword));
}

Status SetPredicate(const SearchFilter &filter, fileheader_predicate_t *pred) {
  memset(pred, 0, sizeof(*pred));
  switch (filter.type()) {
    case SearchFilter::TYPE_EXACT_TITLE:
      pred->mode = RS_TITLE;
      SetPredicateKeyword(pred, filter.string_data());
      break;

    case SearchFilter::TYPE_TITLE:
      pred->mode = RS_KEYWORD;
      SetPredicateKeyword(pred, filter.string_data());
      break;

    case SearchFilter::TYPE_AUTHOR:
      pred->mode = RS_AUTHOR;
      SetPredicateKeyword(pred, filter.string_data());
      break;

    case SearchFilter::TYPE_RECOMMEND:
      pred->mode = RS_RECOMMEND;
      pred->recommend = filter.number_data();
      SetPredicateKeyword(pred, std::to_string(pred->recommend));
      break;

    case SearchFilter::TYPE_MONEY:
      pred->mode = RS_MONEY;
      pred->money = filter.number_data();
      SetPredicateKeyword(pred, std::to_string(pred->money) + "M");
      break;

    case SearchFilter::TYPE_MARK:
      pred->mode = RS_MARK;
      break;

    case SearchFilter::TYPE_SOLVED:
      pred->mode = RS_SOLVED;
      break;

    default:
      return Status(StatusCode::UNIMPLEMENTED, "Unimplemented");
  }
  return Status::OK;
}

Status BoardServiceImpl::Search(ServerContext *context,
                                const SearchRequest *req, SearchReply *rep) {
  int bid;
  const boardheader_t *bp;
  RETURN_ON_FAIL(ResolveRef(req->ref(), &bid, &bp));

  if (req->filter().empty()) {
    return Status(StatusCode::INVALID_ARGUMENT, "No search filters specified.");
  }
  std::string base_name = FN_DIR;
  for (const auto &filter : req->filter()) {
    fileheader_predicate_t pred;
    RETURN_ON_FAIL(SetPredicate(filter, &pred));
    auto dst_name = boards::Search(bid, base_name, pred);
    if (!dst_name) {
      return Status(StatusCode::UNKNOWN, "Search failed");
    }
    base_name = dst_name.value();
  }

  std::vector<fileheader_t> fhs;
  size_t offset = records::Get<fileheader_t>(
      paths::bfile(bp->brdname, base_name), req->offset(), req->length(), &fhs);
  for (auto &fh : fhs) {
    DBCS_safe_trim(fh.title);
    AsPost(offset++, fh).Swap(rep->add_posts());
  }
  rep->set_total_posts(
      records::Count<fileheader_t>(paths::bfile(bp->brdname, base_name)));
  return Status::OK;
}

}  // namespace

std::unique_ptr<BoardService::Service> CreateBoardService() {
  return std::unique_ptr<BoardService::Service>(new BoardServiceImpl());
}

void start_grpc_server(const char *host, unsigned short port) {
  auto service = CreateBoardService();
  auto address = std::string(host) + ":" + std::to_string(port);

  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(service.get());

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  std::cerr << "Serving at " << address << std::endl;
  server->Wait();
}
