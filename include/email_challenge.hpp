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

void LoadUserEmail(const userec_t &u, std::vector<std::string> &all_emails_);
bool LoadVerifyDbEmail(const std::optional<UserHandle> &user_,
                       std::vector<std::string> &all_emails_);
void EmailCodeChallenge(const std::string &email_,
                        const std::vector<std::string> &all_emails_,
                        int &y_);

#endif
