#include <functional>
#include <iostream>
#include <gflags/gflags.h>
#include <google/protobuf/text_format.h>
#include <grpc++/grpc++.h>
#include "board.grpc.pb.h"

DEFINE_string(connect, "127.0.0.1:5962", "Address of boardd to connect to");
DEFINE_string(method, "", "Method");
DEFINE_string(request, "", "Text format of request");

using google::protobuf::TextFormat;

using grpc::ClientContext;
using grpc::Status;

using pttbbs::api::BoardReply;
using pttbbs::api::BoardRequest;
using pttbbs::api::BoardService;
using pttbbs::api::ContentReply;
using pttbbs::api::ContentRequest;
using pttbbs::api::HotboardReply;
using pttbbs::api::HotboardRequest;
using pttbbs::api::ListReply;
using pttbbs::api::ListRequest;
using pttbbs::api::SearchReply;
using pttbbs::api::SearchRequest;

template <class Service, class Request, class Reply>
struct MakeRPC {
  template <class Method>
  static int Do(Method method) {
    std::shared_ptr<grpc::Channel> channel =
        grpc::CreateChannel(FLAGS_connect, grpc::InsecureChannelCredentials());
    std::unique_ptr<typename Service::Stub> stub = Service::NewStub(channel);

    ClientContext context;
    Request req;
    Reply rep;
    if (!TextFormat::ParseFromString(FLAGS_request, &req)) {
      std::cerr << "Error: Could not parse request." << std::endl;
    }

    Status s = (*stub.*method)(&context, req, &rep);
    if (!s.ok()) {
      std::cerr << "Error: (" << s.error_code() << ") " << s.error_message()
                << std::endl;
      return 1;
    }
    std::cout << rep.Utf8DebugString() << std::endl;
    return 0;
  }

  template <class Method>
  static std::function<int()> BindMethod(Method method) {
    return std::bind(&Do<Method>, method);
  }
};

int main(int argc, char *argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  std::pair<std::string, std::function<int()>> methods[] = {
      {"Board", MakeRPC<BoardService, BoardRequest, BoardReply>::BindMethod(
                    &BoardService::Stub::Board)},
      {"List", MakeRPC<BoardService, ListRequest, ListReply>::BindMethod(
                   &BoardService::Stub::List)},
      {"Hotboard",
       MakeRPC<BoardService, HotboardRequest, HotboardReply>::BindMethod(
           &BoardService::Stub::Hotboard)},
      {"Content",
       MakeRPC<BoardService, ContentRequest, ContentReply>::BindMethod(
           &BoardService::Stub::Content)},
      {"Search", MakeRPC<BoardService, SearchRequest, SearchReply>::BindMethod(
                     &BoardService::Stub::Search)},
  };
  for (const auto &m : methods) {
    if (m.first == FLAGS_method) {
      return m.second();
    }
  }
  std::cerr << "Unknown method: " << FLAGS_method << std::endl;
  return 1;
}
