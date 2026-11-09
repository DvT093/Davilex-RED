#include "engine/dfm.h"

#include <cctype>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>

namespace davi {

std::string Lower(std::string s) {
  for (auto& c : s) c = char(std::tolower((unsigned char)c));
  return s;
}

const Value* Component::Prop(const std::string& k) const {
  auto it = props.find(k);
  return it == props.end() ? nullptr : &it->second;
}
int Component::Int(const std::string& k, int def) const {
  const Value* v = Prop(k);
  return v && v->kind == Value::kInt ? int(v->i) : def;
}
bool Component::Bool(const std::string& k, bool def) const {
  const Value* v = Prop(k);
  return v && v->kind == Value::kBool ? v->i != 0 : def;
}
std::string Component::Str(const std::string& k, const std::string& def) const {
  const Value* v = Prop(k);
  return v && v->kind == Value::kString ? v->s : def;
}
Component* Component::Find(const std::string& n) {
  if (Lower(name) == Lower(n)) return this;
  for (auto& c : children)
    if (Component* f = c->Find(n)) return f;
  return nullptr;
}

namespace {

// Reader over the binary DFM stream (TReader format, "TPF0").
struct Reader {
  const uint8_t* p;
  const uint8_t* end;
  bool ok = true;

  uint8_t U8() {
    if (p >= end) { ok = false; return 0; }
    return *p++;
  }
  template <class T> T Get() {
    T v{};
    if (end - p < (ptrdiff_t)sizeof(T)) { ok = false; p = end; return v; }
    std::memcpy(&v, p, sizeof(T));
    p += sizeof(T);
    return v;
  }
  std::string SStr() {
    size_t n = U8();
    if (size_t(end - p) < n) { ok = false; p = end; return {}; }
    std::string s((const char*)p, n);
    p += n;
    return s;
  }
  std::string LStr(size_t width) {
    uint32_t n = Get<uint32_t>();
    if (size_t(end - p) < size_t(n) * width) { ok = false; p = end; return {}; }
    std::string s;
    if (width == 1) {
      s.assign((const char*)p, n);
    } else {  // UTF-16: keep the low byte (forms here are Windows-1252)
      for (uint32_t i = 0; i < n; i++) s += char(p[2 * i]);
    }
    p += size_t(n) * width;
    return s;
  }

  Value Val() {
    Value v;
    uint8_t t = U8();
    switch (t) {
      case 0: break;  // vaNull
      case 1:         // vaList
        v.kind = Value::kList;
        while (ok && p < end && *p != 0) v.list.push_back(Val());
        U8();
        break;
      case 2: v.kind = Value::kInt; v.i = Get<int8_t>(); break;
      case 3: v.kind = Value::kInt; v.i = Get<int16_t>(); break;
      case 4: v.kind = Value::kInt; v.i = Get<int32_t>(); break;
      case 5: {  // vaExtended (80-bit): keep only an approximation
        uint64_t m = Get<uint64_t>();
        uint16_t se = Get<uint16_t>();
        int e = (se & 0x7fff) - 16383 - 63;
        v.kind = Value::kFloat;
        v.f = (se & 0x8000 ? -1.0 : 1.0) * double(m) * std::ldexp(1.0, e);
        break;
      }
      case 6: case 7: v.kind = Value::kString; v.s = SStr(); break;  // string/ident
      case 8: v.kind = Value::kBool; v.i = 0; break;
      case 9: v.kind = Value::kBool; v.i = 1; break;
      case 10: {  // vaBinary
        uint32_t n = Get<uint32_t>();
        if (size_t(end - p) < n) { ok = false; p = end; break; }
        v.kind = Value::kBytes;
        v.bytes.assign(p, p + n);
        p += n;
        break;
      }
      case 11:  // vaSet
        v.kind = Value::kSet;
        for (;;) {
          std::string s = SStr();
          if (s.empty() || !ok) break;
          v.set.push_back(s);
        }
        break;
      case 12: v.kind = Value::kString; v.s = LStr(1); break;  // vaLString
      case 13: v.kind = Value::kNull; break;                   // vaNil
      case 14:                                                 // vaCollection
        v.kind = Value::kCollection;
        while (ok && p < end && *p != 0) {
          if (*p == 2 || *p == 3 || *p == 4) Val();  // item index
          Value item;
          item.kind = Value::kList;
          for (;;) {
            std::string k = SStr();
            if (k.empty() || !ok) break;
            Value kv = Val();
            kv.s = k + "=" + kv.s;
            item.list.push_back(kv);
          }
          v.list.push_back(item);
        }
        U8();
        break;
      case 15: v.kind = Value::kFloat; v.f = Get<float>(); break;
      case 16: v.kind = Value::kFloat; v.f = double(Get<int64_t>()) / 10000.0; break;  // currency
      case 17: v.kind = Value::kFloat; v.f = Get<double>(); break;                      // date
      case 18: v.kind = Value::kString; v.s = LStr(2); break;                          // widestring
      case 19: v.kind = Value::kInt; v.i = Get<int64_t>(); break;
      case 20: v.kind = Value::kString; v.s = LStr(1); break;  // utf8
      default: ok = false; break;
    }
    return v;
  }

  std::shared_ptr<Component> Comp() {
    if (p < end && (*p & 0xf0) == 0xf0) {  // ffInherited / ffChildPos / ffInline
      uint8_t flags = U8();
      if (flags & 2) Val();
    }
    auto c = std::make_shared<Component>();
    c->cls = SStr();
    c->name = SStr();
    if (c->cls.empty() || !ok) { ok = false; return nullptr; }
    for (;;) {
      std::string k = SStr();
      if (k.empty() || !ok) break;
      c->props[k] = Val();
    }
    while (ok && p < end && *p != 0) {
      auto k = Comp();
      if (!k) { ok = false; return nullptr; }
      c->children.push_back(k);
    }
    U8();
    return ok ? c : nullptr;
  }
};

bool Ident(const std::string& s) {
  if (s.empty() || !(std::isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
  for (char ch : s)
    if (!(std::isalnum((unsigned char)ch) || ch == '_')) return false;
  return true;
}

// Deep copy so merging never touches the cached raw forms.
std::shared_ptr<Component> Clone(const Component& c) {
  auto n = std::make_shared<Component>();
  n->cls = c.cls;
  n->name = c.name;
  n->props = c.props;
  for (auto& k : c.children) n->children.push_back(Clone(*k));
  return n;
}

// Applies the delta form `d` (a descendant's DFM) on top of `base`.
void Merge(Component& base, const Component& d) {
  for (auto& [k, v] : d.props) base.props[k] = v;
  for (auto& dk : d.children) {
    Component* existing = nullptr;
    for (auto& bk : base.children)
      if (Lower(bk->name) == Lower(dk->name)) existing = bk.get();
    if (!existing) existing = base.Find(dk->name);
    if (existing && existing != &base) {
      Merge(*existing, *dk);
    } else {
      base.children.push_back(Clone(*dk));
    }
  }
}

}  // namespace

bool DelphiExe::Load(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  data_.assign(std::istreambuf_iterator<char>(f), {});
  const auto& d = data_;
  if (d.size() < 0x400 || d[0] != 'M' || d[1] != 'Z') return false;

  // Forms: every "TPF0" signature that parses as a component tree.
  for (size_t i = 0; i + 4 < d.size(); i++) {
    if (std::memcmp(&d[i], "TPF0", 4) != 0) continue;
    Reader r{d.data() + i + 4, d.data() + d.size()};
    auto c = r.Comp();
    if (c && r.ok && Ident(c->cls)) forms_[c->cls] = c;
  }

  // Classes: Delphi VMTs, found by their self pointer (vmtSelfPtr = -76).
  auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, &d[o], 4); return v; };
  uint32_t pe = u32(0x3c);
  if (pe + 0x100 > d.size()) return !forms_.empty();
  uint16_t nsec, optsz;
  std::memcpy(&nsec, &d[pe + 6], 2);
  std::memcpy(&optsz, &d[pe + 20], 2);
  uint32_t base = u32(pe + 52);
  struct Sec { uint32_t va, rs, ro; };
  std::vector<Sec> secs;
  for (int i = 0; i < nsec; i++) {
    size_t o = pe + 24 + optsz + 40 * i;
    secs.push_back({u32(o + 12), u32(o + 16), u32(o + 20)});
  }
  auto off = [&](uint32_t va, size_t* out) {
    va -= base;
    for (auto& s : secs)
      if (va >= s.va && va < s.va + s.rs && s.ro + (va - s.va) < d.size()) {
        *out = s.ro + (va - s.va);
        return true;
      }
    return false;
  };
  auto name_at = [&](uint32_t vmt, std::string* out) {
    size_t o;
    if (!off(vmt - 44, &o)) return false;
    if (!off(u32(o), &o)) return false;
    size_t n = d[o];
    if (o + 1 + n > d.size()) return false;
    *out = std::string((const char*)&d[o + 1], n);
    return Ident(*out);
  };
  std::map<uint32_t, std::string> names;
  std::map<uint32_t, uint32_t> parents;
  for (auto& s : secs) {
    for (size_t o = s.ro; o + 4 <= s.ro + s.rs && o + 4 <= d.size(); o += 4) {
      uint32_t va = base + s.va + uint32_t(o - s.ro);
      if (u32(o) != va + 76) continue;
      uint32_t vmt = va + 76;
      std::string n;
      if (!name_at(vmt, &n)) continue;
      names[vmt] = n;
      size_t po;
      if (off(vmt - 36, &po)) {
        uint32_t pp = u32(po);  // pointer to the parent's VMT pointer
        size_t ppo;
        if (pp && off(pp, &ppo)) parents[vmt] = u32(ppo);
      }
    }
  }
  for (auto& [vmt, n] : names) {
    auto it = parents.find(vmt);
    if (it != parents.end() && names.count(it->second)) parent_[n] = names[it->second];
  }
  return !forms_.empty();
}

std::string DelphiExe::ParentClass(const std::string& cls) const {
  auto it = parent_.find(cls);
  return it == parent_.end() ? "" : it->second;
}

const Component* DelphiExe::RawForm(const std::string& cls) const {
  auto it = forms_.find(cls);
  return it == forms_.end() ? nullptr : it->second.get();
}

std::vector<std::string> DelphiExe::FormClasses() const {
  std::vector<std::string> v;
  for (auto& [k, _] : forms_) v.push_back(k);
  return v;
}

std::shared_ptr<Component> DelphiExe::Form(const std::string& cls) const {
  const Component* raw = RawForm(cls);
  if (!raw) return nullptr;
  // Nearest ancestor that has a form of its own.
  for (std::string p = ParentClass(cls); !p.empty(); p = ParentClass(p)) {
    if (RawForm(p)) {
      auto merged = Form(p);
      Merge(*merged, *raw);
      merged->cls = raw->cls;
      merged->name = raw->name;
      return merged;
    }
  }
  return Clone(*raw);
}

std::vector<uint8_t> PictureBytes(const Value& v) {
  if (v.kind != Value::kBytes || v.bytes.empty()) return {};
  size_t n = v.bytes[0];
  if (1 + n + 4 > v.bytes.size()) return {};
  std::string cls((const char*)&v.bytes[1], n);
  size_t o = 1 + n;
  if (cls == "TBitmap") {
    uint32_t sz;
    std::memcpy(&sz, &v.bytes[o], 4);
    o += 4;
    if (o + sz > v.bytes.size()) sz = uint32_t(v.bytes.size() - o);
    return {v.bytes.begin() + o, v.bytes.begin() + o + sz};
  }
  return {};
}

std::vector<uint8_t> SizedBytes(const Value& v) {
  if (v.kind != Value::kBytes || v.bytes.size() < 4) return {};
  // A2 Racer IV's TDaviWave stores the WAV without a length prefix.
  if (!std::memcmp(v.bytes.data(), "RIFF", 4) || !std::memcmp(v.bytes.data(), "BM", 2)) return v.bytes;
  uint32_t sz;
  std::memcpy(&sz, v.bytes.data(), 4);
  if (4 + size_t(sz) > v.bytes.size()) sz = uint32_t(v.bytes.size() - 4);
  return {v.bytes.begin() + 4, v.bytes.begin() + 4 + sz};
}

}  // namespace davi
