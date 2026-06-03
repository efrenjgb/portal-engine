#include "Texture.h"
#include <fstream>
#include <cstdio>

// Minimal binary PPM (P6) reader. Assumes the simple header our generator
// writes ("P6\nW H\n255\n") with no embedded comments.
std::optional<Texture> loadPPM(const std::string& path){
    std::ifstream f(path, std::ios::binary);
    if(!f){ fprintf(stderr, "texture: cannot open '%s'\n", path.c_str()); return std::nullopt; }

    std::string magic; int w = 0, h = 0, maxv = 0;
    f >> magic >> w >> h >> maxv;
    if(magic != "P6" || w <= 0 || h <= 0 || maxv != 255){
        fprintf(stderr, "texture: '%s' is not a 8-bit P6 PPM\n", path.c_str()); return std::nullopt;
    }
    f.get();                                  // consume the single whitespace before the data

    std::vector<unsigned char> buf((size_t)w * h * 3);
    f.read((char*)buf.data(), (std::streamsize)buf.size());
    if(!f){ fprintf(stderr, "texture: '%s' truncated\n", path.c_str()); return std::nullopt; }

    Texture t; t.w = w; t.h = h; t.px.resize((size_t)w * h);
    for(size_t i = 0; i < t.px.size(); ++i)
        t.px[i] = 0xFF000000u | (buf[i*3] << 16) | (buf[i*3+1] << 8) | buf[i*3+2];
    return t;
}
