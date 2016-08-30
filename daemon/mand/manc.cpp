#include <iostream>
#include <grpc++/grpc++.h>
#include <gflags/gflags.h>
#include "man.grpc.pb.h"

DEFINE_string(connect, "127.0.0.1:5961", "Address of mand to connect to");

using grpc::ClientContext;
using grpc::Status;

using pttbbs::man::ManService;
using pttbbs::man::ListRequest;
using pttbbs::man::ListReply;
using pttbbs::man::ArticleRequest;
using pttbbs::man::ArticleReply;

int list_action(std::string full_path) {
  auto pos = full_path.find('/');
  std::string board_name = full_path.substr(0, pos);
  std::string path;
  if (pos != std::string::npos)
    path = full_path.substr(pos + 1);

  std::shared_ptr<grpc::Channel> channel =
      grpc::CreateChannel(FLAGS_connect, grpc::InsecureChannelCredentials());
  std::unique_ptr<ManService::Stub> man = ManService::NewStub(channel);

  ClientContext context;
  ListRequest req;
  req.set_board_name(board_name);
  req.set_path(path);
  std::cerr << "Sending request:" << req.DebugString() << std::endl;

  ListReply rep;
  Status status = man->List(&context, req, &rep);
  if (!status.ok()) {
    std::cerr << "Error: (" << status.error_code() << ") "
              << status.error_message() << std::endl;
    return 1;
  }
  std::cout << rep.DebugString() << std::endl;
  for (const auto &e : rep.entries()) {
    std::cerr << (e.is_dir() ? "d" : ".") << " " << e.path() << " " << e.title()
              << std::endl;
  }
  return 0;
}

int article_action(std::string full_path) {
  auto pos = full_path.find('/');
  std::string board_name = full_path.substr(0, pos);
  std::string path;
  if (pos != std::string::npos)
    path = full_path.substr(pos + 1);

  std::shared_ptr<grpc::Channel> channel =
      grpc::CreateChannel("127.0.0.1:5961", grpc::InsecureChannelCredentials());
  std::unique_ptr<ManService::Stub> man = ManService::NewStub(channel);

  ClientContext context;
  ArticleRequest req;
  req.set_board_name(board_name);
  req.set_path(path);
  req.set_select_type(ArticleRequest::SELECT_FULL);
  req.set_max_length(-1);
  std::cerr << "Sending request:" << req.DebugString() << std::endl;

  ArticleReply rep;
  Status status = man->Article(&context, req, &rep);
  if (!status.ok()) {
    std::cerr << "Error: (" << status.error_code() << ") "
              << status.error_message() << std::endl;
    return 1;
  }
  std::cout << rep.DebugString() << std::endl;
  return 0;
}

void SetupUsage(const std::string &prog) {
  std::string usage = "Usage: ";
  usage += prog + " [options] <list|article> path";
  gflags::SetUsageMessage(usage);
}

int main(int argc, char *argv[]) {
  SetupUsage(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (argc != 3) {
    gflags::ShowUsageWithFlags(argv[0]);
    return 1;
  }

  std::string action = argv[1];
  if (action == "list") {
    return list_action(argv[2]);
  } else if (action == "article") {
    return article_action(argv[2]);
  } else {
    std::cerr << "Unknown action: " << action << std::endl;
    return 1;
  }
}
