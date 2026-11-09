#include "engine/ini.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "engine/dfm.h"  // Lower

namespace davi {

std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

std::vector<std::string> Split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) {
      out.push_back(Trim(cur));
      cur.clear();
    } else {
      cur += c;
    }
  }
  out.push_back(Trim(cur));
  return out;
}

bool IniFile::Load(const std::string& path) {
  lines_.clear();
  loaded_ = false;
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::string text((std::istreambuf_iterator<char>(f)), {});
  std::string sec;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t nl = text.find('\n', pos);
    std::string l = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = nl == std::string::npos ? text.size() : nl + 1;
    if (!l.empty() && l.back() == '\r') l.pop_back();
    Line line{l, sec, "", ""};
    std::string t = Trim(l);
    if (!t.empty() && t[0] == '[') {
      size_t e = t.find(']');
      sec = Lower(Trim(t.substr(1, e == std::string::npos ? std::string::npos : e - 1)));
      line.section = sec;
    } else if (!t.empty() && t[0] != ';') {
      size_t eq = t.find('=');
      if (eq != std::string::npos) {
        line.key = Lower(Trim(t.substr(0, eq)));
        line.value = Trim(t.substr(eq + 1));
      }
    }
    lines_.push_back(line);
  }
  loaded_ = true;
  return true;
}

bool IniFile::Save(const std::string& path) const {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  for (auto& l : lines_) f << l.text << "\r\n";
  return bool(f);
}

int IniFile::Find(const std::string& sec, const std::string& key) const {
  std::string s = Lower(sec), k = Lower(key);
  for (size_t i = 0; i < lines_.size(); i++)
    if (lines_[i].section == s && lines_[i].key == k) return int(i);
  return -1;
}

bool IniFile::Has(const std::string& sec, const std::string& key) const { return Find(sec, key) >= 0; }

bool IniFile::HasSection(const std::string& sec) const {
  std::string s = Lower(sec);
  for (auto& l : lines_)
    if (l.section == s) return true;
  return false;
}

std::string IniFile::Get(const std::string& sec, const std::string& key, const std::string& def) const {
  int i = Find(sec, key);
  return i < 0 ? def : lines_[i].value;
}

int IniFile::GetInt(const std::string& sec, const std::string& key, int def) const {
  int i = Find(sec, key);
  if (i < 0) return def;
  const std::string& v = lines_[i].value;
  char* end;
  long n = std::strtol(v.c_str(), &end, 0);  // GetPrivateProfileInt: leading digits
  return end == v.c_str() ? def : int(n);
}

void IniFile::Set(const std::string& sec, const std::string& key, const std::string& value) {
  loaded_ = true;
  int i = Find(sec, key);
  std::string s = Lower(sec);
  if (i >= 0) {
    // Keep the original spelling of the key.
    std::string t = lines_[i].text;
    size_t eq = t.find('=');
    lines_[i].text = t.substr(0, eq + 1) + value;
    lines_[i].value = value;
    return;
  }
  // Append after the last line of the section (or a new section at the end).
  int last = -1;
  for (size_t j = 0; j < lines_.size(); j++)
    if (lines_[j].section == s) last = int(j);
  if (last < 0) {
    if (!lines_.empty() && !Trim(lines_.back().text).empty()) lines_.push_back({"", lines_.back().section, "", ""});
    lines_.push_back({"[" + sec + "]", s, "", ""});
    last = int(lines_.size()) - 1;
  } else {
    // Insert before trailing blank lines of the section.
    while (last > 0 && lines_[last].key.empty() && Trim(lines_[last].text).empty() &&
           lines_[last - 1].section == s)
      last--;
  }
  lines_.insert(lines_.begin() + last + 1, Line{key + "=" + value, s, Lower(key), value});
}

void IniFile::SetInt(const std::string& sec, const std::string& key, int value) {
  Set(sec, key, std::to_string(value));
}

std::vector<std::string> IniFile::Keys(const std::string& sec) const {
  std::vector<std::string> v;
  std::string s = Lower(sec);
  for (auto& l : lines_)
    if (l.section == s && !l.key.empty()) v.push_back(Trim(l.text.substr(0, l.text.find('='))));  // as written
  return v;
}

std::vector<std::string> IniFile::Sections() const {
  std::vector<std::string> v;
  for (auto& l : lines_)
    if (std::find(v.begin(), v.end(), l.section) == v.end() && !l.section.empty()) v.push_back(l.section);
  return v;
}

void LayeredIni::AddLayer(const std::string& path) {
  IniFile f;
  if (f.Load(path)) layers_.push_back(std::move(f));
}

std::string LayeredIni::Get(const std::string& sec, const std::string& key, const std::string& def) const {
  for (auto it = layers_.rbegin(); it != layers_.rend(); ++it)
    if (it->Has(sec, key)) return it->Get(sec, key);
  return def;
}

int LayeredIni::GetInt(const std::string& sec, const std::string& key, int def) const {
  for (auto it = layers_.rbegin(); it != layers_.rend(); ++it)
    if (it->Has(sec, key)) return it->GetInt(sec, key, def);
  return def;
}

bool LayeredIni::Has(const std::string& sec, const std::string& key) const {
  for (auto& l : layers_)
    if (l.Has(sec, key)) return true;
  return false;
}

bool LayeredIni::HasSection(const std::string& sec) const {
  for (auto& l : layers_)
    if (l.HasSection(sec)) return true;
  return false;
}

std::vector<std::string> LayeredIni::Keys(const std::string& sec) const {
  std::vector<std::string> v;
  for (auto& l : layers_)
    for (auto& k : l.Keys(sec))
      if (std::find_if(v.begin(), v.end(), [&](const std::string& o) { return Lower(o) == Lower(k); }) == v.end())
        v.push_back(k);
  return v;
}

}  // namespace davi
