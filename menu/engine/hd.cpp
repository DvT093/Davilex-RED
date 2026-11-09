#include "engine/hd.h"

#include <sys/stat.h>

#include <cstdio>
#include <functional>
#include <set>

#include "engine/assets.h"
#include "engine/dfm.h"

namespace davi {

namespace {

const char* kNameProp = "davi.picture.name";
const char* kPartsProp = "davi.picture.parts";

void FreeParts(void*, void* value) { delete static_cast<std::vector<DerivedPart>*>(value); }

}  // namespace

void NamePicture(SDL_Surface* s, const std::string& name) {
  if (s) SDL_SetStringProperty(SDL_GetSurfaceProperties(s), kNameProp, name.c_str());
}

std::string PictureName(SDL_Surface* s) {
  if (!s) return "";
  const char* n = SDL_GetStringProperty(SDL_GetSurfaceProperties(s), kNameProp, nullptr);
  return n ? n : "";
}

void DerivePicture(SDL_Surface* out, SDL_Surface* src, const SDL_Rect* src_rect, const SDL_Rect* dst_rect) {
  if (!out || !src) return;
  SDL_PropertiesID p = SDL_GetSurfaceProperties(out);
  auto* parts = static_cast<std::vector<DerivedPart>*>(SDL_GetPointerProperty(p, kPartsProp, nullptr));
  if (!parts) {
    parts = new std::vector<DerivedPart>;
    SDL_SetPointerPropertyWithCleanup(p, kPartsProp, parts, FreeParts, nullptr);
  }
  parts->push_back({src, src_rect ? *src_rect : SDL_Rect{0, 0, src->w, src->h},
                    dst_rect ? *dst_rect : SDL_Rect{0, 0, out->w, out->h}});
}

const std::vector<DerivedPart>* DerivedParts(SDL_Surface* s) {
  if (!s) return nullptr;
  return static_cast<std::vector<DerivedPart>*>(SDL_GetPointerProperty(SDL_GetSurfaceProperties(s), kPartsProp, nullptr));
}

HdPictures::~HdPictures() {
  for (auto& [_, s] : cache_)
    if (s) SDL_DestroySurface(s);
}

SDL_Surface* HdPictures::Get(const std::string& name, bool keyed) {
  if (name.empty() || dirs_.empty()) return nullptr;
  std::string file = name + (keyed ? ".alpha.png" : ".png");
  std::string key = Lower(file);
  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;
  SDL_Surface* out = nullptr;
  for (auto& d : dirs_) {
    std::string p = FindNoCase(d, file);
    if (p.empty()) continue;
    if (SDL_Surface* s = SDL_LoadPNG(p.c_str())) {
      out = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
      SDL_DestroySurface(s);
    }
    if (out) break;
  }
  cache_[key] = out;
  return out;
}

int DumpFormPictures(const DelphiExe& exe, const std::string& dir) {
  for (size_t i = 1; i <= dir.size(); i++)
    if (i == dir.size() || dir[i] == '/') mkdir(dir.substr(0, i).c_str(), 0755);
  FILE* tsv = std::fopen((dir + "/pictures.tsv").c_str(), "w");
  if (!tsv) return 0;
  std::fprintf(tsv, "# name\twidth\theight\tkey\n");
  std::set<std::string> done;
  int n = 0;
  auto write = [&](const std::string& name, const std::vector<uint8_t>& bmp, const std::string& key) {
    if (bmp.size() < 54 || !done.insert(Lower(name)).second) return;
    SDL_Surface* s = LoadBmp(bmp);
    if (!s) return;
    std::string path = dir + "/" + name + ".bmp";
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
      std::fwrite(bmp.data(), 1, bmp.size(), f);
      std::fclose(f);
      std::fprintf(tsv, "%s\t%d\t%d\t%s\n", name.c_str(), s->w, s->h, key.c_str());
      n++;
    }
    SDL_DestroySurface(s);
  };
  static const char* links[] = {"UpLink", "DownLink", "FocusedLink", "HoveredLink", "DisabledLink"};
  for (const std::string& cls : exe.FormClasses()) {
    auto root = exe.Form(cls);
    if (!root) continue;
    std::function<void(const Component&)> walk = [&](const Component& c) {
      if (c.cls == "TImage" || c.cls == "TDaviImage" || c.cls == "TRacerAnimatedImage") {
        std::vector<uint8_t> bmp;
        if (const Value* p = c.Prop("Picture.Data")) bmp = PictureBytes(*p);
        if (const Value* p = c.Prop("LinkedDIB.DIB"); p && bmp.empty()) bmp = p->bytes;
        std::string key = "-";
        if (c.Bool("Transparent")) key = "bl";
        if (const Value* tc = c.Prop("TransparentColor")) {
          // Same colours as form.cpp DelphiColor (clFuchsia is the default).
          static const std::map<std::string, long> named = {
              {"clBlack", 0x000000}, {"clWhite", 0xFFFFFF}, {"clLime", 0x00FF00}, {"clYellow", 0x00FFFF},
              {"clBlue", 0xFF0000},  {"clRed", 0x0000FF},   {"clFuchsia", 0xFF00FF}};
          long v = 0xFF00FF;
          if (tc->kind == Value::kInt) v = long(tc->i);
          else if (auto it = named.find(tc->s); it != named.end()) v = it->second;
          char buf[8];
          std::snprintf(buf, sizeof buf, "%02lx%02lx%02lx", v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff);
          key = buf;
        }
        write(cls + "." + c.name, bmp, key);
      } else if (c.cls == "TDaviGraphicSlider") {
        if (const Value* p = c.Prop("BackImage.Data")) write(cls + "." + c.name + ".BackImage", SizedBytes(*p), "-");
      } else if (c.cls == "TDaviGraphicDef") {
        for (const char* l : links)
          if (const Value* v = c.Prop(std::string(l) + ".DIB"); v && v->kind == Value::kBytes)
            write(c.name + "." + l, v->bytes, "edge");
      }
      for (auto& k : c.children) walk(*k);
    };
    walk(*root);
  }
  std::fclose(tsv);
  return n;
}

}  // namespace davi
