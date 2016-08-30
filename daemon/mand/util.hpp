#ifndef _MAND_UTIL_HPP_
#define _MAND_UTIL_HPP_
#include <string>
extern "C" {
#include <event2/buffer.h>
}

#define DISABLE_COPY_AND_ASSIGN(Class) \
  Class(const Class &) = delete; \
  Class &operator=(const Class &) = delete

#define RETURN_ON_FAIL(Expr) \
  do { \
    Status s = (Expr); \
    if (!s.ok()) \
      return s; \
  } while (0)

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

class Evbuffer final {
 public:
  Evbuffer();
  ~Evbuffer();

  evbuffer *Get() { return buf_; }
  bool ConvertUTF8();

 private:
  DISABLE_COPY_AND_ASSIGN(Evbuffer);
  evbuffer *buf_;
};

std::string Big5ToUTF8(const char *big5);

bool IsDirectory(const std::string &path);

#endif
