/*****
 * This is mostly based on recover.cc
 *****/
extern "C" {
#include "bbs.h"
#include "daemons.h"
}

#if defined(USE_VERIFYDB_ACCOUNT_RECOVERY) || defined(USEREC_EMAIL_IS_CONTACT)

#if defined(USE_VERIFYDB_ACCOUNT_RECOVERY) && !defined(USE_VERIFYDB)
#   error "USE_VERIFYDB_ACCOUNT_RECOVERY requires USE_VERIFYDB"
#endif

#include <optional>
#include <string>

#include "verifydb.h"
#include "verifydb.fbs.h"

namespace {

constexpr int kMaxErrCnt = 3;
constexpr size_t kCodeLen = 30;

struct EmailUserHandle {
  std::string userid;
  std::string email;

  int64_t generation;
};

class EmailChallenge {
public:
  int Challenge(const userec_t *u, const std::string &email_prompt, const std::string &email_template_fpath);

private:
  std::optional<EmailUserHandle> user_;
  std::string email_;
  std::vector<std::string> all_emails_;
  int y_ = 2;

  bool LoadUserEmail(const userec_t *u);
  bool LoadVerifyDbEmail();
  bool InputUserEmail();
  bool EmailCodeChallenge(const std::string &email_prompt, const std::string &email_template_fpath);

  static std::optional<std::string> NormalizeEmail(const std::string &email);
  static bool SendChallengeCode(const std::string &email,
                                const std::string &code,
                                const std::string &email_prompt,
                                const std::string &email_template_fpath);
  static std::string GenCode(size_t len);
  static void UserErrorExit();
};

// static
std::optional<std::string>
EmailChallenge::NormalizeEmail(const std::string &email) {
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

// static
bool EmailChallenge::SendChallengeCode(const std::string &email,
                                       const std::string &code,
                                       const std::string &email_prompt,
                                       const std::string &email_template_fpath) {
  std::string subject;
  subject.append(email_prompt.c_str());
  subject.append(code);
  subject.append(" ] @ IP ");
  subject.append(fromhost);
  bsmtp(email_template_fpath.c_str(), subject.c_str(), email.c_str(), "non-exist");

  return true;
}

bool EmailChallenge::LoadUserEmail(const userec_t *u) {
#ifdef USEREC_EMAIL_IS_CONTACT
  std::string uemail = u->email;
  auto email = NormalizeEmail(uemail);
  if (email) {
    all_emails_.push_back(email.value());
  }
#endif

  return true;
}

bool EmailChallenge::LoadVerifyDbEmail() {
  Bytes buf;
  const VerifyDb::GetReply *reply;
  if (!verifydb_getuser(user_->userid.c_str(), user_->generation, &buf, &reply))
    return false;

  if (reply->entry()) {
    for (const auto *ent : *reply->entry()) {
      if (ent->vmethod() == VMETHOD_EMAIL && ent->vkey() != nullptr) {
        auto email = NormalizeEmail(ent->vkey()->str());
        if (email)
          all_emails_.push_back(email.value());
      }
    }
  }
  return true;
}

// static
std::string EmailChallenge::GenCode(size_t len) {
  std::string s(len, '\0');
  random_text_code(&s[0], s.size());
  return s;
}

// static
void EmailChallenge::UserErrorExit() {
  vmsg("錯誤次數過多，請重新操作。");
  exit(0);
}

bool EmailChallenge::InputUserEmail() {
  // Input email.
  char email[EMAILSZ] = {};
  int errcnt = 0;
  while (1) {
    getdata_buf(y_, 0, "認證/聯絡信箱：", email, sizeof(email), DOECHO);
    strip_blank(email, email);
    str_lower(email, email);
    if (*email) {
      break;
    }
    if (++errcnt >= kMaxErrCnt) {
      return false;
    }
    mvprints(y_ + 1, 0, "請重新輸入。");
    email[0] = '\0';
  }
  email_ = email;
  y_++;
  move(y_, 0);
  clrtoeol(); // There might be error message at this line.

  if (!LoadVerifyDbEmail()) {
    vmsg("系統錯誤，請稍候再試。");
    exit(0);
  }

  return true;
}

bool EmailChallenge::EmailCodeChallenge(const std::string &email_prompt, const std::string &email_template_fpath) {
  // Generate code.
  auto code = GenCode(kCodeLen);

  y_++;
  mvprints(y_, 0, "系統處理中...");
  doupdate();

  // Check and send recovery code. Don't exit if email doesn't match, so user
  // can't guess email.
  bool email_matches = false;
  for (const auto &email : all_emails_) {
    if (email == email_) {
      email_matches = true;
    }
  }

  if (email_matches) {
    // We only want to send email if the user input the matching address.
    // Silently fail if email is not matching, so that user cannot guess other
    // user's email address.
    SendChallengeCode(email_, code, email_prompt, email_template_fpath);
  }
  // Add a random 5-10s delay to prevent timing oracle.
  usleep(5000000 + random() % 5000000);
  mvprints(y_++, 0, "若您輸入的資料正確，系統已將認證碼寄送至您的信箱。");

  // Input code.
  char incode[kCodeLen + 1] = {};
  int errcnt = 0;
  while (1) {
    getdata_buf(y_, 0, "請收信後輸入認證碼：", incode, sizeof(incode), DOECHO);
    if (email_matches && code == std::string(incode))
      break;
    if (++errcnt >= kMaxErrCnt)
      UserErrorExit();
    mvprints(y_ + 1, 0, "認證碼錯誤！認證碼共有 %d 字元。", (int)kCodeLen);
    incode[0] = '\0';
  }
  y_++;
  move(y_, 0);
  clrtoeol(); // There might be error message at this line.

  // Some paranoid checkings, we are about to let user reset their password.
  if (code.size() != kCodeLen || code != std::string(incode)) {
    assert(false);
    exit(1);
  }

  return true;
}

int EmailChallenge::Challenge(const userec_t *u, const std::string &email_prompt, const std::string &email_template_fpath) {
  EmailUserHandle user;
  user.userid = u->userid;
  user.generation = u->firstlogin;
  user_ = user;

  vs_hdr("驗證認證/聯絡信箱");

  if (!LoadUserEmail(u)) {
    return ERR_EMAIL_CHALLENGE_LOAD_USER_EMAIL;
  }
  if (!InputUserEmail()) {
    return ERR_EMAIL_CHALLENGE_INPUT_USER_EMAIL;
  }
  if (!EmailCodeChallenge(email_prompt, email_template_fpath)) {
    return ERR_EMAIL_CHALLENGE_EMAIL_CODE_CHALLENGE;
  }

  return ERR_OK;
}

}  // namespace

int email_challenge(const userec_t *u, const char *email_prompt, const char *email_template_fpath) {
  if(!HasUserPerm(PERM_LOGINOK)) {
    vmsg("尚未完成認證！");
    return ERR_INVALID_PERM;
  }
#ifdef USE_REMOTE_CAPTCHA
  const char *errmsg = remote_captcha();
  if (errmsg) {
    vmsg(errmsg);
    exit(1);
  }
#endif

  EmailChallenge ec;
  return ec.Challenge(u, email_prompt, email_template_fpath);
}

#endif
