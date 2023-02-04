extern "C" {
#include "bbs.h"
#include "daemons.h"
}

#ifdef USE_VERIFYDB_ACCOUNT_RECOVERY

#include <optional>
#include <string>
#include <vector>

#include "verifydb.h"
#include "verifydb.fbs.h"

#include "user_handle.hpp"
#include "email_challenge.hpp"

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
void LoadUserEmail(const userec_t &u, std::vector<std::string> &all_emails) {
#ifdef USEREC_EMAIL_IS_CONTACT
  auto email = NormalizeEmail(u.email);
  if (email)
    all_emails.push_back(email.value());
#endif
}

// LoadVerifyDbEmail
//
// Params:
//   user:       UserHandle
//   all_emails: all valid emails.
//
// Return:
//   bool: true: ok false: err
bool LoadVerifyDbEmail(const std::optional<UserHandle> &user,
                       std::vector<std::string> &all_emails) {
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
  return true;
}

// static
bool SendChallengeCode(const std::string &email,
                      const std::string &code) {
  std::string subject;
  subject.append(" " BBSNAME " - ���]�K�X�{�ҽX [ ");
  subject.append(code);
  subject.append(" ] @ IP ");
  subject.append(fromhost);
  bsmtp("etc/recovermail", subject.c_str(), email.c_str(), "non-exist");
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
  vmsg("���~���ƹL�h�A�Э��s�ާ@�C");
  exit(0);
}

// EmailCodeChallenge
//
// Params:
//   email:       user-input email
//   all_emails:  all valid emails
//
//   y:           starting y on screen, updated along with the function.
void EmailCodeChallenge(const std::string &email,
                        const std::vector<std::string> &all_emails,
                        int &y) {
  // Generate code.
  auto code = GenCode(kCodeLen);

  y++;
  mvprints(y, 0, "�t�γB�z��...");
  doupdate();

  // Check and send recovery code. Don't exit if email doesn't match, so user
  // can't guess email.
  bool email_matches = false;
  for (const auto &each_email : all_emails) {
    if (each_email == email)
      email_matches = true;
  }

  if (email_matches) {
    // We only want to send email if the user input the matching address.
    // Silently fail if email is not matching, so that user cannot guess other
    // user's email address.
    SendChallengeCode(email, code);
  }
  // Add a random 5-10s delay to prevent timing oracle.
  usleep(5000000 + random() % 5000000);
  mvprints(y++, 0, "�Y�z��J����ƥ��T�A�t�Τw�N�{�ҽX�H�e�ܱz���H�c�C");

  // Input code.
  char incode[kCodeLen + 1] = {};
  int errcnt = 0;
  while (1) {
    getdata_buf(y, 0, "�Ц��H���J�{�ҽX�G", incode, sizeof(incode), DOECHO);
    if (email_matches && code == std::string(incode))
      break;
    if (++errcnt >= kMaxErrCnt)
      UserErrorExit();
    mvprints(y + 1, 0, "�{�ҽX���~�I�{�ҽX�@�� %d �r���C", (int)kCodeLen);
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

#endif
