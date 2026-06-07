// An image texture. loadImage() handles PNG/JPG/BMP/TGA via stb_image, and
// falls back to a tiny built-in PPM (P6) reader.
#pragma once
#include "Vec2.h"
#include <vector>
#include <string>
#include <optional>
#include <cstdint>
#include <cmath>

struct Texture {
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;   // 0xFFRRGGBB

    // Sample with wrapping. su/sv are in tile units (1.0 == one full image).
    uint32_t at(float su, float sv) const {
        float fu = su - std::floor(su);
        float fv = sv - std::floor(sv);
        int ix = (int)(fu * width);  if(ix >= width)  ix = width - 1;
        int iy = (int)(fv * height); if(iy >= height) iy = height - 1;
        return pixels[(size_t)iy * width + ix];
    }
};

std::optional<Texture> loadImage(const std::string& path);  // PNG/JPG/BMP/TGA, or PPM
std::optional<Texture> loadPPM(const std::string& path);    // built-in fallback
