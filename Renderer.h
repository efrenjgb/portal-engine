// The software renderer: portal flood for walls/floors/ceilings, billboard
// sprites with a per-pixel depth buffer, and the 2D minimap overlay. Owns the
// pixel and depth buffers. Knows nothing about SDL — it just fills a buffer.
#pragma once
#include "Map.h"
#include "Camera.h"
#include "Texture.h"
#include <vector>
#include <cstdint>

// Identifies one editable thing under the crosshair: a sector surface or a sprite.
struct SurfaceRef {
    enum Kind { None = 0, Floor = 1, Ceiling = 2, Wall = 3, Sprite = 4 };
    int sector = -1;
    Kind kind = None;
    int wall = -1;   // only meaningful when kind == Wall
    int sprite = -1; // only meaningful when kind == Sprite
};

class Renderer {
  public:
    const int W, H; // framebuffer size, chosen at construction (e.g. --res WxH)

    Renderer(int w, int h);

    // Loaded image textures, indexed by surface texture id (-1 = procedural).
    void setTextures(const std::vector<Texture>* t) { textures_ = t; }

    // Animated textures: `anims` are the cycles, `animOf` maps each texture id to
    // an index in `anims` (-1 = not animated). imageFor() then resolves an
    // animated id to its current frame. advanceAnim(dt) drives the clock.
    void setAnimations(const std::vector<TexAnim>* anims, const std::vector<int>* animOf) {
        anims_ = anims;
        animOf_ = animOf;
    }
    void advanceAnim(float dt) { animTime_ += dt; }

    void clear(uint32_t bg);
    void renderWorld(const Map& map, const Camera& cam, int playerSector);
    void drawSprites(const Map& map, const Camera& cam);
    void drawMinimap(const Map& map, const Camera& cam, SurfaceRef aim, Vec2 start,
                     float startAngle);
    void crosshair(uint32_t col);

    // HUD text via a built-in 5x7 bitmap font (uppercased; digits + A-Z + a few
    // punctuation). Advances 6*scale px per glyph. Returns the drawn width in px.
    int drawText(int x, int y, const char* text, uint32_t color, int scale = 2);

    // Full-screen texture picker: a BR_COLS x BR_ROWS grid of thumbnails for the
    // given page, with hoverCell (0..cells-1, or -1) highlighted. Layout below is
    // shared with main.cpp's hit-testing.
    static constexpr int BR_COLS = 8, BR_ROWS = 5, BR_HEAD = 28;
    // `view` lists which pool indices to show (the active filter); a cell maps to
    // view[page*cells + cell] -> pool[...]. filterName labels the header.
    void drawTextureBrowser(const std::vector<Texture>& pool, const std::vector<int>& view,
                            int page, int hoverCell, const char* filterName);

    // Full-screen top-down map editor. (sc, ox, oy) map world->screen as
    // sx = ox + wx*sc, sy = oy - wy*sc. hov*/hw* highlight a vertex / wall.
    void drawMapEditor(const Map& map, float sc, float ox, float oy, int hovSec, int hovVert,
                       int hwSec, int hwWall, Vec2 playerPos, float playerAng, int hovSprite = -1,
                       float gridSize = 1.0f);

    // Overlay the in-progress polygon while drawing a new sector (2D editor):
    // placed points, the edges between them, and a rubber-band to the cursor.
    void drawPendingSector(const std::vector<Vec2>& pts, float sc, float ox, float oy, int mx,
                           int my);

#if EDITOR
    // What surface did the last renderWorld draw at this pixel? (kind==None if
    // nothing / background). Decoded from the per-pixel pick buffer.
    SurfaceRef pickAt(int x, int y) const;
#endif

    const uint32_t* pixels() const { return frameBuffer_.data(); }

  private:
    float focalLength_; // focal length in pixels
    const std::vector<Texture>* textures_ = nullptr;
    const std::vector<TexAnim>* anims_ = nullptr;
    const std::vector<int>* animOf_ = nullptr; // texId -> index in anims_, or -1
    float animTime_ = 0.0f;                    // seconds, advanced by advanceAnim
    std::vector<uint32_t> frameBuffer_;        // W*H colour buffer
    std::vector<float> depthBuffer_;           // W*H depth buffer
#if EDITOR
    std::vector<uint32_t> pickBuffer_; // W*H packed surface IDs (editor only)
#endif
    std::vector<int> columnTop_, columnBottom_; // per-column open vertical window

    const Texture* imageFor(int texId) const;
    int animFrame(int texId) const; // resolve an animated texId to its current frame
    void putPixel(int x, int y, uint32_t c);
    void drawLine(int x0, int y0, int x1, int y1, uint32_t c);
    void wallSpan(int x, float yTopf, float yBotf, float vTop, float vBot, int clipT, int clipB,
                  float u, uint32_t base, float depth, uint32_t surf, const TextureTransform& tx,
                  int texId, float light = 1.0f);
    void planeSpan(const Camera& cam, int x, int y0, int y1, float pz, uint32_t base, uint32_t surf,
                   const TextureTransform& tx, int texId, float light = 1.0f,
                   const Sector* holes = nullptr);
    void skySpan(int x, int y0, int y1, uint32_t base, int texId, uint32_t surf);
};
