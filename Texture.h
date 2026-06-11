// An image texture. loadImage() handles PNG/JPG/BMP/TGA via stb_image, and
// falls back to a tiny built-in PPM (P6) reader.
#pragma once
#include "Vec2.h"
#include <vector>
#include <string>
#include <optional>
#include <cstdint>
#include <cmath>

// BUILD's transparency key (palette index 255), which tools/grp_extract.py
// exports as opaque magenta. Treated as "clear" on every render path — walls,
// floors/ceilings and sprites — so a masked tile reads through instead of
// showing a black hole. Alpha < 128 is also honoured for genuine RGBA art.
static constexpr uint32_t kColorKey = 0xFF00FFu; // compared on the low 24 bits
inline bool isClear(uint32_t px) {
    return (px & 0xFFFFFFu) == kColorKey || (px >> 24) < 128;
}

struct Texture {
    int width = 0, height = 0;
    std::vector<uint32_t> pixels; // 0xFFRRGGBB

    // Sample with wrapping. su/sv are in tile units (1.0 == one full image).
    uint32_t at(float su, float sv) const {
        float fu = su - std::floor(su);
        float fv = sv - std::floor(sv);
        int ix = (int)(fu * width);
        if(ix >= width) ix = width - 1;
        int iy = (int)(fv * height);
        if(iy >= height) iy = height - 1;
        return pixels[(size_t)iy * width + ix];
    }

    // Sample with clamping (no wrap) — for sprites/billboards, where su/sv = 1.0 at
    // the trailing edge must stay on the last texel instead of wrapping to texel 0.
    uint32_t atClamp(float su, float sv) const {
        int ix = (int)(su * width);
        int iy = (int)(sv * height);
        if(ix < 0) ix = 0;
        else if(ix >= width) ix = width - 1;
        if(iy < 0) iy = 0;
        else if(iy >= height) iy = height - 1;
        return pixels[(size_t)iy * width + ix];
    }
};

// One animated texture (BUILD picanm): the renderer cycles `frames` (texture
// ids; frames[0] is the base tile) over time. Built by main.cpp from the
// textures/duke/anim.txt sidecar that tools/grp_extract.py writes.
struct TexAnim {
    int type = 0;  // 1 = oscillating, 2 = forward, 3 = backward
    int speed = 0; // BUILD animspeed: larger = slower (frame every (1<<speed) ticks)
    std::vector<int> frames;
};

std::optional<Texture> loadImage(const std::string& path); // PNG/JPG/BMP/TGA, or PPM
std::optional<Texture> loadPPM(const std::string& path);   // built-in fallback
