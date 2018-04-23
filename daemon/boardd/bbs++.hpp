#ifndef _PTTBBS_DAEMON_BOARDD_BBSPP_HPP_
#define _PTTBBS_DAEMON_BOARDD_BBSPP_HPP_
#include <string>
#include <vector>
#include <boost/optional.hpp>
extern "C" {
#include "cmbbs.h"
#include "pttstruct.h"
#include "daemon/boardd/boardd.h"
}

namespace paths {

// Return whether the filename is valid.
bool IsValidFilename(const std::string &filename);

// Get board file.
std::string bfile(const std::string &brdname, const std::string &filename);

}  // namespace paths

namespace records {

// Count records.
template <class T>
size_t Count(const std::string &fn) {
  return get_num_records(fn.c_str(), sizeof(T));
}

// Get records, returning the starting offset.
template <class T>
size_t Get(const std::string &fn, ssize_t offset, ssize_t length,
           std::vector<T> *dst) {
  dst->clear();
  ssize_t total = Count<T>(fn);
  if (total <= 0)
    return 0;
  if (offset < 0)
    offset += total;
  if (offset < 0)
    offset = 0;
  ssize_t start_offset = offset;
  int fd = -1;
  while (length < 0 || length-- > 0) {
    dst->emplace_back();
    if (get_records_keep(fn.c_str(), &dst->back(), sizeof(T), ++offset, 1,
                         &fd) <= 0) {
      dst->pop_back();
      break;
    }
  }
  if (fd >= 0) {
    close(fd);
  }
  return start_offset;
}

}  // namespace records

namespace boards {

using bid_t = int32_t;

// Return whether the board is public.
bool IsPublic(const boardheader_t *bp);

// Get the board header entry in bcache.
boost::optional<boardheader_t *> Get(bid_t bid);

// Resolve the bid by board name.
boost::optional<bid_t> Resolve(const std::string &name);

// Resolve the children of the board. It may modify SHM.
std::vector<bid_t> Children(bid_t bid);

// Search by predicate.
boost::optional<std::string> Search(bid_t bid, const std::string &base_name,
                                    const fileheader_predicate_t &pred);

}  // namespace boards

#endif
