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

class EntryBrowser {
public:
  static int SearchDisplay(int32_t vmethod, const char *vkey);

  int Header();
  int Footer();
  int Renderer(int i, int curr);
  int InputProcessor(PSB_CTX *ctx);

private:
  Bytes buf_;
  std::vector<const VerifyDb::Entry *> entries_;
  std::string title_;

  EntryBrowser() {}
  EntryBrowser(const EntryBrowser &) = delete;
  EntryBrowser &operator=(EntryBrowser &) = delete;

  int Display();
};

extern "C" {

int EntryBrowserHeader(PSB_CTX *ctx) {
  return reinterpret_cast<EntryBrowser *>(ctx->cmd.priv)->Header();
}
int EntryBrowserFooter(PSB_CTX *ctx) {
  return reinterpret_cast<EntryBrowser *>(ctx->cmd.priv)->Footer();
}
int EntryBrowserRenderer(int i, PSB_CTX *ctx) {
  return reinterpret_cast<EntryBrowser *>(ctx->cmd.priv)->Renderer(i, ctx->cmd.curr);
}
int EntryBrowserOnKey(PSB_CTX *ctx) {
  return reinterpret_cast<EntryBrowser *>(ctx->cmd.priv)->InputProcessor(ctx);
}

}

int EntryBrowser::Display() {
  PSB_CTX ctx = {
      .cmd = {
          .curr = 0,
          .total = static_cast<int>(entries_.size()),
          .priv = reinterpret_cast<void *>(this),
      },
      .header_lines = 2,
      .footer_lines = 2,
      .allow_pbs_version_message = 1,
      .header = &EntryBrowserHeader,
      .footer = &EntryBrowserFooter,
      .renderer = &EntryBrowserRenderer,
      .on_key = &EntryBrowserOnKey,
  };

  // If empty result, show a line about it.
  // Also PSB does not accept 0 as total.
  if (ctx.cmd.total == 0)
    ctx.cmd.total = 1;

  psb_main(&ctx);
  return 0;
}

int EntryBrowser::Header() {
  vs_draw_hdr2("搜尋認證資料庫", title_.c_str());
  move(1, 0);
  vbar(ANSI_COLOR(30;47)
       "   使用者代號   認證資訊                               認證時間");
  outs(ANSI_RESET);
  return 0;
}

int EntryBrowser::Footer() {
  vs_footer(" 瀏覽資料 ", " (↑↓)移動 (Enter/r/→)使用者資料 "
                          "\t(q/←)跳出");
  move(b_lines - 1, 0);
  return 0;
}

int EntryBrowser::Renderer(int i, int curr) {
  if (curr >= (int)entries_.size()) {
    outs("   查無資料");
    return 0;
  }

  const auto *entry = entries_[i];
  time4_t ts = entry->timestamp();

  outs("   ");
  // clang-format off
  if (i == curr)
    outs(ANSI_COLOR(1;41;37));
  // clang-format on

  auto disp = FormatVerify(entry->vmethod(), entry->vkey());
  prints("%-12s%s %-38s %s", entry->userid()->c_str(), ANSI_RESET,
         disp ? disp->c_str() : "?", ts ? Cdate(&ts) : "(無時間紀錄)");
  return 0;
}

int EntryBrowser::InputProcessor(PSB_CTX *ctx) {
  if (ctx->cmd.curr >= (int)entries_.size())
    return PSB_NA;

  switch (ctx->cmd.key) {
  case KEY_ENTER:
  case KEY_RIGHT:
  case 'r':
    if (user_info_admin(entries_[ctx->cmd.curr]->userid()->c_str()) < 0)
      vmsg("找不到此使用者");
    ctx->cmd.redraw = true;
    return 0;
  }
  return PSB_NA;
}

// static
int EntryBrowser::SearchDisplay(int32_t vmethod, const char *vkey) {
  EntryBrowser browser;
  const VerifyDb::GetReply *reply;
  if (!verifydb_getverify(vmethod, vkey, &browser.buf_, &reply) ||
      !reply->ok() || !reply->entry()) {
    vmsg("認證系統無法使用，請稍候再試。");
    return -1;
  }

  for (const auto *entry : *reply->entry()) {
    if (!is_entry_user_present(entry))
      continue;
    browser.entries_.push_back(entry);
  }

  if (auto title = FormatVerify(vmethod, vkey); title)
    browser.title_ = title.value();

  return browser.Display();
}

int verifydb_admin_search_display() {
  vs_hdr("搜尋認證資料庫");

  int y = 2;
  char email[EMAILSZ] = {};
  getdata_buf(y++, 0, "認證信箱：", email, sizeof(email), DOECHO);
  strip_blank(email, email);
  str_lower(email, email);
  if (!*email)
    return 0;

  return EntryBrowser::SearchDisplay(VMETHOD_EMAIL, email);
}

#endif
