#pragma once

#include <string>

#ifdef USE_VERIFYDB_ACCOUNT_RECOVERY

struct UserHandle {
  std::string userid;
  int64_t generation;
};

#endif
