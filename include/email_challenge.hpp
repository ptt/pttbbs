#pragma once

extern "C" {
#include "bbs.h"
#include "daemons.h"
}

#ifdef USE_VERIFYDB_ACCOUNT_RECOVERY

#include <optional>
#include <string>
#include <vector>

#include "user_handle.hpp"

void LoadUserEmail(const userec_t &u, std::vector<std::string> &all_emails);
bool LoadVerifyDbEmail(const std::optional<UserHandle> &user,
                       std::vector<std::string> &all_emails);
void EmailCodeChallenge(const std::string &email,
                        const std::vector<std::string> &all_emails,
                        int &y);

#endif
