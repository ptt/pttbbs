extern "C" {
#include "bbs.h"
#include "daemons.h"
}

#ifdef USE_VERIFYDB_ACCOUNT_RECOVERY

# ifndef USE_VERIFYDB
#   error "USE_VERIFYDB_ACCOUNT_RECOVERY requires USE_VERIFYDB"
# endif //USE_VERIFYDB

#include <optional>
#include <string>

#include "verifydb.h"
#include "verifydb.fbs.h"

#include "email_challenge.hpp"
#include "user_handle.hpp"

namespace emailchallenge {

constexpr int kMaxErrCnt = 3;
constexpr size_t kCodeLen = 30;

static bool LoadVerifyDbEmail(const userhandle::UserHandle &user, std::vector<std::string> &all_emails);
static bool AppendNonRepeatedEmail(const std::optional<std::string> &email, std::vector<std::string> &all_emails);
static std::optional<std::string> NormalizeEmail(const std::string &email);
static std::string GenCode(size_t len);
static bool SendChallengeCode(const std::string &email, const std::string &code, const std::string &prompt, const std::string &ip, const std::string &filename);
static void UserErrorExit();

// EmailChallenge
//
// Challenge user by sending randomized-code to the user's email.
// Expected to compatible with AccountRecovery::EmailCodeChallenge,
// which challenge with both contact-email and verify-email.
//
// However, it is possible that there are scenarios that
// the user does not provide the input-email.
// We send to the contact email of the user.
//
// params:
//     is_check_email: whether to check email. ignore email if is_check_email is false.
//     email: user-input-email
//     user: user
//     y: starting y onscreen
//     prompt: prompt for the email title. (prompt [code] @ IP ip)
//     ip: ip for the email title.
//     filename: filename for the email template.
//
// return:
//     bool: true: success / false: fail
//     out_y: cursor-y after returning from the function.
bool EmailChallenge(bool is_check_email, const std::string &email, const userhandle::UserHandle &user, const int y, const std::string &prompt, const std::string &ip, const std::string &filename, int *out_y) {
  std::vector<std::string> all_emails = {};
  std::optional<std::string> normalized_contact_email_opt = NormalizeEmail(user.email);
  std::string normalized_contact_email = {};
  if(normalized_contact_email_opt) {
    normalized_contact_email = normalized_contact_email_opt.value();
  }

  /////
  // 1. Load all emails
  /////
  if(is_check_email) {
    // load contact email
    // ref: AccountRecovery::LoadUser
#ifdef USEREC_EMAIL_IS_CONTACT
    if(normalized_contact_email_opt) {
      all_emails.push_back(normalized_contact_email);
    }
#endif //USEREC_EMAIL_IS_CONTACT

    // load verify-db email (regemail).
    // ref: AccountRecovery::InputUserEmail
    if(!LoadVerifyDbEmail(user, all_emails)) {
      vmsg("系統錯誤，請稍候再試。");
      exit(0);
    }
  }

  /////
  // 2. GenCode
  // ref: AccountRecovery::EmailCodeChallenge
  /////
  auto code = GenCode(kCodeLen);

  /////
  // 2.1. prompt
  /////
  int current_y = y;
  current_y++;
  mvprints(current_y, 0, "系統處理中...");
  doupdate();

  /////
  // 3. Check and send challenge code. Don't exit if email doesn't match, so user
  // can't guess email.
  /////
  bool email_matches = false;
  if(is_check_email) {
    std::optional<std::string> normalized_email_opt = NormalizeEmail(email);
    std::string normalized_email = {};
    if(normalized_email_opt) {
      normalized_email = normalized_email_opt.value();
      //in case that normalized_email is a copy of email.
      for (const auto &each_email : all_emails) {
        if (each_email == normalized_email) {
          email_matches = true;
        }
      }
    }

    if (email_matches) {
      // We only want to send email if the user input the matching address.
      // Silently fail if email is not matching, so that user cannot guess other
      // user's email address.
      //
      // normalized_email is guaranteed to have valid email.
      SendChallengeCode(normalized_email, code, prompt, ip, filename);
    }
  } else {
    // We send to the contact email.
    email_matches = true;
    if(normalized_contact_email_opt) {
      SendChallengeCode(normalized_contact_email, code, prompt, ip, filename);
    }
  }

  /////
  // 4. Add a random 5-10s delay to prevent timing oracle.
  /////
  usleep(5000000 + random() % 5000000);
  if(is_check_email) {
    mvprints(current_y++, 0, "若您輸入的資料正確，系統已將認證碼寄送至您的信箱。");
  } else {
    mvprints(current_y++, 0, "系統已將認證碼寄送至您的聯絡信箱。");
  }

  /////
  // 5. Input code.
  /////
  char incode[kCodeLen + 1] = {};
  int errcnt = 0;
  while (1) {
    getdata_buf(current_y, 0, "請收信後輸入認證碼：", incode, sizeof(incode), DOECHO);
    if (email_matches && code == std::string(incode)) {
      break;
    }
    if (++errcnt >= kMaxErrCnt) {
      UserErrorExit();
    }
    mvprints(current_y + 1, 0, "認證碼錯誤！認證碼共有 %d 字元。", (int)kCodeLen);
    incode[0] = '\0';
  }
  current_y++;
  move(current_y, 0);
  clrtoeol(); // There might be error message at this line.

  /////
  // 6. Some paranoid checkings. we are about to let user pass email-challenge.
  /////
  if (code.size() != kCodeLen || code != std::string(incode)) {
    assert(false);
    exit(1);
  }

  if(out_y != NULL) {
    *out_y = current_y;
  }

  return true;
}

static bool LoadVerifyDbEmail(const userhandle::UserHandle &user, std::vector<std::string> &all_emails) {
  Bytes buf = {};
  // reply is pointing to buf. no need to free reply in the end.
  const VerifyDb::GetReply *reply = NULL;
  if (!verifydb_getuser(user.userid.c_str(), user.generation, &buf, &reply)) {
    return false;
  }

  if (reply->entry()) {
    for (const auto *ent : *reply->entry()) {
      if (ent->vmethod() == VMETHOD_EMAIL && ent->vkey() != nullptr) {
        auto email = NormalizeEmail(ent->vkey()->str());
        AppendNonRepeatedEmail(email, all_emails);
      }
    }
  }

  return true;
}

// AppendNonRepeatedEmail
//
// Append only non-repeated emails.
// It is expected that there are only 2-3 emails.
// It should be ok that we simply use for-loop
// to check non-repeated emails.
//
// Return:
//   bool: true: successfully appended / false: no append
//   all_emails: appended email list.
static bool AppendNonRepeatedEmail(const std::optional<std::string> &email, std::vector<std::string> &all_emails) {
  if(!email) {
    return false;
  }

  bool is_match = false;
  for (const auto &each_email : all_emails) {
    if(each_email == email) {
      is_match = true;
      break;
    }
  }
  if(!is_match) {
    all_emails.push_back(email.value());
    return true;
  }

  return false;
}

static std::optional<std::string> NormalizeEmail(const std::string &email) {
  auto out = email;
  for (auto& c : out) {
    c = std::tolower(c);
  }
  auto idx = out.find('@');
  if (idx == std::string::npos) {
    return std::nullopt;
  }
  if (idx != out.rfind('@')) {
    return std::nullopt;
  }
  return out;
}

static std::string GenCode(size_t len) {
  std::string s(len, '\0');
  random_text_code(&s[0], s.size());
  return s;
}

static bool SendChallengeCode(const std::string &email,
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

static void UserErrorExit() {
  vmsg("錯誤次數過多，請重新操作。");
  exit(0);
}

} // namespace

// email_challenge
// user: user
// y: starting y onscreen
// out_y: cursor-y after returning from the function.
// prompt: prompt for the email title. (prompt [code] @ IP ip)
// filename: filename for the email template.
//
// return:
//     int: non-zero: success / 0: failed
//     out_y: cursor-y after returning from the function.
int email_challenge(const char *email, const userec_t *user, const int y, const char *prompt, const char *filename, int *out_y) {
  std::string email_str = {};
  bool is_check_email = false;
  if(email != NULL) {
    email_str = std::string(email);
    is_check_email = true;
  }

  userhandle::UserHandle user_handle = {};
  bool ret = userhandle::InitUserHandle(user, user_handle);
  if(!ret) {
    return 0;
  }

  return emailchallenge::EmailChallenge(is_check_email, email_str, user_handle, y, prompt, fromhost, filename, out_y);
}

#endif //USE_VERIFYDB_ACCOUNT_RECOVERY
