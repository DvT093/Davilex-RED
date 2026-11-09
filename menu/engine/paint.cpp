#include "engine/paint.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace davi {

Painter* Painter::current_ = nullptr;

namespace {

Painter* g_instance = nullptr;  // for the surface cleanup callbacks
const char* kWatchProp = "davi.painter.watch";

Uint32 Rgb(SDL_Color c) { return (Uint32(c.r) << 16) | (Uint32(c.g) << 8) | c.b; }

// Gives fully transparent pixels the colour of their opaque neighbours, so
// linear filtering does not pull the key colour (magenta) into the edges.
void Bleed(SDL_Surface* s, int passes) {
  const int w = s->w, h = s->h;
  std::vector<uint8_t> known(size_t(w) * h);
  auto px = [&](int x, int y) -> Uint32& { return ((Uint32*)((Uint8*)s->pixels + y * s->pitch))[x]; };
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) known[size_t(y) * w + x] = (px(x, y) >> 24) != 0;
  for (int p = 0; p < passes; p++) {
    std::vector<uint8_t> next = known;
    bool any = false;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        if (known[size_t(y) * w + x]) continue;
        unsigned r = 0, g = 0, b = 0, n = 0;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= w || yy >= h || !known[size_t(yy) * w + xx]) continue;
            Uint32 c = px(xx, yy);
            r += (c >> 16) & 0xff, g += (c >> 8) & 0xff, b += c & 0xff, n++;
          }
        if (!n) continue;
        px(x, y) = ((r / n) << 16) | ((g / n) << 8) | (b / n);  // alpha stays 0
        next[size_t(y) * w + x] = 1;
        any = true;
      }
    known.swap(next);
    if (!any) break;
  }
}

// ARGB8888 copy of `s`; pixels of colour `key` (0xRRGGBB, -1 none) transparent.
SDL_Surface* Keyed(SDL_Surface* s, int key) {
  SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
  if (!c || key < 0) return c;
  for (int y = 0; y < c->h; y++) {
    auto* row = (Uint32*)((Uint8*)c->pixels + y * c->pitch);
    for (int x = 0; x < c->w; x++)
      if ((row[x] & 0xffffff) == Uint32(key)) row[x] = Uint32(key);  // alpha 0
  }
  Bleed(c, 3);
  return c;
}

int KeyOf(SDL_Surface* s) {
  Uint32 k;
  Uint32 pix = SDL_SurfaceHasColorKey(s) && SDL_GetSurfaceColorKey(s, &k)
                   ? k
                   : ((Uint32*)((Uint8*)s->pixels + (s->h - 1) * s->pitch))[0];
  Uint8 r, g, b;
  SDL_GetRGB(pix, SDL_GetPixelFormatDetails(s->format), SDL_GetSurfacePalette(s), &r, &g, &b);
  return (r << 16) | (g << 8) | b;
}

}  // namespace

Painter::~Painter() { Shutdown(); }

void Painter::Init(SDL_Renderer* ren, int lw, int lh, HdPictures* hd) {
  ren_ = ren;
  lw_ = lw;
  lh_ = lh;
  hd_ = hd;
  g_instance = this;
  target_ = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, lw, lh);
  if (target_) SDL_SetTextureScaleMode(target_, SDL_SCALEMODE_LINEAR);
  overlay_ = SDL_CreateSurface(lw, lh, SDL_PIXELFORMAT_ARGB8888);
  overlay_tex_ = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, lw, lh);
  if (overlay_tex_) SDL_SetTextureBlendMode(overlay_tex_, SDL_BLENDMODE_BLEND);
}

void Painter::Shutdown() {
  for (auto& [_, t] : tex_)
    if (t.tex) SDL_DestroyTexture(t.tex);
  tex_.clear();
  for (auto& [_, l] : lines_)
    if (l.tex) SDL_DestroyTexture(l.tex);
  lines_.clear();
  if (target_) SDL_DestroyTexture(target_);
  if (overlay_tex_) SDL_DestroyTexture(overlay_tex_);
  if (overlay_) SDL_DestroySurface(overlay_);
  target_ = overlay_tex_ = nullptr;
  overlay_ = nullptr;
  if (g_instance == this) g_instance = nullptr;
  if (current_ == this) current_ = nullptr;
}

void Painter::Forget(void*, void* surface) {
  Painter* p = g_instance;
  if (!p) return;
  for (auto it = p->tex_.begin(); it != p->tex_.end();) {
    if (std::get<0>(it->first) == surface) {
      if (it->second.tex) SDL_DestroyTexture(it->second.tex);
      it = p->tex_.erase(it);
    } else {
      ++it;
    }
  }
}

void Painter::Begin(int out_w, int out_h, bool upgraded) {
  current_ = this;
  frame_++;
  out_w_ = out_w;
  out_h_ = out_h;
  upgraded_ = upgraded || !target_;
  // Where the 800x600 frame sits in the output (letterboxed, any factor).
  float s = std::min(float(out_w) / float(lw_), float(out_h) / float(lh_));
  view_s_ = s;
  view_x_ = (float(out_w) - lw_ * s) / 2;
  view_y_ = (float(out_h) - lh_ * s) / 2;
  if (upgraded_) {
    scale_ = s, off_x_ = view_x_, off_y_ = view_y_;
  } else {
    prev_target_ = SDL_GetRenderTarget(ren_);
    SDL_SetRenderTarget(ren_, target_);
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
    SDL_RenderClear(ren_);
    scale_ = 1, off_x_ = 0, off_y_ = 0;
  }
  SetClip(nullptr);
}

void Painter::End() {
  SetClip(nullptr);
  if (!upgraded_) {
    SDL_SetRenderTarget(ren_, prev_target_);
    SDL_FRect d{std::round(view_x_), std::round(view_y_), std::round(lw_ * view_s_), std::round(lh_ * view_s_)};
    SDL_RenderTexture(ren_, target_, nullptr, &d);
  }
  scale_ = view_s_, off_x_ = view_x_, off_y_ = view_y_;
  // Text lines not drawn for a while (changing captions, the edit caret).
  for (auto it = lines_.begin(); it != lines_.end();) {
    if (frame_ - it->second.used > 300) {
      if (it->second.tex) SDL_DestroyTexture(it->second.tex);
      it = lines_.erase(it);
    } else {
      ++it;
    }
  }
}

void Painter::ToLogical(float px, float py, float* x, float* y) const {
  *x = (px - view_x_) / view_s_;
  *y = (py - view_y_) / view_s_;
}

SDL_Rect Painter::Viewport() const {
  return {int(std::lround(view_x_)), int(std::lround(view_y_)), int(std::lround(lw_ * view_s_)),
          int(std::lround(lh_ * view_s_))};
}

SDL_FRect Painter::Pixels(const SDL_Rect& r) const {
  float x0 = std::round(X(float(r.x))), y0 = std::round(Y(float(r.y)));
  float x1 = std::round(X(float(r.x + r.w))), y1 = std::round(Y(float(r.y + r.h)));
  return {x0, y0, x1 - x0, y1 - y0};
}

SDL_FRect Painter::PixelsF(const SDL_FRect& r) const {
  float x0 = std::round(X(r.x)), y0 = std::round(Y(r.y));
  float x1 = std::round(X(r.x + r.w)), y1 = std::round(Y(r.y + r.h));
  return {x0, y0, x1 - x0, y1 - y0};
}

void Painter::SetClip(const SDL_Rect* r) {
  clipped_ = r != nullptr;
  if (!r) {
    SDL_SetRenderClipRect(ren_, nullptr);
    return;
  }
  clip_ = *r;
  SDL_FRect f = Pixels(*r);
  SDL_Rect p{int(f.x), int(f.y), int(f.w), int(f.h)};
  SDL_SetRenderClipRect(ren_, &p);
}

void Painter::Fill(const SDL_Rect& r, SDL_Color c) {
  SDL_SetRenderDrawBlendMode(ren_, c.a == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, c.a);
  SDL_FRect f = Pixels(r);
  SDL_RenderFillRect(ren_, &f);
}

void Painter::RoundRect(const SDL_Rect& r, float radius, SDL_Color c) {
  SDL_FRect f = Pixels(r);
  float rad = std::max(0.0f, std::min(radius * scale_, std::min(f.w, f.h) / 2));
  // The outline as corner arcs, then a fan from the centre to the outline
  // (inset half a pixel, full colour) and a fringe to half a pixel out
  // (transparent): the edge is smooth at any size.
  const int seg = std::max(2, int(rad / 2));
  std::vector<SDL_FPoint> dir, ctr;
  const float cx[4] = {f.x + f.w - rad, f.x + rad, f.x + rad, f.x + f.w - rad};
  const float cy[4] = {f.y + rad, f.y + rad, f.y + f.h - rad, f.y + f.h - rad};
  for (int q = 0; q < 4; q++)
    for (int i = 0; i <= seg; i++) {
      float a = float(M_PI) / 2 * (q + float(i) / seg);
      dir.push_back({std::cos(a), -std::sin(a)});
      ctr.push_back({cx[q], cy[q]});
    }
  SDL_FColor in{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f}, out = in;
  out.a = 0;
  std::vector<SDL_Vertex> v;
  std::vector<int> idx;
  v.push_back({{f.x + f.w / 2, f.y + f.h / 2}, in, {0, 0}});
  int n = int(dir.size());
  for (int i = 0; i < n; i++) {
    float ri = std::max(0.0f, rad - 0.5f), ro = rad + 0.5f;
    v.push_back({{ctr[i].x + dir[i].x * ri, ctr[i].y + dir[i].y * ri}, in, {0, 0}});
    v.push_back({{ctr[i].x + dir[i].x * ro, ctr[i].y + dir[i].y * ro}, out, {0, 0}});
  }
  for (int i = 0; i < n; i++) {
    int a = 1 + 2 * i, b = 1 + 2 * ((i + 1) % n);
    idx.insert(idx.end(), {0, a, b, a, a + 1, b, b, a + 1, b + 1});
  }
  SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
  SDL_RenderGeometry(ren_, nullptr, v.data(), int(v.size()), idx.data(), int(idx.size()));
}

Painter::Tex* Painter::Texture(SDL_Surface* s, int key, bool dynamic) {
  const bool want_hd = upgraded_ && !dynamic && hd_;
  auto k = std::make_tuple(s, key, want_hd);
  auto it = tex_.find(k);
  if (it != tex_.end() && !dynamic) {
    it->second.used = frame_;
    return &it->second;
  }
  // Drop the textures when the surface goes (its address may come back).
  SDL_PropertiesID props = SDL_GetSurfaceProperties(s);
  if (!SDL_HasProperty(props, kWatchProp)) SDL_SetPointerPropertyWithCleanup(props, kWatchProp, s, Forget, nullptr);
  if (dynamic && it != tex_.end() && it->second.w == s->w && it->second.h == s->h && key < 0) {
    SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
    if (c) SDL_UpdateTexture(it->second.tex, nullptr, c->pixels, c->pitch), SDL_DestroySurface(c);
    it->second.used = frame_;
    return &it->second;
  }
  if (it != tex_.end()) {
    if (it->second.tex) SDL_DestroyTexture(it->second.tex);
    tex_.erase(it);
  }
  SDL_Surface* pic = nullptr;
  bool hd = false, alpha = key >= 0;
  if (want_hd) {
    std::string name = PictureName(s);
    if (!name.empty()) {
      if (key >= 0 || key == -3) {
        if (SDL_Surface* a = hd_->Get(name, true)) pic = SDL_DuplicateSurface(a);
      }
      if (!pic) {
        if (SDL_Surface* o = hd_->Get(name, false)) {
          pic = SDL_DuplicateSurface(o);
          if (pic && key >= 0) {
            // No keyed HD picture: the original's key mask, scaled up.
            if (!warned_[name]) {
              SDL_Log("menu: %s.alpha.png missing, using the original's transparency", name.c_str());
              warned_[name] = true;
            }
            SDL_Surface* m = Keyed(s, key);
            SDL_Surface* big = m ? SDL_CreateSurface(pic->w, pic->h, SDL_PIXELFORMAT_ARGB8888) : nullptr;
            if (big) {
              SDL_SetSurfaceBlendMode(m, SDL_BLENDMODE_NONE);
              SDL_BlitSurfaceScaled(m, nullptr, big, nullptr, SDL_SCALEMODE_LINEAR);
              for (int y = 0; y < pic->h; y++) {
                auto* d = (Uint32*)((Uint8*)pic->pixels + y * pic->pitch);
                auto* a = (Uint32*)((Uint8*)big->pixels + y * big->pitch);
                for (int x = 0; x < pic->w; x++) d[x] = (d[x] & 0xffffff) | (a[x] & 0xff000000);
              }
              SDL_DestroySurface(big);
            }
            if (m) SDL_DestroySurface(m);
          }
        }
      }
      hd = pic != nullptr;
    }
  }
  if (!pic) pic = Keyed(s, key < 0 ? -1 : key);
  if (!pic) return nullptr;
  Tex t;
  t.tex = SDL_CreateTextureFromSurface(ren_, pic);
  t.w = pic->w;
  t.h = pic->h;
  t.hd = hd;
  t.used = frame_;
  // HD pictures may have alpha of their own (soft edges), and so may the
  // original when it is a 32-bit TGA (Europe Racer, A2 Racer IV: LoadTga
  // marks those surfaces BLEND).
  SDL_BlendMode own = SDL_BLENDMODE_NONE;
  SDL_GetSurfaceBlendMode(s, &own);
  SDL_SetTextureBlendMode(t.tex, alpha || hd || own == SDL_BLENDMODE_BLEND ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
  SDL_DestroySurface(pic);
  if (!t.tex) return nullptr;
  return &(tex_[k] = t);
}

void Painter::Image(SDL_Surface* s, const SDL_Rect* src, const SDL_Rect& dst, int key, bool dynamic) {
  if (!s) return;
  SDL_FRect fs = src ? SDL_FRect{float(src->x), float(src->y), float(src->w), float(src->h)}
                     : SDL_FRect{0, 0, float(s->w), float(s->h)};
  ImageF(s, fs, SDL_FRect{float(dst.x), float(dst.y), float(dst.w), float(dst.h)}, key, dynamic);
}

void Painter::ImageF(SDL_Surface* s, const SDL_FRect& src, const SDL_FRect& dst, int key, bool dynamic) {
  if (!s || src.w <= 0 || src.h <= 0 || dst.w <= 0 || dst.h <= 0) return;
  if (key == -2) key = KeyOf(s);
  if (key == -3 && (!upgraded_ || dynamic)) key = -1;
  // A picture made from parts of others: draw those parts (their HD).
  if (upgraded_ && !dynamic) {
    if (const std::vector<DerivedPart>* parts = DerivedParts(s)) {
      for (const DerivedPart& p : *parts) {
        // The part of `src` (in s) that this part covers.
        float x0 = std::max(src.x, float(p.dst_rect.x)), y0 = std::max(src.y, float(p.dst_rect.y));
        float x1 = std::min(src.x + src.w, float(p.dst_rect.x + p.dst_rect.w));
        float y1 = std::min(src.y + src.h, float(p.dst_rect.y + p.dst_rect.h));
        if (x1 <= x0 || y1 <= y0) continue;
        float sx = float(p.src_rect.w) / float(p.dst_rect.w), sy = float(p.src_rect.h) / float(p.dst_rect.h);
        SDL_FRect ps{p.src_rect.x + (x0 - p.dst_rect.x) * sx, p.src_rect.y + (y0 - p.dst_rect.y) * sy,
                     (x1 - x0) * sx, (y1 - y0) * sy};
        float dx = dst.w / src.w, dy = dst.h / src.h;
        SDL_FRect pd{dst.x + (x0 - src.x) * dx, dst.y + (y0 - src.y) * dy, (x1 - x0) * dx, (y1 - y0) * dy};
        ImageF(p.src, ps, pd, key, false);
      }
      return;
    }
  }
  Tex* t = Texture(s, key, dynamic);
  if (!t) return;
  float kx = float(t->w) / float(s->w), ky = float(t->h) / float(s->h);
  SDL_FRect ts{src.x * kx, src.y * ky, src.w * kx, src.h * ky};
  SDL_FRect d = PixelsF(dst);
  SDL_SetTextureScaleMode(t->tex, upgraded_ ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
  SDL_SetTextureAlphaMod(t->tex, alpha_);
  SDL_RenderTexture(ren_, t->tex, &ts, &d);
  SDL_SetTextureAlphaMod(t->tex, 255);
}

void Painter::Text(const SDL_Rect& r, const std::string& s, const TextStyle& st) {
  if (!st.font || !st.font->Ok() || s.empty()) return;
  Font& f = *st.font;
  auto lines = WrapText(f, s, st.px, r.w, st.wrap);
  int lh = f.LineHeight(st.px);
  int total = lh * int(lines.size());
  int y = r.y;
  if (st.valign == VAlign::kCenter) y = r.y + (r.h - total) / 2;
  if (st.valign == VAlign::kBottom) y = r.y + r.h - total;
  // Clipped to r (DrawText) inside the current clip.
  const bool had_clip = clipped_;
  const SDL_Rect old = clip_;
  SDL_Rect c = r;
  if (had_clip && !SDL_GetRectIntersection(&old, &r, &c)) return;
  SetClip(&c);
  const long size26 = upgraded_ ? std::lround(st.px * scale_ * 64.0f) : long(st.px) * 64;
  SDL_FRect R = Pixels(r);
  for (auto& l : lines) {
    if (!l.empty()) {
      auto key = std::make_tuple(st.font, size26, (Uint32(st.color.a) << 24) | Rgb(st.color), l);
      Line& ln = lines_[key];
      if (!ln.tex) {
        if (SDL_Surface* img = f.RenderLine(l, size26, st.color, &ln.ox, &ln.base)) {
          ln.tex = SDL_CreateTextureFromSurface(ren_, img);
          ln.w = img->w;
          ln.h = img->h;
          SDL_DestroySurface(img);
          if (ln.tex) SDL_SetTextureBlendMode(ln.tex, SDL_BLENDMODE_BLEND);
        }
      }
      ln.used = frame_;
      if (ln.tex) {
        float pen_x, base_y, squeeze = 1;
        if (!upgraded_) {
          // Exactly where Font::DrawLine puts it.
          int w = f.Width(l, st.px);
          int x = r.x;
          if (st.halign == HAlign::kCenter) x = r.x + (r.w - w) / 2;
          if (st.halign == HAlign::kRight) x = r.x + r.w - w;
          pen_x = float(x);
          base_y = float(y + f.Ascent(st.px));
        } else {
          float w = f.WidthF(l, size26);
          // Hinting at the big size can make a line a little wider than
          // the original's: keep the original's width (wrapping and
          // clipping were laid out for it).
          const float wl = float(f.Width(l, st.px)) * scale_;
          if (w > wl && w > 0) squeeze = wl / w, w = wl;
          pen_x = R.x;
          if (st.halign == HAlign::kCenter) pen_x = R.x + (R.w - w) / 2;
          if (st.halign == HAlign::kRight) pen_x = R.x + R.w - w;
          pen_x = std::round(pen_x);
          base_y = std::round(Y(float(y + f.Ascent(st.px))));
        }
        SDL_FRect d{pen_x - ln.ox * squeeze, base_y - ln.base, float(ln.w) * squeeze, float(ln.h)};
        SDL_SetTextureScaleMode(ln.tex, squeeze < 1 ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        SDL_RenderTexture(ren_, ln.tex, nullptr, &d);
      }
    }
    y += lh;
  }
  if (had_clip) SetClip(&old);
  else SetClip(nullptr);
}

SDL_Surface* Painter::OverlayBegin(const SDL_Rect& area) {
  overlay_area_ = area;
  SDL_Rect full{0, 0, lw_, lh_};
  SDL_GetRectIntersection(&area, &full, &overlay_area_);
  SDL_SetSurfaceClipRect(overlay_, nullptr);
  SDL_FillSurfaceRect(overlay_, &overlay_area_, 0);
  SDL_SetSurfaceClipRect(overlay_, &overlay_area_);
  recorded_.clear();
  in_overlay_ = true;
  return overlay_;
}

void Painter::OverlayEnd() {
  in_overlay_ = false;
  // Anything drawn with plain SDL calls (video frames)? Rows with alpha.
  const SDL_Rect& a = overlay_area_;
  int top = -1, bottom = -1;
  for (int y = a.y; y < a.y + a.h; y++) {
    const auto* row = (const Uint32*)((const Uint8*)overlay_->pixels + y * overlay_->pitch) + a.x;
    for (int x = 0; x < a.w; x++)
      if (row[x] >> 24) {
        if (top < 0) top = y;
        bottom = y;
        break;
      }
  }
  if (top >= 0) {
    SDL_Rect u{a.x, top, a.w, bottom - top + 1};
    SDL_UpdateTexture(overlay_tex_, &u,
                      (const Uint8*)overlay_->pixels + u.y * overlay_->pitch + u.x * 4, overlay_->pitch);
    SDL_FRect src{float(u.x), float(u.y), float(u.w), float(u.h)};
    SDL_FRect d = Pixels(u);
    SDL_SetTextureScaleMode(overlay_tex_, upgraded_ ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    SDL_RenderTexture(ren_, overlay_tex_, &src, &d);
  }
  auto rec = std::move(recorded_);
  recorded_.clear();
  for (auto& fn : rec) fn();
}

void Painter::Notice(const std::string& s, Font* font) {
  if (!font || !font->Ok() || s.empty()) return;
  // In output pixels, about 18 menu pixels high.
  const long size26 = std::lround(14 * view_s_ * 64.0f);
  int ox = 0, base = 0;
  SDL_Surface* img = font->RenderLine(s, size26, SDL_Color{255, 255, 255, 255}, &ox, &base);
  if (!img) return;
  SDL_Texture* t = SDL_CreateTextureFromSurface(ren_, img);
  float pad = std::round(6 * view_s_);
  SDL_FRect box{std::round(view_x_ + pad), std::round(view_y_ + pad), float(img->w) + 2 * pad,
                float(img->h) + pad};
  SDL_SetRenderClipRect(ren_, nullptr);
  SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(ren_, 0, 0, 0, 170);
  SDL_RenderFillRect(ren_, &box);
  if (t) {
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_FRect d{box.x + pad, box.y + pad / 2, float(img->w), float(img->h)};
    SDL_RenderTexture(ren_, t, nullptr, &d);
    SDL_DestroyTexture(t);
  }
  SDL_DestroySurface(img);
}

void Blit(SDL_Surface* canvas, SDL_Surface* s, const SDL_Rect* src, const SDL_Rect& dst) {
  if (!canvas || !s) return;
  Painter* p = Painter::Current();
  if (p && p->IsOverlay(canvas)) {
    const bool has_src = src != nullptr;
    const SDL_Rect sr = src ? *src : SDL_Rect{0, 0, s->w, s->h};
    // SDL_BlitSurface draws at the source size, whatever dst says.
    const SDL_Rect d{dst.x, dst.y, sr.w, sr.h};
    const int key = SDL_SurfaceHasColorKey(s) ? -2 : -1;
    p->Record([p, s, has_src, sr, d, key] { p->Image(s, has_src ? &sr : nullptr, d, key); });
    return;
  }
  SDL_Rect d = dst;
  SDL_BlitSurface(s, src, canvas, &d);
}

}  // namespace davi
