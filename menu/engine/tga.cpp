// Truevision TGA (types 2 and 10: true colour, raw or RLE; 24 or 32 bit) as
// an ARGB8888 surface. 32-bit files keep their alpha (the Europe Racer
// menu's anti-aliased buttons and labels rely on it).
#include <cstring>

#include "engine/assets.h"

namespace davi {

SDL_Surface* LoadTga(const std::vector<uint8_t>& b) {
  if (b.size() < 18) return nullptr;
  int idlen = b[0], cmap = b[1], type = b[2];
  int w = b[12] | (b[13] << 8), h = b[14] | (b[15] << 8), bpp = b[16], desc = b[17];
  if (cmap != 0 || (type != 2 && type != 10) || (bpp != 24 && bpp != 32) || w <= 0 || h <= 0) return nullptr;
  int px = bpp / 8;
  size_t p = 18 + idlen;
  std::vector<uint8_t> pix(size_t(w) * h * px);
  if (type == 2) {
    if (b.size() < p + pix.size()) return nullptr;
    std::memcpy(pix.data(), b.data() + p, pix.size());
  } else {
    size_t o = 0;
    while (o < pix.size() && p < b.size()) {
      int hdr = b[p++], n = (hdr & 0x7f) + 1;
      if (hdr & 0x80) {
        if (p + px > b.size()) break;
        for (int i = 0; i < n && o < pix.size(); i++, o += px) std::memcpy(&pix[o], &b[p], px);
        p += px;
      } else {
        size_t len = size_t(n) * px;
        if (p + len > b.size()) break;
        len = std::min(len, pix.size() - o);
        std::memcpy(&pix[o], &b[p], len);
        p += len;
        o += len;
      }
    }
  }
  // Some files declare 32 bit with alpha but leave it 0 everywhere (Europe
  // Racer's dfimg*): the menu draws those opaque.
  bool has_alpha = false;
  if (px == 4)
    for (size_t i = 3; i < pix.size() && !has_alpha; i += 4) has_alpha = pix[i] != 0;
  SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return nullptr;
  bool top_down = desc & 0x20;
  for (int y = 0; y < h; y++) {
    int sy = top_down ? y : h - 1 - y;
    auto* dst = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(s->pixels) + size_t(y) * s->pitch);
    const uint8_t* src = &pix[size_t(sy) * w * px];
    for (int x = 0; x < w; x++, src += px) {
      uint32_t a = has_alpha ? src[3] : 255;
      dst[x] = (a << 24) | (uint32_t(src[2]) << 16) | (uint32_t(src[1]) << 8) | src[0];
    }
  }
  SDL_SetSurfaceBlendMode(s, has_alpha ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
  return s;
}

}  // namespace davi
