#pragma once

extern "C" {
#include "bbs.h"
#include "daemons.h"
}

#include <optional>
#include <string>
#include <vector>

#include "user_handle.hpp"

namespace email_challenge {

void LoadUserEmail(const userec_t *u, std::vector<std::string> &all_emails);
bool LoadVerifyDbEmail(const std::optional<user_handle::UserHandle> &user,
                       std::vector<std::string> &all_emails);
void EmailCodeChallenge(const bool check_email,
                        const std::string &email,
                        const std::vector<std::string> &all_emails,
                        const std::string &prompt,
                        const std::string &ip,
                        const std::string &filename,
                        int &y);

} // namespace email_challenge
