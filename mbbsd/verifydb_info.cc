extern "C" {
#include "bbs.h"
#include "daemons.h"
#include "psb.h"
}

#ifdef USE_VERIFYDB

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "flatbuffers/flatbuffers.h"
#include "verifydb.h"
#include "verifydb.fbs.h"

namespace {

void verify_display(const userec_t *u, const VerifyDb::GetReply *reply) {
  vs_hdr("�{�Ҹ��");
  move(2, 0);
  clrtobot();

  int i = 2;
  mvprints(i++, 0, "�ϥΪ̥N��: %s", u->userid);
  i++;

  if (!reply->entry() || reply->entry()->size() == 0)
    mvprints(i++, 0, "�d�L�{�Ҹ��");
  else {
    int num = 0;
    for (const auto *entry : *reply->entry()) {
      num++;
      auto disp =
          FormatVerify(entry->vmethod(), entry->vkey(), /* ansi_color */ true);
      if (disp) {
        mvprints(i++, 0, "[%d] %s", num, disp->c_str());
        time4_t ts = entry->timestamp();
        if (ts)
          mvprints(i++, 4 + 11, "%s", Cdate(&ts));
      } else {
        mvprints(i++, 0, "[%d] (�������{�Ҥ覡)", num);
      }
    }
  }
}

bool write_email_to_passwd(const char *userid, const char *email) {
#ifndef USEREC_EMAIL_IS_CONTACT
  userec_t u = {};
  int unum = passwd_load_user(userid, &u);
  if (unum < 0)
    return false;
  strlcpy(u.email, email, sizeof(u.email));
  strlcpy(u.justify, *email ? "<E-Mail>: Manual" : "", sizeof(u.justify));
  if (passwd_sync_update(unum, &u) < 0)
    return false;
#endif
  return true;
}

void verify_entry_email_edit(const VerifyDb::Entry *entry, bool *dirty) {
  int y = b_lines - 6;
  move(y, 0);
  clrtobot();

  char email[EMAILSZ] = {};
  do {
    getdata_buf(y, 2, "  (�s�H�c) ", email, sizeof(email), DOECHO);
    strip_blank(email, email);
    str_lower(email, email);

    if (!*email)
      return;

    // Simple quick check.
    if (auto first = strchr(email, '@');
        first == nullptr || first != strrchr(email, '@')) {
      move(y + 1, 2 + 11);
      // clang-format off
      outs(ANSI_COLOR(1;31));
      // clang-format on
      outs("���~: �H�c���榡�����T");
      outs(ANSI_RESET);
      email[0] = '\0';
      continue;
    }

    const char *errmsg;
    if (!check_email_allow_reject_lists(email, &errmsg, nullptr)) {
      move(y + 1, 2 + 11);
      // clang-format off
      outs(ANSI_COLOR(1;31));
      // clang-format on
      outs("�t�Υثe�����\\�H���H�c���U, ���~�T����:");
      outs(ANSI_RESET);
      move(y + 2, 2 + 11);
      outs(ANSI_COLOR(1));
      outs(errmsg);
      outs(ANSI_RESET);

      char force_in[2] = {};
      getdata_buf(y + 4, 2 + 11, "���n�ϥΦ��H�c? [y/N]", force_in,
                  sizeof(force_in), DOECHO);
      move(y + 1, 0);
      clrtobot();
      if (tolower(force_in[0]) != 'y')
        continue;
    }
    break;
  } while (1);
  y++;

  mvprints(y++, 2, "  (�t�έ�) %s", email);
  clrtoeol(); // We might have output an error message earlier.

  char setts_in[2] = {'y'};
  getdata_buf(y++, 2 + 11, "�N�{�Үɶ��]���{�b? [Y/n]", setts_in,
              sizeof(setts_in), DOECHO);
  bool setts = tolower(setts_in[0]) != 'n';

  move(y++, 2 + 11);
  // clang-format off
  outs(ANSI_COLOR(1;33));
  // clang-format on
  outs("�`�N: �ק�{�Ҹ�� ���| ��ʨϥΪ̪��{�Ҫ��A.");
  outs(ANSI_RESET);

  move(y++, 2 + 11);
  // clang-format off
  outs(ANSI_COLOR(1;33));
  // clang-format on
  outs("      �p�ݭק�{�Ҫ��A, �Шϥ��v���ܧ�ﶵ.");
  outs(ANSI_RESET);

  char confirm[2] = {};
  getdata_buf(y++, 2 + 11, "�T�{�g�J? [y/N]", confirm, sizeof(confirm), DOECHO);
  if (tolower(confirm[0]) != 'y')
    return;

  // Mark as dirty anyway.
  *dirty = true;
  const char *userid = entry->userid()->c_str();
  if (verifydb_set(userid, entry->generation(), entry->vmethod(), email,
                   setts ? 0 : entry->timestamp()) < 0) {
    vmsg("�g�J���ѡA�еy�ԦA�աC");
    return;
  }

  // Cannot sync cross systems. Fail gracefully.
  bool ok = true;
  if (emaildb_update_email(userid, email) < 0)
    ok = false;
  if (!write_email_to_passwd(userid, email))
    ok = false;
  if (!ok)
    vmsg("������Ƽg�J���ѡA�Ц^�������C");
}

void verify_entry_email_delete(const VerifyDb::Entry *entry, bool *dirty) {
  if (vans("�T�{�R�������O��? [y/N]") != 'y')
    return;

  // Mark dirty anyway.
  *dirty = true;
  const char *userid = entry->userid()->c_str();
  if (verifydb_set(userid, entry->generation(), entry->vmethod(), "", 0) < 0)
    vmsg("�{�Ҹ�Ʈw�ȮɵL�k�ϥΡA�еy�ԦA�աC");

  // Cannot sync cross systems. Fail gracefully.
  bool ok = true;
  if (emaildb_update_email(userid, "x") < 0)
    ok = false;
  if (!write_email_to_passwd(userid, ""))
    ok = false;
  if (!ok)
    vmsg("������Ƽg�J���ѡA�Ц^�������C");
}

void verify_entry_email_popup(const VerifyDb::Entry *entry, bool *dirty) {
  assert(entry->vmethod() == VMETHOD_EMAIL);

  switch (vans("(E)�ק� (D)�R�� (Q)���� : [Q]")) {
  case 'e':
    verify_entry_email_edit(entry, dirty);
    break;

  case 'd':
    verify_entry_email_delete(entry, dirty);
    break;
  }
}

void verify_entry_sms_delete(const VerifyDb::Entry *entry, bool *dirty) {
  if (vans("�T�{�R�������O��? [y/N]") != 'y')
    return;

  // Mark dirty anyway.
  *dirty = true;
  const char *userid = entry->userid()->c_str();
  if (verifydb_set(userid, entry->generation(), entry->vmethod(), "", 0) < 0)
    vmsg("�{�Ҹ�Ʈw�ȮɵL�k�ϥΡA�еy�ԦA�աC");
}

void verify_entry_sms_popup(const VerifyDb::Entry *entry, bool *dirty) {
  assert(entry->vmethod() == VMETHOD_SMS);

  switch (vans("(D)�R�� (Q)���� : [Q]")) {
  case 'd':
    verify_entry_sms_delete(entry, dirty);
    break;
  }
}

void verify_entry_edit_popup(const VerifyDb::Entry *entry, bool *dirty) {
  int y = b_lines - 10;
  grayout(0, y, GRAYOUT_DARK);

  move(y++, 0);
  vbarf(ANSI_REVERSE " �{�Ҹ�Ƴ]�w\t" ANSI_RESET);

  y++;
  auto disp =
      FormatVerify(entry->vmethod(), entry->vkey(), /* ansi_color */ true);
  if (disp) {
    mvprints(y++, 2, "%s", disp->c_str());
    time4_t ts = entry->timestamp();
    if (ts)
      mvprints(y++, 2 + 11, "%s", Cdate(&ts));
  } else {
    mvprints(y++, 2, "(�������{�Ҥ覡)");
  }

  switch (entry->vmethod()) {
  case VMETHOD_EMAIL:
    verify_entry_email_popup(entry, dirty);
    break;

  case VMETHOD_SMS:
    verify_entry_sms_popup(entry, dirty);
    break;

  default:
    // Can't do anything.
    vans("(Q)���� : [Q]");
  }
}

void verify_entry_add_popup(const userec_t *u,
                            const VerifyDb::GetReply *existing, bool *dirty) {
  int y = b_lines - 10;
  grayout(0, y, GRAYOUT_DARK);

  move(y++, 0);
  vbarf(ANSI_REVERSE " �s�W�{�Ҹ��\t" ANSI_RESET);
  clrtobot();

  // Verify method input.
  y++;
  mvprints(y++, 2, "(1)�q�l�H�c");

  char method_str[2] = {};
  getdata_buf(y++, 2, "��ܻ{�Ҥ覡: ", method_str, sizeof(method_str), DOECHO);

  // Generate a list of used methods.
  std::set<int32_t> used;
  if (existing->entry())
    for (const auto *entry : *existing->entry())
      used.insert(entry->vmethod());

  switch (method_str[0]) {
  case '1':
    if (used.count(VMETHOD_EMAIL)) {
      vmsg("�w�����{�Ҥ覡����ơA�Шϥέק�\\��C");
      return;
    }
    // Create a fake entry for calling edit.
    flatbuffers::FlatBufferBuilder fbb;
    fbb.Finish(VerifyDb::CreateEntryDirect(fbb, u->userid, u->firstlogin,
                                           VMETHOD_EMAIL, "", 0));
    const auto *entry =
        flatbuffers::GetRoot<VerifyDb::Entry>(fbb.GetBufferPointer());
    verify_entry_email_edit(entry, dirty);
    break;
  }
}

}  // namespace

void verify_info(const userec_t *u, int adminmode) {
  vs_hdr("�{�Ҹ��");

  bool dirty = true;
  const VerifyDb::GetReply *reply;
  Bytes buf;
  while (1) {
    if (dirty) {
      if (!verifydb_getuser(u->userid, u->firstlogin, &buf, &reply) ||
          !reply->ok()) {
        vmsg("�{�Ҩt�εL�k�ϥΡA�еy�ԦA�աC");
        return;
      }
      dirty = false;
    }

    verify_display(u, reply);

    if (!adminmode) {
      pressanykey();
      break;
    }

    // Prepare menu.
    int max = reply->entry() ? reply->entry()->size() : 0;
    std::string msg;
    if (max) {
      msg += "(1";
      if (max > 1) {
        msg += "-";
        msg += std::to_string(reply->entry()->size());
      }
      msg += ")�ק� ";
    }
    msg += "(A)�s�W (Q)���} : [Q]";

    int c = vans(msg.c_str());
    if (c == 'a') {
      verify_entry_add_popup(u, reply, &dirty);
    } else if (c >= '1' && c <= '9') {
      int idx = c - '1';
      if (idx >= max)
        break;
      verify_entry_edit_popup(reply->entry()->Get(idx), &dirty);
    } else
      break;
  }
}

#endif
