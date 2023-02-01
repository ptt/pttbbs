extern "C" {
#include "bbs.h"
#include "daemons.h"
}

#ifdef USE_VERIFYDB_ACCOUNT_RECOVERY

# ifndef USE_VERIFYDB
#   error "USE_VERIFYDB_ACCOUNT_RECOVERY requires USE_VERIFYDB"
# endif

#include <optional>
#include <string>
#include <vector>

#include "user_handle.hpp"
#include "email_challenge.hpp"

namespace {

constexpr int kMaxErrCnt = 3;

class AccountRecovery {
public:
  void Run();

private:
  std::optional<UserHandle> user_;
  std::string email_;
  std::vector<std::string> all_emails_;
  int y_ = 2;

  bool LoadUser(const char *userid);
  void InputUserEmail();
  void ResetPasswd();

  static bool SetPasswd(const UserHandle &user, const char *hashed_passwd);
  static void LogToSecurity(const UserHandle &user, const std::string &email);
  static void NotifyUser(const std::string &userid, const std::string &email);
  static void UserErrorExit();
};

bool AccountRecovery::LoadUser(const char *userid) {
  if (!is_validuserid(userid))
    return false;

  userec_t u = {};
  if (!getuser(userid, &u))
    return false;

  // Double check.
  if (!is_validuserid(u.userid))
    return false;

  UserHandle user;
  user.userid = u.userid;
  user.generation = u.firstlogin;
  user_ = user;

  LoadUserEmail(u, all_emails_);

  return true;
}

// static
bool AccountRecovery::SetPasswd(const UserHandle &user,
                                const char *hashed_passwd) {
  userec_t u = {};
  int unum = getuser(user.userid.c_str(), &u);
  if (unum <= 0)
    return false;
  // Check userid and generation again.
  if (std::string(u.userid) != user.userid || u.firstlogin != user.generation)
    return false;
  strlcpy(u.passwd, hashed_passwd, sizeof(u.passwd));
  return 0 == passwd_sync_update(unum, &u);
}


// static
void AccountRecovery::LogToSecurity(const UserHandle &user,
                                    const std::string &email) {
  std::string title = "���]�K�X: ";
  title.append(user.userid);
  title.append(" (�H�H�c�{�Ҩ���)");

  std::string msg;
  msg.append("userid: ");
  msg.append(user.userid);
  msg.append("\nfirstlogin: ");
  msg.append(std::to_string(user.generation));
  msg.append("\nemail: ");
  msg.append(email);
  msg.append("\nip: ");
  msg.append(fromhost);
  msg.append("\n");

  post_msg(BN_SECURITY, title.c_str(), msg.c_str(), "[�t�Φw����]");
}

// static
void AccountRecovery::UserErrorExit() {
  vmsg("���~���ƹL�h�A�Э��s�ާ@�C");
  exit(0);
}

void AccountRecovery::InputUserEmail() {
#ifndef USEREC_EMAIL_IS_CONTACT
  mvprints(y_, 0, "�ثe�ȯ���^�ϥ� Email ���U���b���C");
#endif
  y_ += 2;

  // Input userid.
  char userid[IDLEN + 1] = {};
  int errcnt = 0;
  while (1) {
    getdata_buf(y_, 0, "�����^�b��ID�G", userid, sizeof(userid), DOECHO);
    if (LoadUser(userid))
      break;
    if (++errcnt >= kMaxErrCnt)
      UserErrorExit();
    mvprints(y_ + 1, 0, "�L�� ID�A�Э��s��J�C");
    userid[0] = '\0';
  }
  y_++;

  // Input email.
  char email[EMAILSZ] = {};
  errcnt = 0;
  while (1) {
    getdata_buf(y_, 0, "�{�ҫH�c�G", email, sizeof(email), DOECHO);
    strip_blank(email, email);
    str_lower(email, email);
    if (*email)
      break;
    if (++errcnt >= kMaxErrCnt)
      UserErrorExit();
    mvprints(y_ + 1, 0, "�Э��s��J�C");
    email[0] = '\0';
  }
  email_ = email;
  y_++;
  move(y_, 0);
  clrtoeol(); // There might be error message at this line.

  if (!LoadVerifyDbEmail(user_, all_emails_)) {
    vmsg("�t�ο��~�A�еy�ԦA�աC");
    exit(0);
  }
}

void AccountRecovery::ResetPasswd() {
  // Input new password.
  const char *hashed = nullptr;
  y_++;
  int errcnt = 0;
  while (1) {
    if (errcnt++ >= kMaxErrCnt * 2)
      UserErrorExit();

    char pass[PASS_INPUT_LEN + 1] = {};
    char confirm[PASS_INPUT_LEN + 1] = {};

    move(y_, 0);
    clrtobot();
    if (!getdata(y_, 0, "�г]�w�s�K�X�G", pass, sizeof(pass), PASSECHO) ||
        strlen(pass) < 4) {
      vmsg("�Э��s��J�A�K�X�����ܤ֥|�r��");
      continue;
    }

    move(y_ + 1, 0);
    outs("�Ъ`�N�]�w�K�X�u���e�K�Ӧr�����ġA�W�L���N�۰ʩ����C");

    getdata(y_ + 2, 0, "���ˬd�s�K�X�G", confirm, sizeof(confirm), PASSECHO);
    if (strcmp(pass, confirm)) {
      vmsg("�s�K�X�T�{����, �Э��s��J");
      continue;
    }

    // Hash password.
    hashed = genpasswd(pass);
    memset(pass, 0, sizeof(pass));
    memset(confirm, 0, sizeof(confirm));
    break;
  }
  assert(hashed);

  if (!SetPasswd(user_.value(), hashed)) {
    vmsg("�K�X���]���ѡA�еy�ԦA�աC");
    return;
  }

  LogToSecurity(user_.value(), email_);
  for (const auto &email : all_emails_) {
    notify_password_change(user_->userid.c_str(), email.c_str());
  }

  // Log to user security.
  {
    char logfn[PATHLEN];
    sethomefile(logfn, user_->userid.c_str(), FN_USERSECURITY);
    log_filef(logfn, LOG_CREAT, "%s %s (ResetPasswd) Email: %s\n",
              Cdatelite(&now), fromhost, email_.c_str());
  }

  vmsg("�K�X���]�����A�ХH�s�K�X�n�J�C");
}

void AccountRecovery::Run() {
  vs_hdr("���^�b��");
  InputUserEmail();
  EmailCodeChallenge(email_, all_emails_, y_);
  ResetPasswd();
}

}  // namespace

void recover_account() {
#ifdef USE_REMOTE_CAPTCHA
  const char *errmsg = remote_captcha();
  if (errmsg) {
    vmsg(errmsg);
    exit(1);
  }
#endif

  AccountRecovery ac;
  ac.Run();
  exit(0);
}

#endif
