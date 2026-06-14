// See GrpExtract.h. A .GRP is a trivial KenSilverman archive; the art lives in
// TILES000.ART.. (8-bit palette indices, column-major) and the colours in
// PALETTE.DAT (256 RGB triples at 6 bits/channel). Mirrors tools/grp_extract.py.
#include "GrpExtract.h"
#include "stb_image_write.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Little-endian readers (GRP/ART are LE); bounds-checked against the blob size.
uint32_t rd32(const std::vector<uint8_t>& b, size_t o) {
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
}
int32_t rdi32(const std::vector<uint8_t>& b, size_t o) {
    return (int32_t)rd32(b, o);
}
int16_t rdi16(const std::vector<uint8_t>& b, size_t o) {
    return (int16_t)(b[o] | (b[o + 1] << 8));
}

// One animated-tile record (frames are the consecutive tiles base..base+num).
struct Anim {
    int base, num, type, speed;
};

} // namespace

namespace grp {

ExtractResult extract(const std::string& grpPath, const std::string& outDir) {
    ExtractResult res;

    std::ifstream f(grpPath, std::ios::binary);
    if(!f) {
        res.error = "cannot open " + grpPath;
        return res;
    }
    std::vector<uint8_t> blob((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if(blob.size() < 16 || std::string(blob.begin(), blob.begin() + 12) != "KenSilverman") {
        res.error = "not a KenSilverman .GRP (bad magic)";
        return res;
    }

    // Archive table: 12-byte name + 4-byte size per entry, payloads follow in order.
    int count = rdi32(blob, 12);
    if(count < 0 || 16 + (size_t)count * 16 > blob.size()) {
        res.error = "corrupt GRP file table";
        return res;
    }
    struct Entry {
        std::string name;
        size_t off, size;
    };
    std::vector<Entry> entries;
    size_t toff = 16, payload = 16 + (size_t)count * 16;
    for(int i = 0; i < count; ++i) {
        char raw[13] = {0};
        std::copy(blob.begin() + toff, blob.begin() + toff + 12, raw);
        uint32_t size = rd32(blob, toff + 12);
        std::string name(raw);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return (char)std::toupper(c); });
        if(payload + size > blob.size()) {
            res.error = "GRP entry '" + name + "' runs past end of file";
            return res;
        }
        entries.push_back({name, payload, size});
        payload += size;
        toff += 16;
    }

    auto find = [&](const std::string& n) -> const Entry* {
        for(auto& e : entries)
            if(e.name == n) return &e;
        return nullptr;
    };

    // Palette: first 768 bytes of PALETTE.DAT, 256 RGB triples, 6-bit -> 8-bit.
    const Entry* pal = find("PALETTE.DAT");
    if(!pal || pal->size < 768) {
        res.error = "PALETTE.DAT not found (or too small) in GRP";
        return res;
    }
    uint8_t rgb[256][3];
    for(int i = 0; i < 256; ++i)
        for(int c = 0; c < 3; ++c) {
            int v = blob[pal->off + i * 3 + c];
            rgb[i][c] = (uint8_t)(v * 255 / 63);
        }

    std::vector<const Entry*> arts;
    for(auto& e : entries)
        if(e.name.size() > 4 && e.name.rfind("TILES", 0) == 0 &&
           e.name.compare(e.name.size() - 4, 4, ".ART") == 0)
            arts.push_back(&e);
    if(arts.empty()) {
        res.error = "no TILESxxx.ART files in GRP";
        return res;
    }
    std::sort(arts.begin(), arts.end(),
              [](const Entry* a, const Entry* b) { return a->name < b->name; });

    std::error_code ec;
    fs::create_directories(outDir, ec);
    if(ec) {
        res.error = "cannot create " + outDir + ": " + ec.message();
        return res;
    }

    std::vector<Anim> anims;
    std::vector<uint8_t> rgba;
    for(const Entry* art : arts) {
        size_t base = art->off, end = art->off + art->size;
        if(art->size < 16) continue;
        int start = rdi32(blob, base + 8), last = rdi32(blob, base + 12);
        int n = last - start + 1;
        if(n <= 0) continue;
        size_t oSizx = base + 16, oSizy = oSizx + 2 * n, oPic = oSizy + 2 * n, o = oPic + 4 * n;
        if(o > end) continue; // malformed header
        for(int i = 0; i < n; ++i) {
            int w = rdi16(blob, oSizx + 2 * i), h = rdi16(blob, oSizy + 2 * i);
            int tile = start + i;
            if(w <= 0 || h <= 0) continue;     // empty slot: no pixel data stored
            if(o + (size_t)w * h > end) break; // truncated art
            const uint8_t* data = &blob[o];
            o += (size_t)w * h;

            rgba.assign((size_t)w * h * 4, 0);
            for(int x = 0; x < w; ++x) { // source is column-major
                int col = x * h;
                for(int y = 0; y < h; ++y) {
                    uint8_t idx = data[col + y];
                    size_t j = ((size_t)y * w + x) * 4;
                    if(idx == 255) { // BUILD transparency key -> opaque magenta
                        rgba[j] = 255;
                        rgba[j + 1] = 0;
                        rgba[j + 2] = 255;
                        rgba[j + 3] = 255;
                    } else {
                        rgba[j] = rgb[idx][0];
                        rgba[j + 1] = rgb[idx][1];
                        rgba[j + 2] = rgb[idx][2];
                        rgba[j + 3] = 255;
                    }
                }
            }
            char name[32];
            std::snprintf(name, sizeof name, "tile%04d.png", tile);
            std::string path = (fs::path(outDir) / name).string();
            if(stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4)) res.tiles++;

            // picanm: bits 0-5 frame count, 6-7 type (1 osc, 2 fwd, 3 bwd), 24-27 speed.
            uint32_t pa = rd32(blob, oPic + 4 * i);
            int num = pa & 0x3f, type = (pa >> 6) & 3, speed = (pa >> 24) & 0xf;
            if(num && type) anims.push_back({tile, num, type, speed});
        }
    }

    std::sort(anims.begin(), anims.end(),
              [](const Anim& a, const Anim& b) { return a.base < b.base; });
    std::ofstream af((fs::path(outDir) / "anim.txt").string());
    af << "# animated tiles: base_tile num_frames type(1=osc 2=fwd 3=bwd) speed\n";
    for(auto& a : anims) af << a.base << ' ' << a.num << ' ' << a.type << ' ' << a.speed << '\n';
    res.anims = (int)anims.size();

    res.ok = true;
    return res;
}

std::string findGrp(const std::string& dir) {
    std::error_code ec;
    if(!fs::is_directory(dir, ec)) return "";
    std::string best;
    for(auto& e : fs::directory_iterator(dir, ec)) {
        if(!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if(ext == ".grp") { // prefer the lowest-sorting name for determinism
            std::string p = e.path().string();
            if(best.empty() || p < best) best = p;
        }
    }
    return best;
}

} // namespace grp
