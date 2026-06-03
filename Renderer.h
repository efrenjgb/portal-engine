// The software renderer: portal flood for walls/floors/ceilings, billboard
// sprites with a per-pixel depth buffer, and the 2D minimap overlay. Owns the
// pixel and depth buffers. Knows nothing about SDL — it just fills a buffer.
#pragma once
#include "Map.h"
#include "Camera.h"
#include <vector>
#include <cstdint>

class Renderer {
public:
    static constexpr int W = 960, H = 600;

    Renderer();

    void clear(uint32_t bg);
    void renderWorld(const Map& map, const Camera& cam, int playerSector);
    void drawSprites(const Map& map, const Camera& cam);
    void drawMinimap(const Map& map, const Camera& cam, int pickSector,
                     Vec2 start, float startAngle);
    void crosshair(uint32_t col);

    const uint32_t* pixels() const { return fb_.data(); }

private:
    float F_;                          // focal length in pixels
    std::vector<uint32_t> fb_;         // W*H colour buffer
    std::vector<float>    zbuf_;       // W*H depth buffer
    std::vector<int>      ytop_, ybot_;// per-column open vertical window

    void putpx(int x, int y, uint32_t c);
    void line2d(int x0, int y0, int x1, int y1, uint32_t c);
    void wallSpan(int x, float yTopf, float yBotf, float vTop, float vBot,
                  int clipT, int clipB, float u, uint32_t base, float depth);
    void planeSpan(const Camera& cam, int x, int y0, int y1, float pz, uint32_t base);
};
