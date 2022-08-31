#pragma once

#include "user_handle.hpp"

#ifdef USE_VERIFYDB_ACCOUNT_RECOVERY
namespace emailchallenge {
bool EmailChallenge(bool is_check_email, const std::string &email, const userhandle::UserHandle &user, const int y, const std::string &prompt, const std::string &ip, const std::string &filename, int *out_y);
}
#endif
