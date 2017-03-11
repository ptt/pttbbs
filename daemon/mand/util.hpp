#ifndef _MAND_UTIL_HPP_
#define _MAND_UTIL_HPP_
#include <string>
#include "daemon/boardd/macros.hpp"

class File final {
 public:
  File(const std::string &path);
  ~File();

  ssize_t Read(void *buf, size_t size);
  void Close();

  operator bool() const { return IsValid(); }
  bool IsValid() const { return fd_ >= 0; }

 private:
  DISABLE_COPY_AND_ASSIGN(File);

  int fd_;
};

std::string Big5ToUTF8(const char *big5);

bool IsDirectory(const std::string &path);

#endif
