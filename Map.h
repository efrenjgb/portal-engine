// The world: sectors (2D polygons with floor/ceiling heights), sprites, and the
// player start. Plain data — loading/saving lives in Map.cpp.
#pragma once
#include "Vec2.h"
#include <vector>
#include <string>
#include <optional>
#include <cstdint>

// Per-surface texture mapping: sample coord = world / scale + offset.
// Larger scale => the texture appears bigger (fewer repeats). uScale/uOffset act
// along a wall (or world X for floors); vScale/vOffset act vertically (or world Y).
struct TextureTransform {
    float uScale = 1, vScale = 1, uOffset = 0, vOffset = 0;
    bool isDefault() const { return uScale == 1 && vScale == 1 && uOffset == 0 && vOffset == 0; }
};

// A sector is a CCW loop of vertices. Wall s runs from vertices[s] to vertices[s+1];
// neighbors[s] is the sector on the far side of that wall, or -1 if solid.
struct Sector {
    float floor = 0, ceiling = 0;
    std::vector<Vec2>              vertices;
    std::vector<int>              neighbors;
    std::vector<TextureTransform> wallTextures;     // parallel to vertices/neighbors
    std::vector<int>              wallTextureIds;    // image-texture index, -1 = procedural
    std::vector<float>            wallLight;         // per-wall brightness (1 = normal)
    TextureTransform              floorTexture, ceilingTexture;
    int                           floorTextureId = -1, ceilingTextureId = -1;
    bool                          ceilingIsSky = false;   // render ceiling as a parallax sky
    uint32_t floorColor = 0, ceilingColor = 0, wallColor = 0;
    float                         floorLight = 1.0f, ceilingLight = 1.0f;   // per-surface light
};

// A flat camera-facing billboard. z is the feet height; radius is half the
// on-screen width in world units.
struct Sprite {
    Vec2 position;
    float z = 0, radius = 0, height = 0;
    uint32_t color = 0;
    int textureId = -1;     // image texture index, or -1 for the procedural billboard
};

struct Map {
    std::vector<Sector>      sectors;
    std::vector<Sprite>      sprites;
    std::vector<std::string> textures;   // image files; index == texture id
    Vec2  playerStart;
    int   startSector = 0;
    float startAngle  = 0;   // radians
};

std::optional<Map> loadMap(const std::string& path);
bool               saveMap(const Map& map, const std::string& path);
