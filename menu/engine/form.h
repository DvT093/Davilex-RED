// A menu screen built from a Delphi form: the original layout (DFM in the
// user's EXE), the captions/fonts from the game's MenuCaps-style ini, and an
// optional mod layout file that moves, hides or adds components.
#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/assets.h"
#include "engine/dfm.h"
#include "engine/ini.h"
#include "engine/text.h"

namespace davi {

// TDaviGraphicDef: one bitmap per button state.
struct GraphicDef {
  enum State { kUp, kDown, kFocused, kHovered, kDisabled, kCount };
  SDL_Surface* img[kCount] = {};
  int w = 0, h = 0;
  std::string caption_rect;  // OnGetCaptionRect handler name ("" = whole button)
  SDL_Rect caption{0, 0, 0, 0};  // TGDIButtonDefinition.CaptionRect (0 = whole button)
  // TGDIButtonDefinition.Font: the caption font of every button using it.
  bool has_font = false;
  int font = 1, font_px = 11;
  SDL_Color font_color{0, 0, 0, 255};
  bool left = false;  // Alignment = taLeftJustify
  int alpha = 255;    // Translucent -> TranslucencyFactor
  SDL_Surface* Get(State s) const;
};

struct FontSpec {
  int font = 1;  // index into Skin::fonts (MenuCaps "Font=")
  int px = 16;
  SDL_Color color{0, 0, 0, 255};
};

struct Widget {
  // kThumb (port, layout Class=TDaviThumb): a clickable picture in a rounded
  // frame, `checked` = the chosen one of its `group` (arrow keys walk the
  // group); `caption` when there is no picture.
  enum Kind { kOther, kImage, kLabel, kButton, kCheckBox, kRadio, kEdit, kSlider, kThumb };
  Kind kind = kOther;
  std::string cls, name;
  SDL_Rect r{0, 0, 0, 0};
  bool visible = true, enabled = true;
  int tab_order = 1000;
  bool tab_stop = true, is_cancel = false, is_default = false;
  // Text (labels, buttons, edits).
  std::string caption;
  FontSpec font;
  SDL_Color disabled_color{75, 97, 99, 255};
  bool wrap = false, autosize = true, transparent = true;
  HAlign halign = HAlign::kLeft;
  VAlign valign = VAlign::kTop;
  // Edits.
  SDL_Color color{255, 255, 255, 255};
  bool border = true, select_all = false;
  int max_len = 0;
  // Images.
  SDL_Surface* picture = nullptr;  // not owned
  SDL_Surface* owned = nullptr;    // owned canvas (drawn on by flow code)
  bool stretch = false, center = false, pic_transparent = false;
  int key_rgb = -1;  // transparent colour 0xRRGGBB (default: bottom-left pixel)
  // Buttons.
  const GraphicDef* def = nullptr;
  bool checked = false;  // check boxes / radio buttons
  int group = 0;
  std::string onclick;   // DFM OnClick handler name
  // Sliders (TDaviGraphicSlider): picture = track, def = needle.
  int min = 0, max = 100, pos = 0, margin = 0;
  // Draw order (TGDI ZValue: higher is on top; equal keeps DFM order).
  int z = 0;
  int alpha = 255;  // TGDIGraphic translucency
  std::string parent;  // name of the containing component ("" = the form)
  Widget() = default;
  Widget(const Widget&) = delete;
  Widget& operator=(const Widget&) = delete;
  ~Widget();
  SDL_Surface* Pic() const { return owned ? owned : picture; }
  // Makes an owned canvas of the widget's size (copy of `bg` under it).
  SDL_Surface* Canvas(SDL_Surface* bg);
};

// Shared look: fonts, button definitions, caption strings.
struct Skin {
  DelphiExe* exe = nullptr;
  Assets* assets = nullptr;
  LayeredIni caps;                          // MenuCaps.ini (+ mod layer)
  std::map<int, std::unique_ptr<Font>> fonts;  // MenuCaps Font index -> font
  std::map<std::string, std::unique_ptr<GraphicDef>> defs;  // "TData.defBtn"
  std::map<std::string, std::vector<SDL_Surface*>> owned_bmps;
  // OnGetCaptionRect handlers by name: button rect (0-based) -> caption rect.
  std::map<std::string, std::function<SDL_Rect(const Widget&)>> caption_rects;
  std::string data_module = "TData";  // where "Data.xxx" references live
  // TGDI (Davilex Menulib) forms. The DFM leaves every graphic name empty;
  // the original program sets them in code, so the game supplies them:
  // def_link("TOwner.definition", state) -> image name for a TGDIButtonDefinition
  // state (GraphicDef::State), graphic_name(form, component) -> image for a
  // TGDIGraphic/TGDISlider. "" = no image.
  std::function<std::string(const std::string& def, int state)> def_link;
  std::function<std::string(const std::string& form, const std::string& comp)> graphic_name;
  // Caption text from the layout ini (e.g. an LT_ key -> language text).
  std::function<std::string(const std::string& value)> text;
  // Layout ini font name -> Skin::fonts index (default: 1 for every name).
  std::map<std::string, int> font_names;
  // Layout ini "Font=" when the ini sets none, and whether the number is a
  // Skin::fonts index as is (A2 Racer IV) or 1 = font 1, else font 2 (VR).
  int default_font = 1;
  bool raw_font_index = false;
  // TGDI components without a layout-ini caption: keep the DFM caption
  // (Europe Racer: an LT_ key) or take [DEFAULT] Caption like other
  // components (A2 Racer IV: its DFM captions are component names).
  bool tgdi_dfm_caption = true;
  // Background bitmap name for a form class ("" = none), see Screen.
  std::function<std::string(const std::string& cls)> background_name;

  const GraphicDef* Def(const std::string& form, const std::string& ref);
  // font_names lookup ("<name> bold" first when bold); 1 if unknown.
  int FontIndex(const std::string& name, bool bold) const;
  int FontIndex(const std::string& name, const Value* style) const;
  Font* GetFont(int idx);
  ~Skin();
};

class Form {
 public:
  std::string cls;
  int w = 800, h = 600;
  std::vector<std::unique_ptr<Widget>> widgets;  // z-order: first = bottom
  SDL_Surface* background = nullptr;             // imgBackGround picture

  // Builds the form `cls` from the EXE. `mod_layout` is an optional ini path
  // (see CLAUDE.md "Menu mods") applied last.
  bool Build(Skin& skin, const std::string& cls, const std::string& mod_layout);
  Widget* Get(const std::string& name);  // nullptr if absent
  Widget& W(const std::string& name);     // a dummy if absent (logged)
  void SetCaption(const std::string& name, const std::string& text);
  void SetVisible(const std::string& name, bool v);
  void SetEnabled(const std::string& name, bool v);
  void SetPicture(const std::string& name, SDL_Surface* s);

 private:
  std::unique_ptr<Widget> dummy_;
  void Add(Skin& skin, const Component& c, int ox, int oy);
};

}  // namespace davi
