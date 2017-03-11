#include <memory>
#include <string>
#include <vector>
#include <grpc++/grpc++.h>
#include "daemon/boardd/bbs++.hpp"
#include "daemon/boardd/board.grpc.pb.h"
#include "daemon/boardd/board.pb.h"
#include "daemon/boardd/strings.hpp"
#include "daemon/mand/util.hpp"
extern "C" {
#include "cmbbs.h"
#include "daemon/boardd/boardd.h"
#include "proto.h"
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
using pttbbs::api::HotboardReply;
using pttbbs::api::HotboardRequest;
using pttbbs::api::ListReply;
using pttbbs::api::ListRequest;
using pttbbs::api::Post;

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

 private:
  DISABLE_COPY_AND_ASSIGN(BoardServiceImpl);
};

Status ResolveRef(const BoardRef &ref, int *bid, const boardheader_t **bp) {
  switch (ref.ref_case()) {
    case BoardRef::kBid:
      *bid = ref.bid();
      break;
    case BoardRef::kName:
      *bid = boards::Resolve(ref.name());
      break;
    default:
      return Status(StatusCode::INVALID_ARGUMENT, "Unknown ref");
  }
  auto *bptr = boards::Get(*bid);
  if (!boards::IsPublic(bptr)) {
    return Status(StatusCode::NOT_FOUND, "No such board");
  }
  *bp = bptr;
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
  for (const auto &child : boards::Children(bid, bp)) {
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
#ifdef HOTBOARDCACHE
  for (int i = 0; i < SHM->nHOTs; i++) {
    int bid = SHM->HBcache[i] + 1;
    auto bp = getbcache(bid);
    if (boards::IsPublic(bp)) {
      AsBoard(bid, bp).Swap(rep->add_boards());
    }
  }
  return Status::OK;
#else
  return Status(StatusCode::UNIMPLEMENTED, "Hotboard cache is not built in");
#endif
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
