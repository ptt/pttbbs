#include <memory>
#include <grpc++/grpc++.h>
#include <gflags/gflags.h>
#include "man_directory_impl.hpp"
#include "man_service_impl.hpp"
extern "C" {
#include "cmbbs.h"
}

DEFINE_string(address, "0.0.0.0:5961", "Address to listen on");

void setup() {
  setuid(BBSUID);
  setgid(BBSGID);
  chdir(BBSHOME);

  attach_SHM();
}

int main(int argc, char *argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (argc > 1) {
    std::cerr << "Unrecognized command line argument: " << argv[1] << std::endl;
    return 1;
  }

  setup();

  auto man_directory = std::shared_ptr<ManDirectory>(CreateManDirectoryImpl());
  auto service = CreateManServiceImpl(man_directory);
  auto &address = FLAGS_address;

  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(service.get());

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  std::cerr << "Serving at " << address << std::endl;
  server->Wait();
  return 0;
}
