#ifndef _MAND_FILE_HEADER_HPP_
#define _MAND_FILE_HEADER_HPP_
#include <string>
extern "C" {
#include "pttstruct.h"
}

class FileHeader final {
 public:
  FileHeader(const fileheader_t *fhdr);
  FileHeader(const FileHeader &other);
  FileHeader &operator=(const FileHeader &other);
  ~FileHeader();

  std::string FileName() const { return fhdr_.filename; }
  std::string Title() const;
  bool HasFlag(unsigned char flag) const {
    return (fhdr_.filemode & flag) == flag;
  }
  // Whether it is a directory, when the lister knows (gem.svc).
  bool IsDir() const { return is_dir_; }
  void SetDir(bool is_dir) { is_dir_ = is_dir; }

 private:
  fileheader_t fhdr_;
  bool is_dir_ = false;
};

#endif
