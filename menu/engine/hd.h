// Upgraded ("HD") pictures for the menu: every original picture gets a
// name (a bitmap file "mainscre", a form picture "TfrmMainScreen.imgLogo",
// a button state "defBtn.UpLink"), and an HD directory may hold an
// upscaled PNG with that name. "<name>.png" is the plain upscale,
// "<name>.alpha.png" the same picture with its transparent colour turned
// into real alpha (tools/upscale_menu.py makes both from the user's CD).
// The renderer (paint.h) draws the HD picture in the original's place.
#pragma once

#include <SDL3/SDL.h>

#include <map>
#include <string>
#include <vector>

namespace davi {

class DelphiExe;

// Names an original picture (stored on the surface; freed with it).
void NamePicture(SDL_Surface* s, const std::string& name);
std::string PictureName(SDL_Surface* s);  // "" if unnamed

// `out` was made from parts of other pictures: `src_rect` of `src` drawn
// (scaled) to `dst_rect` of `out`. The HD renderer draws those parts.
struct DerivedPart {
  SDL_Surface* src;
  SDL_Rect src_rect, dst_rect;
};
void DerivePicture(SDL_Surface* out, SDL_Surface* src, const SDL_Rect* src_rect, const SDL_Rect* dst_rect);
const std::vector<DerivedPart>* DerivedParts(SDL_Surface* s);

// HD pictures by name, from a list of directories (first wins).
class HdPictures {
 public:
  void AddDir(const std::string& dir) { dirs_.push_back(dir); }
  // The HD picture for `name` (ARGB8888, owned and cached), the keyed one
  // ("<name>.alpha.png") when `keyed`; nullptr if there is none.
  SDL_Surface* Get(const std::string& name, bool keyed);
  bool Empty() const { return dirs_.empty(); }
  ~HdPictures();

 private:
  std::vector<std::string> dirs_;
  std::map<std::string, SDL_Surface*> cache_;
};

// Writes every picture embedded in the forms of `exe` (images, slider
// tracks, button states) as "<name>.bmp" into `dir`, plus pictures.tsv:
// name, width, height, key (RRGGBB of the transparent colour, "bl" for
// the bottom-left pixel, "edge" for a button background, "-" for none).
// Returns the number of pictures written.
int DumpFormPictures(const DelphiExe& exe, const std::string& dir);

}  // namespace davi
