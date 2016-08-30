#ifndef _MAND_MAN_PATH_HPP_
#define _MAND_MAN_PATH_HPP_
#include <string>
#include <vector>

class ManPath final {
 public:
  ManPath(const std::string &board_name, const std::string &path);
  ManPath(const ManPath &base, const std::string &name);
  ManPath(const ManPath &other);
  ManPath &operator=(const ManPath &other);

  bool IsValid() const { return valid_; }
  const std::string &BoardName() const { return board_name_; }
  const std::vector<std::string> &Dirs() const { return dirs_; }

  std::string BBSPath() const;
  std::string PathRelativeToBoard() const;
  std::string FullPath() const;

 private:
  std::string board_name_;
  std::vector<std::string> dirs_;
  bool valid_;
};

#endif
