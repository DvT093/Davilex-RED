// Windows-style .ini files (GetPrivateProfileString semantics): sections and
// keys are case-insensitive, values are raw Windows-1252 text. Writing keeps
// every other line (comments, order) as it was, like WritePrivateProfileString.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace davi {

class IniFile {
 public:
  bool Load(const std::string& path);  // false if missing (file stays empty)
  bool Save(const std::string& path) const;
  bool Loaded() const { return loaded_; }

  bool Has(const std::string& sec, const std::string& key) const;
  bool HasSection(const std::string& sec) const;
  std::string Get(const std::string& sec, const std::string& key, const std::string& def = "") const;
  int GetInt(const std::string& sec, const std::string& key, int def = 0) const;
  void Set(const std::string& sec, const std::string& key, const std::string& value);
  void SetInt(const std::string& sec, const std::string& key, int value);
  // Keys of a section in file order.
  std::vector<std::string> Keys(const std::string& sec) const;
  std::vector<std::string> Sections() const;

 private:
  struct Line {
    std::string text;     // original line (without newline)
    std::string section;  // lower-case section the line belongs to
    std::string key;      // lower-case key, empty if not a key line
    std::string value;
  };
  std::vector<Line> lines_;
  bool loaded_ = false;
  int Find(const std::string& sec, const std::string& key) const;
};

// An ini file from the game plus optional override files (mods). A key in a
// later layer wins; sections and keys may be added by any layer.
class LayeredIni {
 public:
  void AddLayer(const std::string& path);  // missing files are skipped
  std::string Get(const std::string& sec, const std::string& key, const std::string& def = "") const;
  int GetInt(const std::string& sec, const std::string& key, int def = 0) const;
  bool Has(const std::string& sec, const std::string& key) const;
  bool HasSection(const std::string& sec) const;
  std::vector<std::string> Keys(const std::string& sec) const;

 private:
  std::vector<IniFile> layers_;
};

std::string Trim(const std::string& s);
std::vector<std::string> Split(const std::string& s, char sep);

}  // namespace davi
