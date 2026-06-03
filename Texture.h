// An image texture loaded from a PPM (P6) file. (PPM keeps the loader tiny and
// dependency-free; swap loadPPM for a stb_image call to support PNG/JPG.)
#pragma once
#include "Vec2.h"
#include <vector>
#include <string>
#include <optional>
#include <cstdint>
#include <cmath>

struct Texture {
    int w = 0, h = 0;
    std::vector<uint32_t> px;       // 0xFFRRGGBB

    // Sample with wrapping. su/sv are in tile units (1.0 == one full image).
    uint32_t at(float su, float sv) const {
        float fu = su - std::floor(su);
        float fv = sv - std::floor(sv);
        int ix = (int)(fu * w); if(ix >= w) ix = w - 1;
        int iy = (int)(fv * h); if(iy >= h) iy = h - 1;
        return px[(size_t)iy * w + ix];
    }
};

std::optional<Texture> loadPPM(const std::string& path);
