// The world: sectors (2D polygons with floor/ceiling heights), sprites, and the
// player start. Plain data — loading/saving lives in Map.cpp.
#pragma once
#include "Vec2.h"
#include <vector>
#include <string>
#include <optional>
#include <cstdint>

// Per-surface texture mapping: sample coord = world / scale + offset.
// Larger scale => the texture appears bigger (fewer repeats). us/uo act along a
// wall (or world X for floors); vs/vo act vertically (or world Y for floors).
struct TexXform {
    float us = 1, vs = 1, uo = 0, vo = 0;
    bool isDefault() const { return us == 1 && vs == 1 && uo == 0 && vo == 0; }
};

// A sector is a CCW loop of vertices. Wall s runs from vert[s] to vert[s+1];
// neigh[s] is the sector on the far side of that wall, or -1 if solid.
struct Sector {
    float floor = 0, ceil = 0;
    std::vector<Vec2>      vert;
    std::vector<int>       neigh;
    std::vector<TexXform>  wallTex;          // parallel to vert/neigh
    TexXform               floorTex, ceilTex;
    uint32_t floorCol = 0, ceilCol = 0, wallCol = 0;
};

// A flat camera-facing billboard. z is the feet height; radius is half the
// on-screen width in world units.
struct Sprite {
    Vec2 pos;
    float z = 0, radius = 0, height = 0;
    uint32_t col = 0;
};

struct Map {
    std::vector<Sector> sectors;
    std::vector<Sprite> sprites;
    Vec2  playerStart;
    int   startSector = 0;
    float startAngle  = 0;   // radians
};

std::optional<Map> loadMap(const std::string& path);
bool               saveMap(const Map& map, const std::string& path);
