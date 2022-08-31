#pragma once

namespace userhandle {

struct UserHandle {
  std::string userid;
  int64_t generation;
  std::string email;
};

bool InitUserHandle(const userec_t *user, UserHandle &out_user_handle);

} // namespace
