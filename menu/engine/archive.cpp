#include "engine/archive.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <fstream>

#include "engine/assets.h"  // ReadFile, FindNoCase
#include "engine/dfm.h"     // Lower

namespace davi {

bool RcsArchive::Open(const std::string& dir, const std::string& base) {
  by_name_.clear();
  std::string ind_path = FindNoCase(dir, base + ".IND");
  img_path_ = FindNoCase(dir, base + ".IMG");
  if (ind_path.empty() || img_path_.empty()) return false;
  std::vector<uint8_t> ind = ReadFile(ind_path);
  if (ind.size() < 2) return false;
  size_t n = ind[0] | (ind[1] << 8);
  if (ind.size() < 2 + 24 * n) return false;
  std::ifstream img(img_path_, std::ios::binary | std::ios::ate);
  uint32_t img_size = uint32_t(img.tellg());
  std::vector<Entry> list;
  for (size_t i = 0; i < n; i++) {
    const uint8_t* e = ind.data() + 2 + 24 * i;
    Entry en;
    en.name.assign(reinterpret_cast<const char*>(e), strnlen(reinterpret_cast<const char*>(e), 20));
    std::memcpy(&en.off, e + 20, 4);
    list.push_back(en);
  }
  // An item ends where the next one (by offset) starts.
  std::vector<uint32_t> offs;
  for (auto& e : list) offs.push_back(e.off);
  std::sort(offs.begin(), offs.end());
  for (auto& e : list) {
    auto it = std::upper_bound(offs.begin(), offs.end(), e.off);
    e.end = it == offs.end() ? img_size : *it;
    by_name_[Lower(e.name)] = e;
  }
  return true;
}

bool RcsArchive::Has(const std::string& name) const { return by_name_.count(Lower(name)) != 0; }

std::vector<std::string> RcsArchive::Names() const {
  std::vector<std::string> v;
  for (auto& [_, e] : by_name_) v.push_back(e.name);
  return v;
}

std::vector<uint8_t> RcsArchive::Read(const std::string& name) const {
  auto it = by_name_.find(Lower(name));
  if (it == by_name_.end() || it->second.end <= it->second.off) return {};
  const Entry& e = it->second;
  std::ifstream f(img_path_, std::ios::binary);
  std::vector<uint8_t> raw(e.end - e.off);
  f.seekg(e.off);
  f.read(reinterpret_cast<char*>(raw.data()), raw.size());
  raw.resize(size_t(f.gcount()));
  // Compressed form: u32 unpacked size, then zlib (78 xx header).
  if (raw.size() > 6 && raw[4] == 0x78) {
    uint32_t want;
    std::memcpy(&want, raw.data(), 4);
    if (want > 0 && want < (256u << 20)) {
      std::vector<uint8_t> out(want);
      uLongf got = want;
      if (uncompress(out.data(), &got, raw.data() + 4, uLong(raw.size() - 4)) == Z_OK && got == want) return out;
    }
  }
  return raw;
}

}  // namespace davi
