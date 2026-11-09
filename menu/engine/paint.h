// Draws the menu with the GPU renderer at the window's real pixel size,
// keeping the original 800x600 layout: every widget is drawn on its own,
// scaled by one factor, instead of one 800x600 picture blown up. Pictures
// come from their HD version when there is one (hd.h), text is rendered at
// the scaled size. In "original" mode it draws at 800x600 into a texture
// that is then scaled up as a whole, which is how the menu always looked.
#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "engine/hd.h"
#include "engine/text.h"

namespace davi {

class Painter {
 public:
  ~Painter();
  void Init(SDL_Renderer* ren, int logical_w, int logical_h, HdPictures* hd);
  void Shutdown();

  // Starts a frame on the current render target of `out_w` x `out_h`
  // pixels: upgraded = native resolution + HD pictures, else 800x600.
  void Begin(int out_w, int out_h, bool upgraded);
  // Finishes the frame (the 800x600 texture to the output in original mode).
  void End();

  // Logical (800x600) -> output pixels.
  float Scale() const { return scale_; }
  float X(float x) const { return off_x_ + x * scale_; }
  float Y(float y) const { return off_y_ + y * scale_; }
  // Output pixels -> logical (for the mouse).
  void ToLogical(float px, float py, float* x, float* y) const;
  SDL_Rect Viewport() const;  // the 800x600 area in output pixels

  // Drawing, all in logical coordinates.
  void SetClip(const SDL_Rect* r);
  void Fill(const SDL_Rect& r, SDL_Color c);
  // A filled rectangle with round corners of `radius` (logical pixels),
  // anti-aliased over one output pixel; drawn at the output's resolution.
  void RoundRect(const SDL_Rect& r, float radius, SDL_Color c);
  // `src` (nullptr = whole) of picture `s` into `dst`. key: -1 none, -2 the
  // picture's own colour key (bottom-left pixel if it has none), else
  // 0xRRGGBB, -3 the HD picture's alpha when upgraded (button states:
  // their background), none otherwise. `dynamic`: the pixels change (owned canvases), upload again.
  void Image(SDL_Surface* s, const SDL_Rect* src, const SDL_Rect& dst, int key = -1, bool dynamic = false);
  // Opacity of the next Image calls (TGDI Translucent), 255 = opaque.
  void SetAlpha(Uint8 a) { alpha_ = a; }
  void Text(const SDL_Rect& r, const std::string& s, const TextStyle& st);

  // The 800x600 surface handed to Screen::PaintOver: what the screen draws
  // there with SDL calls (video frames) is drawn scaled; what it draws
  // with davi::Blit / davi::DrawText is recorded and drawn at full
  // resolution on top.
  SDL_Surface* OverlayBegin(const SDL_Rect& area);
  void OverlayEnd();
  bool IsOverlay(SDL_Surface* s) const { return s && s == overlay_ && in_overlay_; }
  void Record(std::function<void()> fn) { recorded_.push_back(std::move(fn)); }

  // A message in the top-left corner of the window (output pixels).
  void Notice(const std::string& s, Font* font);

  static Painter* Current() { return current_; }
  bool Upgraded() const { return upgraded_; }

 private:
  struct Tex {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;  // texture size
    bool hd = false;
    Uint64 used = 0;
  };
  Tex* Texture(SDL_Surface* s, int key, bool dynamic);
  void ImageF(SDL_Surface* s, const SDL_FRect& src, const SDL_FRect& dst, int key, bool dynamic);
  SDL_FRect Pixels(const SDL_Rect& r) const;  // rounded edges
  SDL_FRect PixelsF(const SDL_FRect& r) const;
  static void Forget(void* painter, void* surface);

  SDL_Renderer* ren_ = nullptr;
  HdPictures* hd_ = nullptr;
  int lw_ = 800, lh_ = 600;
  int out_w_ = 0, out_h_ = 0;
  bool upgraded_ = true;
  Uint8 alpha_ = 255;
  float scale_ = 1, off_x_ = 0, off_y_ = 0;  // drawing now
  float view_s_ = 1, view_x_ = 0, view_y_ = 0;  // the 800x600 frame in the output
  SDL_Texture* target_ = nullptr;  // original mode: the 800x600 frame
  SDL_Texture* prev_target_ = nullptr;
  SDL_Rect clip_{0, 0, 0, 0};
  bool clipped_ = false;
  Uint64 frame_ = 0;
  // Pictures: (surface, key, hd) -> texture.
  std::map<std::tuple<SDL_Surface*, int, bool>, Tex> tex_;
  std::map<std::string, bool> warned_;
  // Text lines: (font, size 26.6, colour, text) -> texture.
  struct Line {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0, ox = 0, base = 0;
    Uint64 used = 0;
  };
  std::map<std::tuple<Font*, long, Uint32, std::string>, Line> lines_;
  // PaintOver overlay.
  SDL_Surface* overlay_ = nullptr;
  SDL_Texture* overlay_tex_ = nullptr;
  SDL_Rect overlay_area_{0, 0, 0, 0};
  bool in_overlay_ = false;
  std::vector<std::function<void()>> recorded_;
  static Painter* current_;
};

// For Screen::PaintOver: draws `s` (part `src`) onto `canvas` at `dst`
// (canvas pixels = logical coordinates). On the PaintOver canvas this is
// drawn at full resolution with the picture's HD version; elsewhere it is
// SDL_BlitSurface. Honours the picture's colour key.
void Blit(SDL_Surface* canvas, SDL_Surface* s, const SDL_Rect* src, const SDL_Rect& dst);

}  // namespace davi
