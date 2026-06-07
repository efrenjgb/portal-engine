#include "Texture.h"
#include "stb_image.h"          // declarations only; impl is in stb_image_impl.cpp
#include <fstream>
#include <cstdio>

// Load PNG/JPG/BMP/TGA via stb_image; fall back to PPM (which stb doesn't read).
std::optional<Texture> loadImage(const std::string& path){
    int w = 0, h = 0, n = 0;
    unsigned char* d = stbi_load(path.c_str(), &w, &h, &n, 3);   // force RGB
    if(d){
        Texture t; t.width = w; t.height = h; t.pixels.resize((size_t)w * h);
        for(size_t i = 0; i < t.pixels.size(); ++i)
            t.pixels[i] = 0xFF000000u | (d[i*3] << 16) | (d[i*3+1] << 8) | d[i*3+2];
        stbi_image_free(d);
        return t;
    }
    return loadPPM(path);
}

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

    Texture t; t.width = w; t.height = h; t.pixels.resize((size_t)w * h);
    for(size_t i = 0; i < t.pixels.size(); ++i)
        t.pixels[i] = 0xFF000000u | (buf[i*3] << 16) | (buf[i*3+1] << 8) | buf[i*3+2];
    return t;
}
