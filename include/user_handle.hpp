#pragma once

#include <string>

namespace user_handle {

struct UserHandle {
  std::string userid;
  int64_t generation;
};

bool InitUserHandle(const userec_t *u, UserHandle &user);

} // namespace user_handle
