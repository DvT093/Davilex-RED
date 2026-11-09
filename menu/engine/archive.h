// Davilex RCS archives (<name>.IND + <name>.IMG, a .CHK beside them): the
// RED engine's resource packs, also used by the Europe Racer menu
// (Graphics.IMG, Music.IMG, Langdata/<lang>/menu/Voices.IMG).
//
// .IND: u16 count, then per entry 24 bytes: name[20] (zero padded), u32
// offset into .IMG. An item in the .IMG is either raw (size = next offset -
// offset) or compressed: u32 unpacked size + a zlib stream. Read() tries the
// zlib form first and falls back to raw.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace davi {

class RcsArchive {
 public:
  // `base` without extension; the .IND/.IMG are found case-insensitively.
  bool Open(const std::string& dir, const std::string& base);
  bool Has(const std::string& name) const;
  // Item bytes, empty if absent. `name` is matched case-insensitively.
  std::vector<uint8_t> Read(const std::string& name) const;
  std::vector<std::string> Names() const;
  const std::string& Path() const { return img_path_; }

 private:
  struct Entry {
    uint32_t off = 0, end = 0;
    std::string name;
  };
  std::map<std::string, Entry> by_name_;  // lower-case name
  std::string img_path_;
};

}  // namespace davi
