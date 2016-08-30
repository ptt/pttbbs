#include <string>
#include <vector>
#include <algorithm>
#include "man_path.hpp"

namespace {
std::vector<std::string> Split(std::string s, char c) {
  std::vector<std::string> parts;
  std::string::size_type prev = 0, next;
  while ((next = s.find(c, prev)) != std::string::npos) {
    parts.emplace_back(s.substr(prev, next - prev));
    prev = next + 1;
  }
  parts.emplace_back(s.substr(prev));
  return parts;
}

std::string Join(const std::vector<std::string> &parts, char sep) {
  std::string s;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0)
      s.push_back(sep);
    s += parts[i];
  }
  return s;
}

void RemoveEmptyComponents(std::vector<std::string> &comps) {
  comps.erase(std::remove_if(comps.begin(), comps.end(),
                             [](const std::string &s) { return s.empty(); }),
              comps.end());
}

bool IsPathValid(const std::string &board_name,
                 const std::vector<std::string> &dirs) {
  if (board_name.empty())
    return false;
  for (size_t i = 0; i < dirs.size(); ++i) {
    const auto &name = dirs[i];
    if (name.empty())
      return false;
    for (const auto &c : name)
      if (!isalnum(c) && c != '.')
        return false;
    switch (name.front()) {
      case 'D':
        break;
      case 'M':
        if (i != dirs.size() - 1)
          return false;
        break;
      default:
        return false;
    }
  }
  return true;
}
}  // namespace

ManPath::ManPath(const std::string &board_name, const std::string &path)
    : board_name_(board_name), dirs_(Split(path, '/')) {
  RemoveEmptyComponents(dirs_);
  valid_ = IsPathValid(board_name_, dirs_);
}

ManPath::ManPath(const ManPath &base, const std::string &name)
    : board_name_(base.board_name_), dirs_(base.dirs_), valid_(base.valid_) {
  dirs_.emplace_back(name);
}

ManPath::ManPath(const ManPath &other)
    : board_name_(other.board_name_),
      dirs_(other.dirs_),
      valid_(other.valid_) {}

ManPath &ManPath::operator=(const ManPath &other) {
  board_name_ = other.board_name_;
  dirs_ = other.dirs_;
  valid_ = other.valid_;
  return *this;
}

std::string ManPath::PathRelativeToBoard() const {
  return Join(dirs_, '/');
}

std::string ManPath::FullPath() const {
  std::string p(BBSHOME);
  p += "/man/boards/";
  p.push_back(board_name_[0]);
  p.push_back('/');
  p += board_name_;
  if (!dirs_.empty()) {
    p.push_back('/');
    p += PathRelativeToBoard();
  }
  return p;
}
