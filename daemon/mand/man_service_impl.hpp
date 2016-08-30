#ifndef _MAND_MAN_SERVICE_IMPL_HPP_
#define _MAND_MAN_SERVICE_IMPL_HPP_
#include <memory>
#include "man_directory.hpp"
#include "man.grpc.pb.h"

std::unique_ptr<pttbbs::man::ManService::Service> CreateManServiceImpl(
    std::shared_ptr<ManDirectory> &man_directory);

#endif
