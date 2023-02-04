extern "C" {
#include "bbs.h"
#include "daemons.h"
}

#include <optional>
#include <string>
#include <vector>

#ifdef USE_VERIFYDB_ACCOUNT_RECOVERY
#include "verifydb.h"
#include "verifydb.fbs.h"
#endif // USE_VERIFYDB_ACCOUNT_RECOVERY

#include "user_handle.hpp"
#include "email_challenge.hpp"

namespace email_challenge {

constexpr int kMaxErrCnt = 3;
constexpr size_t kCodeLen = 30;

// static
std::optional<std::string>
NormalizeEmail(const std::string &email) {
  auto out = email;
  for (auto& c : out)
    c = std::tolower(c);
  auto idx = out.find('@');
  if (idx == std::string::npos)
    return std::nullopt;
  if (idx != out.rfind('@'))
    return std::nullopt;
  return out;
}

// LoadUserEmail
//
// Params:
//   u:          userec
//   all_emails: all valid emails.
void LoadUserEmail(const userec_t *u, std::vector<std::string> &all_emails) {
#ifdef USEREC_EMAIL_IS_CONTACT
  auto email = NormalizeEmail(u->email);
  if (email)
    all_emails.push_back(email.value());
#endif // USEREC_EMAIL_IS_CONTACT
}

// LoadVerifyDbEmail
//
// Params:
//   user:       UserHandle
//   all_emails: all valid emails.
//
// Return:
//   bool: true: ok false: err
bool LoadVerifyDbEmail(const std::optional<user_handle::UserHandle> &user,
                       std::vector<std::string> &all_emails) {
#ifdef USE_VERIFYDB_ACCOUNT_RECOVERY
  Bytes buf;
  const VerifyDb::GetReply *reply;
  if (!verifydb_getuser(user->userid.c_str(), user->generation, &buf, &reply))
    return false;

  if (reply->entry()) {
    for (const auto *ent : *reply->entry()) {
      if (ent->vmethod() == VMETHOD_EMAIL && ent->vkey() != nullptr) {
        auto email = NormalizeEmail(ent->vkey()->str());
        if (email)
          all_emails.push_back(email.value());
      }
    }
  }
#endif // USE_VERIFYDB_ACCOUNT_RECOVERY
  return true;
}

// static
bool SendChallengeCode(const std::string &email,
                      const std::string &code,
                      const std::string &prompt,
                      const std::string &ip,
                      const std::string &filename) {
  std::string subject;
  subject.append(prompt);
  subject.append(" [ ");
  subject.append(code);
  subject.append(" ] @ IP ");
  subject.append(ip);
  bsmtp(filename.c_str(), subject.c_str(), email.c_str(), "non-exist");
  return true;
}

// static
std::string GenCode(size_t len) {
  std::string s(len, '\0');
  random_text_code(&s[0], s.size());
  return s;
}

// static
void UserErrorExit() {
  vmsg("錯誤次數過多，請重新操作。");
  exit(0);
}

// EmailCodeChallenge
//
// Params:
//   check_email: whether to check email (AccountRecovery)
//                or not (reset password / change contact email).
//                It's possible that email is "" even if we do want to check input.
//                We can't put email as NULL to indicate that we want to
//                skip checking the email.
//   email:       user-input email
//   all_emails:  all valid emails
//   prompt:      prompt for the email title (prompt [ code ] @ ip)
//   ip:          ip for the email title
//   filename:    filename for the email template
//
//   y:           starting y on screen, updated along with the function.
void EmailCodeChallenge(const bool check_email,
                        const std::string &email,
                        const std::vector<std::string> &all_emails,
                        const std::string &prompt,
                        const std::string &ip,
                        const std::string &filename,
                        int &y) {
  // Generate code.
  auto code = GenCode(kCodeLen);

  y++;
  mvprints(y, 0, "系統處理中...");
  doupdate();

  // Check and send recovery code. Don't exit if email doesn't match, so user
  // can't guess email.
  bool email_matches = false;
  if (check_email) {
    for (const auto &each_email : all_emails) {
      if (each_email == email)
        email_matches = true;
    }

    if (email_matches) {
      // We only want to send email if the user input the matching address.
      // Silently fail if email is not matching, so that user cannot guess other
      // user's email address.
      SendChallengeCode(email, code, prompt, ip, filename);
    }
  } else {
    email_matches = true;
    // We send to all the valid user emails.
    for (const auto &each_email : all_emails) {
      SendChallengeCode(each_email, code, prompt, ip, filename);
    }
  }

  // Add a random 5-10s delay to prevent timing oracle.
  usleep(5000000 + random() % 5000000);
  if (check_email) {
    mvprints(y++, 0, "若您輸入的資料正確，系統已將認證碼寄送至您的信箱。");
  } else {
    mvprints(y++, 0, "系統已將認證碼寄送至您的信箱。");
  }

  // Input code.
  char incode[kCodeLen + 1] = {};
  int errcnt = 0;
  while (1) {
    getdata_buf(y, 0, "請收信後輸入認證碼：", incode, sizeof(incode), DOECHO);
    if (email_matches && code == std::string(incode))
      break;
    if (++errcnt >= kMaxErrCnt)
      UserErrorExit();
    mvprints(y + 1, 0, "認證碼錯誤！認證碼共有 %d 字元。", (int)kCodeLen);
    incode[0] = '\0';
  }
  y++;
  move(y, 0);
  clrtoeol(); // There might be error message at this line.

  // Some paranoid checkings, we are about to let user reset their password.
  if (code.size() != kCodeLen || code != std::string(incode)) {
    assert(false);
    exit(1);
  }
}

} // namespace email_challenge

// email_code_challenge
//
// Params:
//   email:    user-input email. NULL if no user-input email.
//   u:        userec
//   y:        starting y on screen.
//   prompt:   prompt for the email title (prompt [ code ] @ ip)
//   ip:       ip for the email title
//   filename: filename for the email template
//
//   out_y:    updated y along with the function.
//             NULL if we do not need the updated y.
//
// Return:
//   int: 0: ok -1: err
int email_code_challenge(const char *email,
                    const userec_t *u,
                    const int y,
                    const char *prompt,
                    const char *ip,
                    const char *filename,

                    int *out_y) {
  std::string email_str = {};
  bool check_email = false;
  if (email != NULL) {
    email_str = std::string(email);
    check_email = true;
  }

  user_handle::UserHandle user = {};
  if (!user_handle::InitUserHandle(u, user)) {
    return -1;
  }

  std::vector<std::string> all_emails = {};
  std::optional<user_handle::UserHandle> user_opt = user;

  email_challenge::LoadUserEmail(u, all_emails);

  if (!email_challenge::LoadVerifyDbEmail(user_opt, all_emails)) {
    return -1;
  }

  int y_ = y;
  email_challenge::EmailCodeChallenge(check_email, email_str, all_emails, prompt, ip, filename, y_);
  if (out_y != NULL)
    *out_y = y_;

  return 0;
}
