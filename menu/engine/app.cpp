#include "engine/app.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <objc/message.h>
#include <objc/runtime.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

extern char** environ;

namespace davi {

namespace {

// The green title-bar button. Fullscreen here goes without a Space of
// its own (see Init), and SDL then marks the window "no fullscreen": the
// button turns into an old-style zoom "+". Marked fullscreen-capable
// again, the button, Window > Enter Full Screen and Ctrl+Cmd+F all send
// toggleFullScreen:, which goes to the main loop as an SDL event and
// switches SDL's fullscreen (the same one as Alt+Enter).
Uint32 g_toggle_fullscreen_event = 0;

#if defined(__APPLE__)
void ToggleFullScreenAction(id, SEL, id) {
  if (!g_toggle_fullscreen_event) return;
  SDL_Event e{};
  e.type = g_toggle_fullscreen_event;
  SDL_PushEvent(&e);
}
#endif

void EnableFullscreenButton(SDL_Window* win) {
#if defined(__APPLE__)
  void* nsw = SDL_GetPointerProperty(SDL_GetWindowProperties(win), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
  if (!nsw) return;
  using Get = unsigned long (*)(void*, SEL);
  using Set = void (*)(void*, SEL, unsigned long);
  unsigned long b = reinterpret_cast<Get>(objc_msgSend)(nsw, sel_registerName("collectionBehavior"));
  b &= ~((1ul << 8) | (1ul << 9));  // FullScreenAuxiliary, FullScreenNone
  b |= 1ul << 7;                     // FullScreenPrimary
  reinterpret_cast<Set>(objc_msgSend)(nsw, sel_registerName("setCollectionBehavior:"), b);
  class_replaceMethod(object_getClass(reinterpret_cast<id>(nsw)), sel_registerName("toggleFullScreen:"),
                      reinterpret_cast<IMP>(ToggleFullScreenAction), "v@:@");
  if (!g_toggle_fullscreen_event) g_toggle_fullscreen_event = SDL_RegisterEvents(1);
#else
  (void)win;
#endif
}

void ToggleFullscreen(SDL_Window* win) {
  bool fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN;
  SDL_SetWindowFullscreen(win, !fs);
}

}  // namespace

Screen::Screen(App& a, const std::string& form_cls) : app(a) {
  std::string layout = a.skin.assets ? a.skin.assets->Find("layout/" + form_cls + ".ini") : "";
  built = form.Build(a.skin, form_cls, layout);
  if (a.skin.background_name) {
    std::string bg = a.skin.background_name(form_cls);
    if (!bg.empty() && a.skin.assets) {
      form.background = a.skin.assets->Bitmap(bg);
      if (Widget* w = form.Get("imgBackGround")) w->picture = form.background;
    }
  }
}

void Screen::Close(int result) {
  App& a = app;
  auto it = std::find(a.popups_.begin(), a.popups_.end(), this);
  if (it == a.popups_.end()) return;
  a.popups_.erase(it);
  OnHide();
  a.hover_ = a.pressed_ = nullptr;
  a.focus_ = nullptr;
  if (Screen* t = a.Top()) a.FocusFirst(*t);
  auto done = std::move(done_);
  done_ = nullptr;
  if (done) done(result);
}

App::~App() {
  audio.Shutdown();
  painter_.Shutdown();
  if (offscreen_) SDL_DestroyTexture(offscreen_);
  if (ren_) SDL_DestroyRenderer(ren_);
  if (win_) SDL_DestroyWindow(win_);
  SDL_Quit();
}

bool App::Init(const AppConfig& cfg) {
  cfg_ = cfg;
  // Fullscreen without a Space of its own: the race child (RunChild) takes
  // the screen over in place, and hiding this window then switches nothing.
  SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    SDL_Log("SDL_Init: %s", SDL_GetError());
    return false;
  }
  audio.Init();
  // Window: the 800x600 menu, scaled up to fit ~80% of the display.
  int ww = cfg.width, wh = cfg.height;
  if (const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay())) {
    float sc = std::min(dm->w * 0.8f / cfg.width, dm->h * 0.8f / cfg.height);
    if (sc > 1.0f) ww = int(cfg.width * sc), wh = int(cfg.height * sc);
  }
  win_ = SDL_CreateWindow(cfg.title.c_str(), ww, wh,
                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                              (cfg.fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
  if (!win_) return false;
  EnableFullscreenButton(win_);
  ren_ = SDL_CreateRenderer(win_, nullptr);
  if (!ren_) return false;
  SDL_SetRenderVSync(ren_, 1);
  // No logical presentation: the painter draws every widget at the
  // window's real pixel size (HiDPI), in the 800x600 layout.
  for (auto& d : cfg.hd_dirs) hd_.AddDir(d);
  painter_.Init(ren_, cfg.width, cfg.height, &hd_);
  if (cfg.render_w > 0 && cfg.render_h > 0)
    offscreen_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, cfg.render_w, cfg.render_h);
  int pw = 0, ph = 0;
  SDL_GetRenderOutputSize(ren_, &pw, &ph);
  std::fprintf(stderr, "menu: window %dx%d pixels%s, %s\n", pw, ph,
               offscreen_ ? (" (drawing offscreen at " + std::to_string(cfg.render_w) + "x" +
                             std::to_string(cfg.render_h) + ")").c_str()
                          : "",
               cfg.upgraded ? "upgraded" : "original");
  return true;
}

void App::SetUpgraded(bool on, bool notice) {
  cfg_.upgraded = on;
  if (notice) {
    notice_ = on ? "Menu: Upgraded" : "Menu: Origineel";
    notice_until_ = SDL_GetTicks() + 2000;
  }
}

void App::ActivateScreen(Screen* s) {
  hover_ = pressed_ = focus_ = nullptr;
  if (s) FocusFirst(*s);
}

void App::Show(Screen* s) {
  while (!popups_.empty()) {
    Screen* p = popups_.back();
    popups_.pop_back();
    p->OnHide();
    p->done_ = nullptr;
  }
  if (cur_ && cur_ != s) cur_->OnHide();
  cur_ = s;
  ActivateScreen(s);
  if (s) s->OnShow();
  // OnShow may have changed what is enabled, or set the focus itself.
  if (s && (!focus_ || !focus_->visible || !focus_->enabled)) FocusFirst(*s);
}

void App::ShowModal(Screen* s, std::function<void(int)> done) {
  s->popup_ = true;
  s->done_ = std::move(done);
  popups_.push_back(s);
  ActivateScreen(s);
  s->OnShow();
  if (!focus_ || !focus_->visible || !focus_->enabled) FocusFirst(*s);
}

void App::After(int ms, std::function<void()> fn, const void* owner) {
  timers_.push_back({SDL_GetTicks() + Uint64(std::max(0, ms)), std::move(fn), owner});
}

void App::CancelTimers(const void* owner) {
  timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [&](const Timer& t) { return t.owner == owner; }),
                timers_.end());
}

void App::FocusFirst(Screen& s) {
  Widget* best = nullptr;
  for (auto& w : s.form.widgets) {
    if (!w->visible || !w->enabled || !w->tab_stop) continue;
    if (w->kind != Widget::kButton && w->kind != Widget::kEdit && w->kind != Widget::kCheckBox &&
        w->kind != Widget::kRadio && w->kind != Widget::kThumb)
      continue;
    if (!best || w->tab_order < best->tab_order) best = w.get();
  }
  focus_ = best;
}

void App::MoveFocus(int dir) {
  Screen* s = Top();
  if (!s) return;
  std::vector<Widget*> v;
  for (auto& w : s->form.widgets)
    if (w->visible && w->enabled && w->tab_stop &&
        (w->kind == Widget::kButton || w->kind == Widget::kEdit || w->kind == Widget::kCheckBox ||
         w->kind == Widget::kRadio || w->kind == Widget::kThumb))
      v.push_back(w.get());
  if (v.empty()) return;
  std::stable_sort(v.begin(), v.end(), [](Widget* a, Widget* b) { return a->tab_order < b->tab_order; });
  auto it = std::find(v.begin(), v.end(), focus_);
  int i = it == v.end() ? 0 : int(it - v.begin()) + dir;
  focus_ = v[(i + int(v.size())) % int(v.size())];
}

void App::Origin(Screen& s, int* ox, int* oy) const {
  *ox = s.IsPopup() ? (cfg_.width - s.form.w) / 2 : 0;
  *oy = s.IsPopup() ? (cfg_.height - s.form.h) / 2 : 0;
}

Widget* App::HitTest(int x, int y) {
  Screen* s = Top();
  if (!s) return nullptr;
  int ox, oy;
  Origin(*s, &ox, &oy);
  x -= ox;
  y -= oy;
  for (auto it = s->form.widgets.rbegin(); it != s->form.widgets.rend(); ++it) {
    Widget* w = it->get();
    if (!w->visible) continue;
    bool clickable = w->kind == Widget::kButton || w->kind == Widget::kCheckBox || w->kind == Widget::kSlider ||
                     w->kind == Widget::kRadio || w->kind == Widget::kEdit || w->kind == Widget::kThumb ||
                     !w->onclick.empty();
    if (!clickable) continue;
    if (x >= w->r.x && x < w->r.x + w->r.w && y >= w->r.y && y < w->r.y + w->r.h) return w->enabled ? w : nullptr;
  }
  return nullptr;
}

void App::Click(Widget* w) {
  Screen* s = Top();
  if (!w || !s || !w->enabled || !w->visible) return;
  if (w->kind == Widget::kCheckBox) w->checked = !w->checked;
  if (w->kind == Widget::kRadio) {
    for (auto& o : s->form.widgets)
      if (o->kind == Widget::kRadio && o->group == w->group && o->parent == w->parent) o->checked = false;
    w->checked = true;
  }
  s->OnClick(*w);
}

void App::DrawForm(Screen& s, int ox, int oy) {
  Painter& p = painter_;
  SDL_Rect clip{ox, oy, s.form.w, s.form.h};
  p.SetClip(&clip);
  for (auto& wp : s.form.widgets) {
    Widget& w = *wp;
    if (!w.visible) continue;
    SDL_Rect r{w.r.x + ox, w.r.y + oy, w.r.w, w.r.h};
    switch (w.kind) {
      case Widget::kImage: {
        SDL_Surface* pic = w.Pic();
        if (!pic) break;
        int key = -1;
        if (w.pic_transparent && !w.owned) {
          // Delphi TBitmap transparency: the bottom-left pixel's colour,
          // unless the program set TransparentColor. (Also set on the
          // surface: flow code blits these pictures with their key.)
          Uint32 k = w.key_rgb >= 0
                         ? SDL_MapSurfaceRGB(pic, Uint8(w.key_rgb >> 16), Uint8(w.key_rgb >> 8), Uint8(w.key_rgb))
                         : ((Uint32*)((Uint8*)pic->pixels + (pic->h - 1) * pic->pitch))[0];
          SDL_SetSurfaceColorKey(pic, true, k);
          key = w.key_rgb >= 0 ? w.key_rgb : -2;
        }
        p.SetAlpha(Uint8(w.alpha));
        if (w.stretch) {
          p.Image(pic, nullptr, r, key, w.owned != nullptr);
        } else {
          SDL_Rect dst = r;
          if (w.center) dst.x += (w.r.w - pic->w) / 2, dst.y += (w.r.h - pic->h) / 2;
          SDL_Rect src{0, 0, std::min(pic->w, w.r.w ? w.r.w : pic->w), std::min(pic->h, w.r.h ? w.r.h : pic->h)};
          dst.w = src.w, dst.h = src.h;
          p.Image(pic, &src, dst, key, w.owned != nullptr);
        }
        p.SetAlpha(255);
        break;
      }
      case Widget::kLabel: {
        TextStyle st{skin.GetFont(w.font.font), w.font.px, w.enabled ? w.font.color : w.disabled_color,
                     w.halign, w.valign, w.wrap};
        SDL_Rect tr = r;
        if (w.autosize && !w.wrap && st.font) {
          tr.w = std::max(tr.w, st.font->Width(w.caption, w.font.px) + 2);
          tr.h = std::max(tr.h, st.font->LineHeight(w.font.px) * 1);
        }
        p.Text(tr, w.caption, st);
        break;
      }
      case Widget::kEdit: {
        SDL_Rect in = r;
        if (w.border) {
          p.Fill(r, SDL_Color{0x80, 0x80, 0x80, 255});
          in = {r.x + 1, r.y + 1, r.w - 2, r.h - 2};
        }
        p.Fill(in, SDL_Color{w.color.r, w.color.g, w.color.b, 255});
        if (w.select_all && focus_ == &w && !w.caption.empty() && skin.GetFont(w.font.font)) {
          SDL_Rect sel{r.x + 3, r.y + 2, skin.GetFont(w.font.font)->Width(w.caption, w.font.px) + 2, r.h - 4};
          p.Fill(sel, SDL_Color{0, 0, 0x80, 255});
        }
        SDL_Color tc = w.select_all && focus_ == &w ? SDL_Color{255, 255, 255, 255} : w.font.color;
        TextStyle st{skin.GetFont(w.font.font), w.font.px, tc, HAlign::kLeft, VAlign::kCenter, false};
        SDL_Rect tr{r.x + 4, r.y, r.w - 8, r.h};
        std::string t = w.caption;
        if (focus_ == &w && !w.select_all && (SDL_GetTicks() / 500) % 2 == 0) t += "|";
        p.Text(tr, t, st);
        break;
      }
      case Widget::kButton:
      case Widget::kCheckBox:
      case Widget::kRadio: {
        if (!w.def) break;
        GraphicDef::State state = GraphicDef::kUp;
        if (!w.enabled) state = GraphicDef::kDisabled;
        else if (pressed_ == &w && hover_ == &w) state = GraphicDef::kDown;
        else if (hover_ == &w) state = GraphicDef::kHovered;
        else if (focus_ == &w) state = GraphicDef::kFocused;
        if (w.kind != Widget::kButton) state = w.checked ? GraphicDef::kDown : GraphicDef::kUp;
        if (SDL_Surface* img = w.def->Get(state)) {
          SDL_Rect src{0, 0, std::min(img->w, w.r.w), std::min(img->h, w.r.h)};
          // The state pictures have the background they were drawn for in
          // them. Upgraded: their HD version has real alpha there (see
          // tools/upscale_menu.py). A button with Transparent=1 (mod
          // layout) keys out its bottom-left colour in both looks.
          p.SetAlpha(Uint8(w.def->alpha));
          p.Image(img, &src, SDL_Rect{r.x, r.y, src.w, src.h}, w.pic_transparent ? -2 : -3);
          p.SetAlpha(255);
        }
        if (!w.caption.empty()) {
          SDL_Rect cr{0, 0, w.r.w, w.r.h};
          auto it = skin.caption_rects.find(w.def->caption_rect);
          if (it != skin.caption_rects.end()) cr = it->second(w);
          // TGDIButtonDefinition.CaptionRect: w/h <= 0 = up to the edge minus that.
          const SDL_Rect& dc = w.def->caption;
          if (dc.x || dc.y || dc.w || dc.h)
            cr = {dc.x, dc.y, dc.w > 0 ? dc.w : w.r.w - dc.x + dc.w, dc.h > 0 ? dc.h : w.r.h - dc.y + dc.h};
          bool side = w.kind != Widget::kButton && (dc.x || dc.y);  // text beside a radio/check mark
          cr.x += r.x;
          cr.y += r.y;
          // DrawText: DT_CENTER|DT_VCENTER|DT_SINGLELINE, or DT_CENTER|DT_WORDBREAK (top).
          TextStyle st{skin.GetFont(w.font.font), w.font.px, w.enabled ? w.font.color : w.disabled_color,
                       side ? HAlign::kLeft : HAlign::kCenter, w.wrap ? VAlign::kTop : VAlign::kCenter, w.wrap};
          p.Text(cr, w.caption, st);
        }
        break;
      }
      case Widget::kThumb: {
        // Port: a picture in a rounded frame like the game's picture boxes
        // (orange inside, light rim); the chosen one has the cyan of the
        // game's lamps, hover and keyboard focus a white rim, unavailable
        // ones are faded.
        bool lit = w.enabled && (hover_ == &w || focus_ == &w);
        const float rad = 5;
        if (w.checked && w.enabled)
          p.RoundRect(SDL_Rect{r.x - 2, r.y - 2, r.w + 4, r.h + 4}, rad + 2, SDL_Color{79, 230, 255, 110});
        SDL_Color rim = !w.enabled  ? SDL_Color{230, 190, 110, 255}
                        : lit       ? SDL_Color{255, 255, 255, 255}
                        : w.checked ? SDL_Color{79, 230, 255, 255}
                                    : SDL_Color{255, 243, 196, 255};
        p.RoundRect(r, rad, rim);
        SDL_Rect ir{r.x + 2, r.y + 2, r.w - 4, r.h - 4};
        p.RoundRect(ir, rad - 2, SDL_Color{255, 165, 0, 255});
        if (SDL_Surface* pic = w.Pic()) {
          float k = std::min(1.0f, std::min(float(ir.w) / pic->w, float(ir.h) / pic->h));
          int dw = int(std::lround(pic->w * k)), dh = int(std::lround(pic->h * k));
          SDL_Rect d{ir.x + (ir.w - dw) / 2, ir.y + (ir.h - dh) / 2, dw, dh};
          p.Image(pic, nullptr, d, w.pic_transparent ? (w.key_rgb >= 0 ? w.key_rgb : -2) : -1);
        } else if (!w.caption.empty()) {
          TextStyle st{skin.GetFont(w.font.font), w.font.px, w.font.color, HAlign::kCenter, VAlign::kCenter, false};
          p.Text(ir, w.caption, st);
        }
        if (!w.enabled) p.RoundRect(ir, rad - 2, SDL_Color{255, 165, 0, 170});
        break;
      }
      case Widget::kSlider: {
        if (w.picture) p.Image(w.picture, nullptr, SDL_Rect{r.x, r.y, w.picture->w, w.picture->h});
        SDL_Surface* n = w.def ? w.def->Get(GraphicDef::kUp) : nullptr;
        if (n && w.max > w.min) {
          int span = w.r.w - 2 * w.margin - n->w;
          SDL_Rect d{r.x + w.margin + (w.pos - w.min) * span / (w.max - w.min), r.y + (w.r.h - n->h) / 2, n->w, n->h};
          p.Image(n, nullptr, d);
        }
        break;
      }
      default:
        break;
    }
  }
  s.PaintOver(p.OverlayBegin(clip), ox, oy);
  p.OverlayEnd();
  p.SetClip(nullptr);
}

void App::Draw(const std::string* shot) {
  // Output: the window, or the offscreen test frame.
  int ow = 0, oh = 0;
  if (offscreen_) {
    SDL_SetRenderTarget(ren_, offscreen_);
    ow = cfg_.render_w, oh = cfg_.render_h;
  } else {
    SDL_GetRenderOutputSize(ren_, &ow, &oh);
  }
  SDL_SetRenderClipRect(ren_, nullptr);
  SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
  SDL_RenderClear(ren_);
  painter_.Begin(ow, oh, cfg_.upgraded);
  if (cur_) DrawForm(*cur_, 0, 0);
  for (Screen* p : popups_) {
    int ox, oy;
    Origin(*p, &ox, &oy);
    if (p->form.background) {
      SDL_Rect dst{ox, oy, p->form.background->w, p->form.background->h};
      painter_.Image(p->form.background, nullptr, dst);
    }
    DrawForm(*p, ox, oy);
  }
  painter_.End();
  int dim = dim_;
  if (fade_in_at_) {
    Uint64 t = SDL_GetTicks() - fade_in_at_;
    dim = t >= kFadeMs ? 0 : int(255 - 255 * t / kFadeMs);
    if (!dim) fade_in_at_ = 0;
  }
  if (dim > 0) {
    SDL_SetRenderClipRect(ren_, nullptr);
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, Uint8(std::min(dim, 255)));
    SDL_RenderFillRect(ren_, nullptr);
  }
  if (!notice_.empty() && SDL_GetTicks() < notice_until_ && !dim) painter_.Notice(notice_, skin.GetFont(2));
  if (shot) {
    if (SDL_Surface* img = SDL_RenderReadPixels(ren_, nullptr)) {
      if (!SDL_SavePNG(img, shot->c_str())) SDL_Log("screenshot %s: %s", shot->c_str(), SDL_GetError());
      SDL_DestroySurface(img);
    }
  }
  if (offscreen_) {
    // Show the test frame in the window too, fitted.
    SDL_SetRenderTarget(ren_, nullptr);
    int ww = 0, wh = 0;
    SDL_GetRenderOutputSize(ren_, &ww, &wh);
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
    SDL_RenderClear(ren_);
    float k = std::min(float(ww) / ow, float(wh) / oh);
    SDL_FRect d{(ww - ow * k) / 2, (wh - oh * k) / 2, ow * k, oh * k};
    SDL_SetTextureScaleMode(offscreen_, SDL_SCALEMODE_LINEAR);
    SDL_RenderTexture(ren_, offscreen_, nullptr, &d);
  }
  SDL_RenderPresent(ren_);
}

void App::Screenshot(const std::string& path) { Draw(&path); }

void App::LoadScript(const std::string& path) {
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    std::istringstream ss(line);
    Step s{};
    if (!(ss >> s.at >> s.cmd)) continue;
    std::getline(ss, s.arg);
    s.arg = Trim(s.arg);
    script_.push_back(s);
  }
  std::stable_sort(script_.begin(), script_.end(), [](const Step& a, const Step& b) { return a.at < b.at; });
  script_t0_ = SDL_GetTicks();
}

// A key press on screen `t` (also the script's "key"): edits, the
// screen's own keys, thumbnail rows, Tab, Esc (cancel), Enter/Space.
void App::Key(Screen& t, SDL_Keycode k, SDL_Keymod mod) {
  if (focus_ && focus_->kind == Widget::kEdit && k == SDLK_BACKSPACE) {
    if (focus_->select_all) focus_->caption.clear(), focus_->select_all = false;
    else if (!focus_->caption.empty()) focus_->caption.pop_back();
    t.OnEditChange(*focus_);
    return;
  }
  if (t.OnKey(k)) return;
  if (focus_ && focus_->kind == Widget::kThumb &&
      (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_UP || k == SDLK_DOWN)) {
    // Port: the arrow keys walk a row (or column) of thumbnails.
    std::vector<Widget*> row;
    for (auto& w : t.form.widgets)
      if (w->kind == Widget::kThumb && w->group == focus_->group && w->visible && w->enabled)
        row.push_back(w.get());
    std::stable_sort(row.begin(), row.end(), [](Widget* a, Widget* b) { return a->tab_order < b->tab_order; });
    auto it = std::find(row.begin(), row.end(), focus_);
    int n = int(row.size()), i = int(it - row.begin()) + (k == SDLK_LEFT || k == SDLK_UP ? -1 : 1);
    if (it != row.end() && i >= 0 && i < n) focus_ = row[i];
    return;
  }
  if (k == SDLK_TAB) {
    MoveFocus(mod & SDL_KMOD_SHIFT ? -1 : 1);
  } else if (k == SDLK_ESCAPE) {
    for (auto& w : t.form.widgets)
      if (w->is_cancel && w->visible && w->enabled) {
        Click(w.get());
        break;
      }
  } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) {
    Widget* target = focus_ && focus_->kind != Widget::kEdit ? focus_ : nullptr;
    if (!target && k != SDLK_SPACE)
      for (auto& w : t.form.widgets)
        if (w->is_default && w->visible && w->enabled) target = w.get();
    if (target) {
      if (on_button_down && (target->kind == Widget::kButton || target->kind == Widget::kThumb))
        on_button_down(*target);
      Click(target);
    }
  }
}

void App::RunScript() {
  while (!script_.empty() && SDL_GetTicks() - script_t0_ >= script_.front().at) {
    Step s = script_.front();
    script_.erase(script_.begin());
    Screen* t = Top();
    if (s.cmd == "click" && t) {
      Widget* w = t->form.Get(s.arg);
      if (!w) SDL_Log("script: no %s on %s", s.arg.c_str(), t->form.cls.c_str());
      else {
        if (on_button_down && (w->kind == Widget::kButton || w->kind == Widget::kThumb)) on_button_down(*w);
        Click(w);
      }
    } else if (s.cmd == "key" && t) {
      SDL_Keycode k = SDL_GetKeyFromName(s.arg.c_str());
      if (cfg_.upgrade_key && k == cfg_.upgrade_key) SetUpgraded(!cfg_.upgraded, true);
      else Key(*t, k, SDL_KMOD_NONE);
    } else if (s.cmd == "type" && t) {
      if (focus_ && focus_->kind == Widget::kEdit) {
        if (focus_->select_all) focus_->caption.clear(), focus_->select_all = false;
        focus_->caption += s.arg;
        t->OnEditChange(*focus_);
      }
    } else if (s.cmd == "shot") {
      Screenshot(s.arg);
    } else if (s.cmd == "upgraded") {
      SetUpgraded(s.arg != "0");
    } else if (s.cmd == "hover" && t) {
      hover_ = t->form.Get(s.arg);
    } else if (s.cmd == "quit") {
      Quit(0);
    }
  }
}

int App::Run() {
  Uint64 last = SDL_GetTicks();
  while (!quit_) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      Screen* t = Top();
      switch (e.type) {
        case SDL_EVENT_QUIT:
          quit_ = true;
          break;
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
          SDL_ConvertEventToRenderCoordinates(ren_, &e);  // window -> output pixels
          float fx = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x;
          float fy = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y;
          painter_.ToLogical(fx, fy, &fx, &fy);
          fx = std::floor(fx), fy = std::floor(fy);
          Widget* w = HitTest(int(fx), int(fy));
          hover_ = w;
          // Sliders follow the mouse while the button is held.
          Widget* sl = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? w : pressed_;
          if (sl && sl->kind == Widget::kSlider && t &&
              (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || (e.type == SDL_EVENT_MOUSE_MOTION && (e.motion.state & SDL_BUTTON_LMASK)))) {
            int ox, oy;
            Origin(*t, &ox, &oy);
            SDL_Surface* n = sl->def ? sl->def->Get(GraphicDef::kUp) : nullptr;
            int nw = n ? n->w : 0, span = std::max(1, sl->r.w - 2 * sl->margin - nw);
            int v = sl->min + (int(fx) - ox - sl->r.x - sl->margin - nw / 2) * (sl->max - sl->min) / span;
            v = std::max(sl->min, std::min(sl->max, v));
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) pressed_ = sl;
            if (v != sl->pos) {
              sl->pos = v;
              t->OnClick(*sl);
            }
            if (e.type == SDL_EVENT_MOUSE_MOTION) break;
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) break;
          }
          if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            pressed_ = w;
            if (w && (w->kind == Widget::kButton || w->kind == Widget::kEdit || w->kind == Widget::kCheckBox ||
                      w->kind == Widget::kRadio || w->kind == Widget::kThumb) && w->tab_stop)
              focus_ = w;
            if (w && on_button_down && (w->kind == Widget::kButton || w->kind == Widget::kThumb)) on_button_down(*w);
          } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
            Widget* p = pressed_;
            pressed_ = nullptr;
            if (p && p == w) Click(p);
          }
          break;
        }
        case SDL_EVENT_TEXT_INPUT:
          if (focus_ && focus_->kind == Widget::kEdit) {
            if (focus_->select_all) focus_->caption.clear(), focus_->select_all = false;
            focus_->caption += Utf8ToCp1252(e.text.text);
            if (focus_->max_len > 0 && int(focus_->caption.size()) > focus_->max_len)
              focus_->caption.resize(focus_->max_len);
            if (Screen* t = Top()) t->OnEditChange(*focus_);
          }
          break;
        case SDL_EVENT_KEY_DOWN: {
          if (!t) break;
          SDL_Keycode k = e.key.key;
          if (k == SDLK_F12 && !cfg_.screenshot_path.empty()) {
            Screenshot(cfg_.screenshot_path);
            break;
          }
          if (cfg_.upgrade_key && k == cfg_.upgrade_key && !e.key.repeat) {
            SetUpgraded(!cfg_.upgraded, true);
            break;
          }
          if ((k == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT)) ||
              (k == SDLK_F && (e.key.mod & SDL_KMOD_GUI) && (e.key.mod & SDL_KMOD_CTRL))) {
            if (!e.key.repeat) ToggleFullscreen(win_);
            break;
          }
          Key(*t, k, e.key.mod);
          break;
        }
        default:
          if (g_toggle_fullscreen_event && e.type == g_toggle_fullscreen_event) ToggleFullscreen(win_);
          break;
      }
    }
    // Text input only while an edit has focus.
    bool want_text = focus_ && focus_->kind == Widget::kEdit;
    if (want_text != SDL_TextInputActive(win_)) {
      if (want_text) SDL_StartTextInput(win_);
      else SDL_StopTextInput(win_);
    }
    Uint64 now = SDL_GetTicks();
    for (size_t i = 0; i < timers_.size();) {
      if (now >= timers_[i].at) {
        auto fn = std::move(timers_[i].fn);
        timers_.erase(timers_.begin() + i);
        fn();
        i = 0;
      } else {
        i++;
      }
    }
    RunScript();
    double dt = double(now - last) / 1000.0;
    last = now;
    if (cur_) cur_->Update(dt);
    for (Screen* p : std::vector<Screen*>(popups_)) p->Update(dt);
    audio.Update();
    Draw();
  }
  return exit_code_;
}

namespace {

struct Placement {
  int x = 0, y = 0, w = 0, h = 0;
  bool fullscreen = false;
};

Placement WindowPlacement(SDL_Window* win) {
  Placement p;
  p.fullscreen = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN;
  SDL_Rect r;
  if (p.fullscreen && SDL_GetDisplayBounds(SDL_GetDisplayForWindow(win), &r)) {
    p.x = r.x, p.y = r.y, p.w = r.w, p.h = r.h;
  } else {
    SDL_GetWindowPosition(win, &p.x, &p.y);
    SDL_GetWindowSize(win, &p.w, &p.h);
  }
  return p;
}

// macOS 14 and later activate apps cooperatively: the active app (the
// menu) hands activation to the race before the race asks for it.
void YieldActivationTo(pid_t pid) {
#if defined(__APPLE__)
  void* app = reinterpret_cast<void* (*)(void*, SEL)>(objc_msgSend)(
      reinterpret_cast<void*>(objc_getClass("NSApplication")), sel_registerName("sharedApplication"));
  SEL yield = sel_registerName("yieldActivationToApplication:");
  if (!app || !class_respondsToSelector(object_getClass(reinterpret_cast<id>(app)), yield)) return;
  void* other = reinterpret_cast<void* (*)(void*, SEL, int)>(objc_msgSend)(
      reinterpret_cast<void*>(objc_getClass("NSRunningApplication")),
      sel_registerName("runningApplicationWithProcessIdentifier:"), int(pid));
  if (other) reinterpret_cast<void (*)(void*, SEL, void*)>(objc_msgSend)(app, yield, other);
#else
  (void)pid;
#endif
}

void Place(SDL_Window* win, const Placement& p) {
  if (p.fullscreen) {
    SDL_SetWindowPosition(win, p.x, p.y);  // picks the display
    SDL_SetWindowFullscreen(win, true);
  } else {
    SDL_SetWindowFullscreen(win, false);
    SDL_SetWindowSize(win, p.w, p.h);
    SDL_SetWindowPosition(win, p.x, p.y);
  }
  SDL_SyncWindow(win);
}

}  // namespace

void App::DrawDim(int dim) {
  dim_ = dim;
  Draw();
  dim_ = 0;
}

int App::RunChild(const std::vector<std::string>& argv, std::vector<std::string>* messages) {
  // The race takes over this window's place (SwARc's host link: the child
  // gets <ID>_WINDOW and a socket <ID>_LINK_FD). This window fades to
  // black, stays up (black) until the race says its window is "shown",
  // then hides; when the race says "exit" it comes back black at the
  // race window's last placement before the race window goes, and the
  // main loop fades it in. Without a link (no child_id) it is only hidden.
  const Placement before = WindowPlacement(win_);
  const float density = SDL_GetWindowPixelDensity(win_);
  std::fprintf(stderr, "menu: window %d,%d %dx%d%s before the race\n", before.x, before.y, before.w,
               before.h, before.fullscreen ? " fullscreen" : "");
  audio.Pause(true);
  if (!fade_in_at_)  // else still black from the race before (a restart)
    for (int i = 1; i <= 8; i++) DrawDim(255 * i / 8);
  fade_in_at_ = 0;
  int sv[2] = {-1, -1};
  std::vector<std::string> env_s;
  for (char** e = environ; *e; e++) env_s.push_back(*e);
  if (!cfg_.child_id.empty() && socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
    fcntl(sv[0], F_SETFD, FD_CLOEXEC);
    fcntl(sv[0], F_SETFL, fcntl(sv[0], F_GETFL) | O_NONBLOCK);
    const std::string pw = cfg_.child_id + "_WINDOW=", pl = cfg_.child_id + "_LINK_FD=";
    env_s.erase(std::remove_if(env_s.begin(), env_s.end(),
                               [&](const std::string& v) { return !v.compare(0, pw.size(), pw) || !v.compare(0, pl.size(), pl); }),
                env_s.end());
    char buf[160];
    std::snprintf(buf, sizeof buf, "%d,%d,%d,%d,%d,%.3f", before.x, before.y, before.w, before.h,
                  before.fullscreen ? 1 : 0, density > 0 ? density : 1.0f);
    env_s.push_back(pw + buf);
    env_s.push_back(pl + std::to_string(sv[1]));
  }
  std::vector<char*> envp;
  for (auto& v : env_s) envp.push_back(const_cast<char*>(v.c_str()));
  envp.push_back(nullptr);
  std::vector<char*> args;
  for (auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
  args.push_back(nullptr);
  pid_t pid;
  int rc = posix_spawn(&pid, args[0], nullptr, nullptr, args.data(), envp.data());
  if (sv[1] >= 0) close(sv[1]);
  if (rc != 0) {
    SDL_Log("menu: cannot start %s (%d)", args[0], rc);
    if (sv[0] >= 0) close(sv[0]);
    audio.Pause(false);
    fade_in_at_ = SDL_GetTicks();
    return -1;
  }
  if (sv[0] < 0) SDL_HideWindow(win_);
  bool hidden = sv[0] < 0, covered = false;
  // "closed": the user closed the race window (close button, Cmd+Q). Menu
  // and race are one program to the user, so the menu quits too instead
  // of coming back (Davilex-RED LEARNINGS.md, "venster dicht = spel dicht").
  bool closed = false;
  Placement after = before;
  std::string pending;
  auto handle = [&](const std::string& line) {
    if (line == "closed") {
      closed = true;
      std::fprintf(stderr, "menu: the race window was closed, quitting\n");
    } else if (line == "shown") {
      YieldActivationTo(pid);
      if (!hidden) SDL_HideWindow(win_);
      hidden = true;
      const char go = 'a';  // the race window takes the focus now
      (void)!write(sv[0], &go, 1);
      std::fprintf(stderr, "menu: race window shown, menu window hidden\n");
    } else if (!line.compare(0, 7, "window ")) {
      int fs = 0;
      if (std::sscanf(line.c_str() + 7, "%d %d %d %d %d", &after.x, &after.y, &after.w, &after.h, &fs) == 5)
        after.fullscreen = fs != 0;
      std::fprintf(stderr, "menu: race window %d,%d %dx%d%s at the end\n", after.x, after.y, after.w, after.h,
                   after.fullscreen ? " fullscreen" : "");
    } else if (line == "exit" && closed) {
      covered = true;  // stay hidden: the menu quits as well
      const char ack = 'x';
      (void)!write(sv[0], &ack, 1);
    } else if (line == "exit") {
      // Back at the race window's place, black, before that one goes.
      Place(win_, after);
      if (hidden) SDL_ShowWindow(win_);
      hidden = false;
      covered = true;
      SDL_RaiseWindow(win_);
      DrawDim(255);
      DrawDim(255);
      const char ack = 'x';
      (void)!write(sv[0], &ack, 1);
    } else if (messages) {
      messages->push_back(line);
    }
  };
  auto read_link = [&] {
    if (sv[0] < 0) return;
    char buf[512];
    for (;;) {
      ssize_t n = read(sv[0], buf, sizeof buf);
      if (n <= 0) break;
      pending.append(buf, size_t(n));
    }
    for (size_t nl; (nl = pending.find('\n')) != std::string::npos;) {
      std::string line = pending.substr(0, nl);
      pending.erase(0, nl + 1);
      handle(line);
    }
  };
  int status = 0;
  for (;;) {
    read_link();
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid || r < 0) break;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) kill(pid, SIGTERM);
    }
    if (!hidden) DrawDim(255);  // vsync paces the loop
    else SDL_Delay(10);
  }
  read_link();
  if (sv[0] >= 0) close(sv[0]);
  if (closed) {
    Quit(0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  }
  if (!covered) {  // no "exit" (no link, or the race crashed)
    Place(win_, after);
    SDL_ShowWindow(win_);
    DrawDim(255);
  }
  SDL_RaiseWindow(win_);
  const Placement now = WindowPlacement(win_);
  std::fprintf(stderr, "menu: window %d,%d %dx%d%s after the race\n", now.x, now.y, now.w, now.h,
               now.fullscreen ? " fullscreen" : "");
  audio.Pause(false);
  hover_ = pressed_ = nullptr;
  fade_in_at_ = SDL_GetTicks();
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

}  // namespace davi
