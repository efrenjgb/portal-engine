// Built-in extractor for BUILD-engine .GRP archives (Duke Nukem 3D shareware /
// full): decode the palette + TILESxxx.ART art and write one PNG per tile, plus
// the anim.txt sidecar the engine reads for animated textures. This is the C++
// port of tools/grp_extract.py, so a release binary can produce textures/duke/
// with no Python toolchain. SDL-free.
#pragma once
#include <string>

namespace grp {

struct ExtractResult {
    bool ok = false;
    int tiles = 0; // PNGs written
    int anims = 0; // animated-tile entries recorded in anim.txt
    std::string error;
};

// Read `grpPath`, decode every TILESxxx.ART against PALETTE.DAT, and write one
// PNG per non-empty tile into `outDir` (created if needed) named tileNNNN.png,
// plus `outDir/anim.txt`. BUILD's transparency key (palette index 255) is written
// as opaque magenta — the engine's colour-key (see Texture.h kColorKey).
ExtractResult extract(const std::string& grpPath, const std::string& outDir);

// First *.grp (case-insensitive) in `dir`, or "" if none — drives the drop-in
// auto-extract on startup.
std::string findGrp(const std::string& dir);

} // namespace grp
