#include <regex>
#include <string>
#include <vector>
#include "daemon/boardd/bbs++.hpp"
extern "C" {
#include <string.h>
#include "var.h"
#include "cmbbs.h"
#include "pttstruct.h"
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

boost::optional<std::string> Search(bid_t bid, const std::string &base_name,
                                    const fileheader_predicate_t &pred) {
  auto bp = Get(bid);
  if (!bp) {
    return {};
  }

  char genbuf[PATHLEN];
  bool first_select = strncmp(base_name.c_str(), "SR.", 3) != 0;
  select_read_name(genbuf, sizeof(genbuf),
                   first_select ? nullptr : base_name.c_str(), &pred);

  const size_t kMaxLen = 64; // sizeof(currdirect)
  std::string src_direct = paths::bfile(bp.value()->brdname, base_name);
  std::string dst_direct = paths::bfile(bp.value()->brdname, genbuf);
  if (dst_direct.size() >= kMaxLen) {
    return {};
  }

  int force_full_build = pred.mode & (RS_MARK | RS_RECOMMEND | RS_SOLVED);
  time4_t resume_from;
  int count;
  if (select_read_should_build(dst_direct.c_str(), bid, &resume_from, &count) &&
      (count = select_read_build(
           src_direct.c_str(), dst_direct.c_str(), !first_select,
           force_full_build ? 0 : resume_from, count,
           match_fileheader_predicate, (void *)&pred)) < 0) {
    return {};
  }
  return std::string(genbuf);
}

}  // namespace boards
