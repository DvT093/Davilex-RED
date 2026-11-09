// Delphi form loader: reads the binary form resources (DFM, "TPF0") that a
// Delphi 2-7 program carries, straight from the user's own EXE, plus the
// class hierarchy (VMT parent links) so inherited forms can be merged.
// Generic: nothing here knows about a particular game.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace davi {

// One DFM property value.
struct Value {
  enum Kind { kNull, kInt, kFloat, kString, kBool, kBytes, kList, kSet, kCollection };
  Kind kind = kNull;
  int64_t i = 0;
  double f = 0;
  std::string s;                 // string / identifier (Windows-1252)
  std::vector<uint8_t> bytes;    // binary data (DIBs, fonts, wavs)
  std::vector<Value> list;       // list items
  std::vector<std::string> set;  // set members
};

struct Component {
  std::string cls, name;
  std::map<std::string, Value> props;  // "Font.Height" etc.
  std::vector<std::shared_ptr<Component>> children;

  const Value* Prop(const std::string& k) const;
  int Int(const std::string& k, int def = 0) const;
  bool Bool(const std::string& k, bool def = false) const;
  std::string Str(const std::string& k, const std::string& def = "") const;
  // Finds a descendant (or this) by component name, case-insensitive.
  Component* Find(const std::string& name);
};

// The forms and classes of one Delphi EXE.
class DelphiExe {
 public:
  bool Load(const std::string& path);
  // Root component of a form class (e.g. "TfrmMainScreen"), with the forms it
  // inherits from merged in (base components first, overrides applied).
  std::shared_ptr<Component> Form(const std::string& cls) const;
  // Raw (unmerged) form, or nullptr.
  const Component* RawForm(const std::string& cls) const;
  std::string ParentClass(const std::string& cls) const;
  std::vector<std::string> FormClasses() const;

 private:
  std::vector<uint8_t> data_;
  std::map<std::string, std::shared_ptr<Component>> forms_;  // by class
  std::map<std::string, std::string> parent_;                // class -> parent
};

// Splits "TBitmap"-prefixed TPicture data / size-prefixed blobs.
// Returns the payload of a TPicture.Data value (BMP file bytes for TBitmap).
std::vector<uint8_t> PictureBytes(const Value& v);
// Strips the u32 length prefix Davi components put before embedded files
// (TDaviEmbeddedFont.Data, TDaviWave.WavFileData).
std::vector<uint8_t> SizedBytes(const Value& v);

std::string Lower(std::string s);

}  // namespace davi
