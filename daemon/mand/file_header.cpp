#include <string>
#include "file_header.hpp"
#include "util.hpp"

FileHeader::FileHeader(const fileheader_t *fhdr) : fhdr_(*fhdr) {}

FileHeader::FileHeader(const FileHeader &other)
    : fhdr_(other.fhdr_), is_dir_(other.is_dir_) {}

FileHeader::~FileHeader() {}

FileHeader &FileHeader::operator=(const FileHeader &other) {
  fhdr_ = other.fhdr_;
  is_dir_ = other.is_dir_;
  return *this;
}

std::string FileHeader::Title() const {
  return Big5ToUTF8(fhdr_.title);
}
