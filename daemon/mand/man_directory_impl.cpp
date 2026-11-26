#include <vector>
#include <string>
#include "file_header.hpp"
#include "man_directory_impl.hpp"
#include "man_path.hpp"
#include "util.hpp"
extern "C" {
#include "bbs.h"
#include "cmbbs.h"
#include "daemon/boardd/boardd.h"
#include "gem.h"
}

#define BOARD_HIDDEN(bptr) (bptr->brdattr & (BRD_HIDE | BRD_TOP) || \
    (bptr->level & ~(PERM_BASIC|PERM_CHAT|PERM_PAGE|PERM_POST|PERM_LOGINOK) && \
     !(bptr->brdattr & BRD_POSTMASK)))

namespace {

class ManDirectoryImpl final : public ManDirectory {
 public:
  ManDirectoryImpl() {}
  ~ManDirectoryImpl() {}

  bool HasPermission(const ManPath &path);
  bool HasPermission(const FileHeader &fhdr);
  bool ListDirectory(const ManPath &path, std::vector<FileHeader> *entries);

 private:
  DISABLE_COPY_AND_ASSIGN(ManDirectoryImpl);

  bool HasPermission(const ManPath &path, const std::string &name);
  bool HasBoardPermission(const std::string &board_name);
};

bool ManDirectoryImpl::HasPermission(const ManPath &path) {
  if (!HasBoardPermission(path.BoardName()))
    return false;
  ManPath p(path.BoardName(), "");
  for (const auto &dir : path.Dirs()) {
    if (!HasPermission(p, dir))
      return false;
    p = ManPath(p, dir);
  }
  return true;
}

bool ManDirectoryImpl::HasPermission(const FileHeader &fhdr) {
  return !fhdr.HasFlag(FILE_HIDE) && !fhdr.HasFlag(FILE_BM);
}

// The gem.svc root and sub path of a board's gem area directory.
static void GemDir(const ManPath &path, std::string *root, std::string *sub) {
  *root = "board:" + path.BoardName();
  sub->clear();
  for (const auto &d : path.Dirs()) {
    if (!sub->empty())
      *sub += "/";
    *sub += d;
  }
}

static bool GemList(const ManPath &path, std::vector<FileHeader> *entries) {
  std::string root, sub;
  GemDir(path, &root, &sub);
  int count = 0;
  if (gem_list(root.c_str(), sub.c_str(), 0, 0, nullptr, &count, nullptr) < 0)
    return false;
  std::vector<gem_entry_t> ents(count);
  int n = count ? gem_list(root.c_str(), sub.c_str(), 0, count, ents.data(),
                           nullptr, nullptr) : 0;
  if (n < 0)
    return false;
  for (int i = 0; i < n; i++) {
    entries->emplace_back(&ents[i].header);
    entries->back().SetDir(ents[i].kind == GEM_KIND_DIR);
  }
  return true;
}

bool ManDirectoryImpl::ListDirectory(const ManPath &path,
                                     std::vector<FileHeader> *entries) {
  if (IS_ENABLED(CONFIG_GEM))
    return GemList(path, entries);
  File f(path.FullPath() + "/.DIR");
  // Files with zero size can be deleted. Return an empty result instead of an
  // error when the directory exists.
  if (!f)
    return IsDirectory(path.FullPath());
  fileheader_t fhdr;
  ssize_t r;
  while ((r = f.Read(&fhdr, sizeof(fhdr))) > 0) {
    entries->emplace_back(&fhdr);
  }
  return r == 0;
}

bool ManDirectoryImpl::HasPermission(const ManPath &path,
                                     const std::string &name) {
  if (IS_ENABLED(CONFIG_GEM)) {
    std::string root, sub;
    GemDir(path, &root, &sub);
    gem_entry_t e;
    if (gem_entry(root.c_str(), sub.c_str(), -1, name.c_str(), &e, nullptr,
                  nullptr, 0) < 0)
      return false;
    return HasPermission(FileHeader(&e.header));
  }
  File f(path.FullPath() + "/.DIR");
  if (!f)
    return false;
  fileheader_t fhdr;
  ssize_t r;
  while ((r = f.Read(&fhdr, sizeof(fhdr))) > 0) {
    FileHeader h(&fhdr);
    if (name == h.FileName())
      return HasPermission(h);
  }
  return false;
}

bool ManDirectoryImpl::HasBoardPermission(const std::string &board_name) {
  int bid = getbnum(board_name.c_str());
  if (bid < 1 || bid >= MAX_BOARD)
    return false;
  boardheader_t *bptr = getbcache(bid);
  return bptr->brdname[0] && !BOARD_HIDDEN(bptr);
}

}  // namespace

std::unique_ptr<ManDirectory> CreateManDirectoryImpl() {
  return std::unique_ptr<ManDirectory>(new ManDirectoryImpl());
}
