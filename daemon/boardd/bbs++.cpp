#include <regex>
#include <string>
#include <vector>
#include "daemon/boardd/bbs++.hpp"
extern "C" {
#include "var.h"
#include "cmbbs.h"
#include "daemon/boardd/boardd.h"
}

namespace paths {

const std::regex filename_regex{"[MG]\\.\\d+\\.A(?:\\.[0-9A-F]+)?"};

bool IsValidFilename(const std::string &filename) {
  return std::regex_match(filename, filename_regex);
}

std::string bfile(const std::string &brdname, const std::string &filename) {
  char buf[PATH_MAX];
  setbfile(buf, brdname.c_str(), filename.c_str());
  return buf;
}

}  // namespace paths

namespace boards {
namespace {

#define BOARD_HIDDEN(bptr) (bptr->brdattr & (BRD_HIDE | BRD_TOP) || \
    (bptr->level & ~(PERM_BASIC|PERM_CHAT|PERM_PAGE|PERM_POST|PERM_LOGINOK) && \
     !(bptr->brdattr & BRD_POSTMASK)))

bool IsValidBid(bid_t bid) {
  return bid > 0 && bid <= MAX_BOARD;
}

}  // namespace

bool IsPublic(const boardheader_t *bp) {
  return bp->brdname[0] && !BOARD_HIDDEN(bp);
}

boost::optional<boardheader_t *> Get(bid_t bid) {
  if (!IsValidBid(bid)) {
    return {};
  }
  return getbcache(bid);
}

boost::optional<bid_t> Resolve(const std::string &name) {
  bid_t bid = getbnum(name.c_str());
  if (!IsValidBid(bid)) {
    return {};
  }
  return bid;
}

std::vector<bid_t> Children(bid_t bid) {
  auto bp = Get(bid);
  if (!bp) {
    return {};
  }
  std::vector<bid_t> bids;
  if (bp.value()->brdattr & BRD_GROUPBOARD) {
    const int type = BRD_GROUP_LL_TYPE_CLASS;
    if (bp.value()->firstchild[type] == 0) {
      resolve_board_group(bid, type);
    }
    for (bid = bp.value()->firstchild[type]; bid > 0;
         bid = bp.value()->next[type]) {
      bp = Get(bid);
      if (!bp) {
        // Invalid linked-list?
        // TODO: Log error.
        break;
      }
      if (IsPublic(bp.value())) {
        bids.push_back(bid);
      }
    }
  }
  return bids;
}

}  // namespace boards
