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

 private:
  fileheader_t fhdr_;
};

#endif
