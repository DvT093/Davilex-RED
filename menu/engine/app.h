// Window, main loop and screen stack of the menu. A Screen is one Delphi
// form plus the flow code behind it; popups are modal screens drawn
// centered on top of the current one.
#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/assets.h"
#include "engine/form.h"
#include "engine/hd.h"
#include "engine/paint.h"

namespace davi {

class App;

class Screen {
 public:
  Screen(App& app, const std::string& form_cls);
  virtual ~Screen() = default;
  Form form;
  App& app;
  bool built = false;

  virtual void OnShow() {}
  virtual void OnHide() {}
  // A button (or clickable image/label) was clicked.
  virtual void OnClick(Widget& w) { (void)w; }
  // Return true if handled (Enter/Esc/Tab are handled by the App otherwise).
  virtual bool OnKey(SDL_Keycode k) { (void)k; return false; }
  virtual void Update(double dt) { (void)dt; }
  virtual void OnEditChange(Widget& w) { (void)w; }
  // Extra drawing on top of the widgets (canvas = whole 800x600 frame).
  // Draw pictures with davi::Blit and text with davi::DrawText: those are
  // drawn sharp at the window's resolution; other SDL drawing is scaled.
  virtual void PaintOver(SDL_Surface* canvas, int ox, int oy) { (void)canvas; (void)ox; (void)oy; }

  // Popups only: closes this popup with a result (Delphi ModalResult).
  void Close(int result);
  bool IsPopup() const { return popup_; }

 private:
  friend class App;
  bool popup_ = false;
  std::function<void(int)> done_;
};

struct AppConfig {
  std::string title = "Menu";
  int width = 800, height = 600;
  bool fullscreen = false;
  std::string screenshot_path;  // F12 / scripted shots
  // Env prefix (SwARc game id, upper case) of the child RunChild starts,
  // for SwARc's window handoff (<ID>_WINDOW, <ID>_LINK_FD); empty: none.
  std::string child_id;
  // Upgraded look (native resolution, HD pictures, sharp text) or the
  // original 800x600 one; `upgrade_key` switches live (0 = no key).
  bool upgraded = true;
  SDL_Keycode upgrade_key = SDLK_F9;
  // Directories with HD pictures (hd.h), first wins.
  std::vector<std::string> hd_dirs;
  // Tests: draw into an offscreen frame of this size (0 = the window), so
  // screenshots show e.g. a 3024x1964 screen without taking it over.
  int render_w = 0, render_h = 0;
};

class App {
  friend class Screen;
 public:
  explicit App(Skin& skin) : skin(skin) {}
  ~App();
  bool Init(const AppConfig& cfg);
  // Runs until Quit(). Returns the exit code.
  int Run();
  void Quit(int code = 0) { quit_ = true; exit_code_ = code; }

  // Replaces the current screen (and closes any popups).
  void Show(Screen* s);
  // Opens a modal popup; `done` gets its result when it closes.
  void ShowModal(Screen* s, std::function<void(int)> done = nullptr);
  Screen* Current() const { return cur_; }
  // Runs `fn` after `ms` milliseconds (Delphi TTimer-like, one shot).
  void After(int ms, std::function<void()> fn, const void* owner = nullptr);
  void CancelTimers(const void* owner);
  // Runs a child process in this window's place (fade to black, the
  // child's window where this one was, this one back where the child's
  // ended), audio paused; returns its exit status (-1 if it could not be
  // started). `messages` gets the child's own lines on the link.
  int RunChild(const std::vector<std::string>& argv, std::vector<std::string>* messages = nullptr);
  void Screenshot(const std::string& path);
  // Upgraded or original look (AppConfig::upgraded), live.
  void SetUpgraded(bool on, bool notice = false);
  bool Upgraded() const { return cfg_.upgraded; }
  void SetFocus(Widget* w) { focus_ = w; }

  Skin& skin;
  Audio audio;
  std::function<void(Widget&)> on_button_down;  // click sound
  // Scripted input for tests: lines "<ms> click <name>", "<ms> key <name>",
  // "<ms> shot <path>", "<ms> quit".
  void LoadScript(const std::string& path);

 private:
  struct Timer {
    Uint64 at;
    std::function<void()> fn;
    const void* owner;
  };
  struct Step {
    Uint64 at;
    std::string cmd, arg;
  };
  void Draw(const std::string* shot = nullptr);
  void DrawDim(int dim);  // Draw with black over it (0-255)
  void DrawForm(Screen& s, int ox, int oy);
  Screen* Top() const { return popups_.empty() ? cur_ : popups_.back(); }
  void Origin(Screen& s, int* ox, int* oy) const;
  Widget* HitTest(int x, int y);
  void Click(Widget* w);
  void ActivateScreen(Screen* s);
  void FocusFirst(Screen& s);
  void MoveFocus(int dir);
  void Key(Screen& t, SDL_Keycode k, SDL_Keymod mod);
  void RunScript();

  SDL_Window* win_ = nullptr;
  SDL_Renderer* ren_ = nullptr;
  Painter painter_;
  HdPictures hd_;
  SDL_Texture* offscreen_ = nullptr;  // AppConfig::render_w/h
  std::string notice_;
  Uint64 notice_until_ = 0;
  Screen* cur_ = nullptr;
  std::vector<Screen*> popups_;
  std::vector<Timer> timers_;
  std::vector<Step> script_;
  Uint64 script_t0_ = 0;
  Widget* hover_ = nullptr;
  Widget* pressed_ = nullptr;
  Widget* focus_ = nullptr;
  bool quit_ = false;
  int exit_code_ = 0;
  AppConfig cfg_;
  static constexpr Uint64 kFadeMs = 300;
  int dim_ = 0;             // black over the frame (DrawDim)
  Uint64 fade_in_at_ = 0;   // fading in from black since (after RunChild)
};

}  // namespace davi
