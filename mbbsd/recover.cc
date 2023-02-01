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
  std::string title = "重設密碼: ";
  title.append(user.userid);
  title.append(" (以信箱認證身份)");

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

  post_msg(BN_SECURITY, title.c_str(), msg.c_str(), "[系統安全局]");
}

// static
void AccountRecovery::UserErrorExit() {
  vmsg("錯誤次數過多，請重新操作。");
  exit(0);
}

void AccountRecovery::InputUserEmail() {
#ifndef USEREC_EMAIL_IS_CONTACT
  mvprints(y_, 0, "目前僅能取回使用 Email 註冊之帳號。");
#endif
  y_ += 2;

  // Input userid.
  char userid[IDLEN + 1] = {};
  int errcnt = 0;
  while (1) {
    getdata_buf(y_, 0, "欲取回帳號ID：", userid, sizeof(userid), DOECHO);
    if (LoadUser(userid))
      break;
    if (++errcnt >= kMaxErrCnt)
      UserErrorExit();
    mvprints(y_ + 1, 0, "無此 ID，請重新輸入。");
    userid[0] = '\0';
  }
  y_++;

  // Input email.
  char email[EMAILSZ] = {};
  errcnt = 0;
  while (1) {
    getdata_buf(y_, 0, "認證信箱：", email, sizeof(email), DOECHO);
    strip_blank(email, email);
    str_lower(email, email);
    if (*email)
      break;
    if (++errcnt >= kMaxErrCnt)
      UserErrorExit();
    mvprints(y_ + 1, 0, "請重新輸入。");
    email[0] = '\0';
  }
  email_ = email;
  y_++;
  move(y_, 0);
  clrtoeol(); // There might be error message at this line.

  if (!LoadVerifyDbEmail(user_, all_emails_)) {
    vmsg("系統錯誤，請稍候再試。");
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
    if (!getdata(y_, 0, "請設定新密碼：", pass, sizeof(pass), PASSECHO) ||
        strlen(pass) < 4) {
      vmsg("請重新輸入，密碼必須至少四字元");
      continue;
    }

    move(y_ + 1, 0);
    outs("請注意設定密碼只有前八個字元有效，超過的將自動忽略。");

    getdata(y_ + 2, 0, "請檢查新密碼：", confirm, sizeof(confirm), PASSECHO);
    if (strcmp(pass, confirm)) {
      vmsg("新密碼確認失敗, 請重新輸入");
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
    vmsg("密碼重設失敗，請稍候再試。");
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

  vmsg("密碼重設完成，請以新密碼登入。");
}

void AccountRecovery::Run() {
  vs_hdr("取回帳號");
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
