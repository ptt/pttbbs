#ifndef _MAND_MAN_HPP_
#define _MAND_MAN_HPP_
#include <vector>
#include "man_path.hpp"
#include "file_header.hpp"

class ManDirectory {
 public:
  virtual ~ManDirectory() {}
  virtual bool HasPermission(const ManPath &path) = 0;
  virtual bool HasPermission(const FileHeader &fhdr) = 0;
  virtual bool ListDirectory(const ManPath &path,
                             std::vector<FileHeader> *entries) = 0;
};

#endif
