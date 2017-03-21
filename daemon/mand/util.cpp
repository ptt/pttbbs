#include "util.hpp"
extern "C" {
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cmbbs.h"
#include "daemon/boardd/boardd.h"
}

File::File(const std::string &path) {
  fd_ = open(path.c_str(), O_RDONLY);
}

File::~File() {
  Close();
}

ssize_t File::Read(void *buf, size_t size) {
  return read(fd_, buf, size);
}

void File::Close() {
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

std::string Big5ToUTF8(const char *big5) {
  std::string utf8;
  const uint8_t *p = reinterpret_cast<const uint8_t *>(big5);
  while (*p) {
    if (isascii(*p))
      utf8.push_back(*p);
    else if (!p[1]) {
      utf8.push_back('?');
      break;
    } else {
      uint8_t cs[4];
      utf8.append(
          reinterpret_cast<const char *>(cs),
          ucs2utf(b2u_table[static_cast<uint16_t>(p[0]) << 8 | p[1]], cs));
      p++;
    }
    p++;
  }
  return utf8;
}

bool IsDirectory(const std::string &path) {
  struct stat st;
  if (stat(path.c_str(), &st) < 0)
    return false;
  return S_ISDIR(st.st_mode);
}
