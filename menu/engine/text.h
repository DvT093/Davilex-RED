// Text drawing with FreeType, laid out the way GDI's DrawText does it for a
// Delphi TLabel / button caption (em height in pixels, tmHeight line
// spacing, word wrap on spaces). Strings are Windows-1252.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace davi {

class Font {
 public:
  Font();
  ~Font();
  Font(const Font&) = delete;
  Font& operator=(const Font&) = delete;
  bool Load(std::vector<uint8_t> ttf);
  bool LoadFile(const std::string& path);
  bool Ok() const { return face_ != nullptr; }
  // Synthetic bold (GDI fsBold on a face without a bold style).
  bool embolden = false;

  // Pixel em size `px` (Delphi Font.Height = -px; Size pt -> px = pt*96/72).
  int Width(const std::string& s, int px);
  int LineHeight(int px);  // GDI tmHeight
  int Ascent(int px);
  // Draws one line with its top at y (GDI TextOut).
  void DrawLine(SDL_Surface* dst, int x, int y, const std::string& s, int px, SDL_Color c,
                const SDL_Rect* clip = nullptr);

  // Scaled drawing (paint.h): sizes in 26.6 pixels (px * 64), any size.
  float WidthF(const std::string& s, long size26);
  // One line in colour `c` on transparent (ARGB8888, caller frees); the pen
  // starts at x = *origin_x, the baseline is at y = *baseline.
  SDL_Surface* RenderLine(const std::string& s, long size26, SDL_Color c, int* origin_x, int* baseline);

 private:
  void SetSize(int px);
  void SetSize26(long size26);
  std::vector<uint8_t> data_;
  void* lib_ = nullptr;   // FT_Library
  void* face_ = nullptr;  // FT_Face
  long cur_size26_ = 0;
};

enum class HAlign { kLeft, kCenter, kRight };
enum class VAlign { kTop, kCenter, kBottom };

struct TextStyle {
  Font* font = nullptr;
  int px = 16;
  SDL_Color color{0, 0, 0, 255};
  HAlign halign = HAlign::kLeft;
  VAlign valign = VAlign::kTop;
  bool wrap = false;
};

// Breaks `s` into lines that fit `width` (only on spaces and '\n').
std::vector<std::string> WrapText(Font& f, const std::string& s, int px, int width, bool wrap);
// Draws `s` inside rect r (clipped to it), like DrawText.
void DrawText(SDL_Surface* dst, const SDL_Rect& r, const std::string& s, const TextStyle& st);

// Windows-1252 byte -> Unicode code point.
uint32_t Cp1252(unsigned char c);
// UTF-8 -> Windows-1252 (for text typed by the user), unknown -> '?'.
std::string Utf8ToCp1252(const std::string& s);

}  // namespace davi
