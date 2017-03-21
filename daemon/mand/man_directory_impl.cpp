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

bool ManDirectoryImpl::ListDirectory(const ManPath &path,
                                     std::vector<FileHeader> *entries) {
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
