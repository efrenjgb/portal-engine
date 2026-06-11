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
    std::vector<Vec2> vertices;
    std::vector<int> neighbors;
    std::vector<TextureTransform> wallTextures; // parallel to vertices/neighbors
    std::vector<int> wallTextureIds;            // image-texture index, -1 = procedural
    std::vector<float> wallLight;               // per-wall brightness (1 = normal)

    // A sector is a CCW outer polygon plus zero or more inner hole loops (CW) for
    // cutouts (columns/pits/platforms). loopStart[k] is the first vertex index of
    // loop k; loop 0 is the outer boundary and always starts at 0. The parallel
    // arrays above stay one entry per wall across every loop. wallEnd(w) gives the
    // far vertex of wall w, wrapping at its own loop's end (not the next loop's).
    std::vector<int> loopStart{0};
    int loopOf(int w) const { // which loop vertex/wall w belongs to
        for(int k = (int)loopStart.size() - 1; k >= 0; --k)
            if(w >= loopStart[k]) return k;
        return 0;
    }
    int loopBegin(int k) const { return loopStart[k]; }
    int loopEnd(int k) const {
        return (k + 1 < (int)loopStart.size()) ? loopStart[k + 1] : (int)vertices.size();
    }
    int loopSize(int k) const { return loopEnd(k) - loopBegin(k); }
    int wallEnd(int w) const { // far vertex of wall w, wrapping within w's own loop
        int k = loopOf(w);
        return (w + 1 < loopEnd(k)) ? w + 1 : loopBegin(k);
    }
    TextureTransform floorTexture, ceilingTexture;
    int floorTextureId = -1, ceilingTextureId = -1;
    bool ceilingIsSky = false; // render ceiling as a parallax sky
    uint32_t floorColor = 0, ceilingColor = 0, wallColor = 0;
    float floorLight = 1.0f, ceilingLight = 1.0f; // per-surface light

    // Door / lift: a sector that animates one surface between two heights when the
    // player presses `use`. A DOOR (mover==1) slides its ceiling between the floor
    // (closed) and moverRest (the authored ceiling = open). A LIFT (mover==2)
    // slides its floor between moverRest (the authored floor = down) and the
    // highest neighbouring floor (up). The closed-door height gap is < player
    // height, so the existing collision blocks you until it opens — no special
    // case needed. mover/moverSpeed are authored (map `mover` line); moverRest and
    // moverOpen are runtime state.
    int mover = 0;           // 0 none, 1 door (ceiling), 2 lift (floor)
    float moverSpeed = 3.0f; // units / second
    float moverRest = 0.0f;  // authored rest height of the moved surface
    bool moverOpen = false;  // current target: door open / lift up
};

// A flat camera-facing billboard. z is the feet height; radius is half the
// on-screen width in world units.
struct Sprite {
    Vec2 position;
    float z = 0, radius = 0, height = 0;
    uint32_t color = 0;
    int textureId = -1; // image texture index, or -1 for the procedural billboard
};

struct Map {
    std::vector<Sector> sectors;
    std::vector<Sprite> sprites;
    std::vector<std::string> textures; // image files; index == texture id
    Vec2 playerStart;
    int startSector = 0;
    float startAngle = 0; // radians
};

std::optional<Map> loadMap(const std::string& path);
bool saveMap(const Map& map, const std::string& path);
