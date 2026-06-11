// Text map loader/writer. Format (one record per line, '#' starts a comment):
//   player <x> <y> <sector> <angle_deg>
//   sector <floor> <ceil> <floorcol> <ceilcol> <wallcol> [floorlight ceillight]
//                                                         colours = RRGGBB hex
//     wall <x> <y> <neighbour> [us vs uo vo [texId [light]]]   CCW; -1 = solid
//     loop                      begin an inner hole loop (cutout) in this sector
//     mover door|lift [speed]   makes the current sector a door/lift (see Sector)
//   sprite <x> <y> <z> <radius> <height> <col> [texId]
#include "Map.h"
#include <cstdio>
#include <cstring>

std::optional<Map> loadMap(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if(!f) {
        fprintf(stderr, "cannot open map '%s'\n", path.c_str());
        return std::nullopt;
    }

    Map m;
    char line[256];
    int cur = -1, ln = 0;
    float wallLightDefault = 1.0f; // applied to walls of a legacy single-light sector
    bool havePlayer = false;

    while(fgets(line, sizeof line, f)) {
        ++ln;
        char* p = line;
        while(*p == ' ' || *p == '\t') ++p;
        if(*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
        char kw[16];
        if(sscanf(p, "%15s", kw) != 1) continue;

        if(!strcmp(kw, "player")) {
            float x, y, a;
            int s;
            if(sscanf(p, "%*s %f %f %d %f", &x, &y, &s, &a) != 4) {
                fprintf(stderr, "map:%d bad player\n", ln);
                fclose(f);
                return std::nullopt;
            }
            m.playerStart = {x, y};
            m.startSector = s;
            m.startAngle = a * PI_F / 180.0f;
            havePlayer = true;
        } else if(!strcmp(kw, "sector")) {
            float fl, ce, l1 = 1.0f, l2 = 1.0f;
            unsigned fc, cc, wc;
            int got = sscanf(p, "%*s %f %f %x %x %x %f %f", &fl, &ce, &fc, &cc, &wc, &l1, &l2);
            if(got < 5 || got > 7) {
                fprintf(stderr, "map:%d bad sector\n", ln);
                fclose(f);
                return std::nullopt;
            }
            Sector S;
            S.floor = fl;
            S.ceiling = ce;
            // got==6 is a legacy single sector-wide light -> apply to floor, ceiling,
            // walls
            S.floorLight = (got >= 6) ? l1 : 1.0f;
            S.ceilingLight = (got == 7) ? l2 : S.floorLight;
            wallLightDefault = (got == 6) ? l1 : 1.0f;
            S.floorColor = 0xFF000000u | fc;
            S.ceilingColor = 0xFF000000u | cc;
            S.wallColor = 0xFF000000u | wc;
            m.sectors.push_back(std::move(S));
            cur = (int)m.sectors.size() - 1;
        } else if(!strcmp(kw, "texture")) {
            char file[256];
            if(sscanf(p, "%*s %255s", file) != 1) {
                fprintf(stderr, "map:%d bad texture\n", ln);
                fclose(f);
                return std::nullopt;
            }
            m.textures.push_back(file);
        } else if(!strcmp(kw, "wall")) {
            if(cur < 0) {
                fprintf(stderr, "map:%d wall before sector\n", ln);
                fclose(f);
                return std::nullopt;
            }
            float x, y, wl = wallLightDefault;
            int nb, texId = -1;
            TextureTransform tx;
            int got = sscanf(p, "%*s %f %f %d %f %f %f %f %d %f", &x, &y, &nb, &tx.uScale,
                             &tx.vScale, &tx.uOffset, &tx.vOffset, &texId, &wl);
            if(got != 3 && got != 7 && got != 8 && got != 9) {
                fprintf(stderr, "map:%d bad wall\n", ln);
                fclose(f);
                return std::nullopt;
            }
            m.sectors[cur].vertices.push_back({x, y});
            m.sectors[cur].neighbors.push_back(nb);
            m.sectors[cur].wallTextures.push_back(tx);
            m.sectors[cur].wallTextureIds.push_back(texId);
            m.sectors[cur].wallLight.push_back(wl);
        } else if(!strcmp(kw, "floortex") || !strcmp(kw, "ceiltex")) {
            if(cur < 0) {
                fprintf(stderr, "map:%d %s before sector\n", ln, kw);
                fclose(f);
                return std::nullopt;
            }
            TextureTransform tx;
            int texId = -1;
            int got = sscanf(p, "%*s %f %f %f %f %d", &tx.uScale, &tx.vScale, &tx.uOffset,
                             &tx.vOffset, &texId);
            if(got != 4 && got != 5) {
                fprintf(stderr, "map:%d bad %s\n", ln, kw);
                fclose(f);
                return std::nullopt;
            }
            if(kw[0] == 'f') {
                m.sectors[cur].floorTexture = tx;
                m.sectors[cur].floorTextureId = texId;
            } else {
                m.sectors[cur].ceilingTexture = tx;
                m.sectors[cur].ceilingTextureId = texId;
            }
        } else if(!strcmp(kw, "ceilsky")) {
            if(cur < 0) {
                fprintf(stderr, "map:%d ceilsky before sector\n", ln);
                fclose(f);
                return std::nullopt;
            }
            m.sectors[cur].ceilingIsSky = true;
        } else if(!strcmp(kw, "loop")) { // begin an inner hole loop (cutout) in the sector
            if(cur < 0) {
                fprintf(stderr, "map:%d loop before sector\n", ln);
                fclose(f);
                return std::nullopt;
            }
            m.sectors[cur].loopStart.push_back((int)m.sectors[cur].vertices.size());
        } else if(!strcmp(kw, "mover")) {
            if(cur < 0) {
                fprintf(stderr, "map:%d mover before sector\n", ln);
                fclose(f);
                return std::nullopt;
            }
            char kind[16];
            float sp = 3.0f;
            int got = sscanf(p, "%*s %15s %f", kind, &sp);
            if(got < 1) {
                fprintf(stderr, "map:%d bad mover\n", ln);
                fclose(f);
                return std::nullopt;
            }
            Sector& S = m.sectors[cur];
            S.mover =
                (kind[0] == 'l' || kind[0] == 'L' || kind[0] == '2') ? 2 : 1; // lift else door
            if(got >= 2) S.moverSpeed = sp;
            // Start at rest: a door closed (ceiling pulled to the floor, its open
            // height remembered in moverRest), a lift down (floor unchanged).
            if(S.mover == 1) {
                S.moverRest = S.ceiling;
                S.ceiling = S.floor;
            } else {
                S.moverRest = S.floor;
            }
            S.moverOpen = false;
        } else if(!strcmp(kw, "sprite")) {
            float x, y, z, r, h;
            unsigned col;
            int texId = -1;
            int got = sscanf(p, "%*s %f %f %f %f %f %x %d", &x, &y, &z, &r, &h, &col, &texId);
            if(got != 6 && got != 7) {
                fprintf(stderr, "map:%d bad sprite\n", ln);
                fclose(f);
                return std::nullopt;
            }
            Sprite S;
            S.position = {x, y};
            S.z = z;
            S.radius = r;
            S.height = h;
            S.color = 0xFF000000u | col;
            S.textureId = texId;
            m.sprites.push_back(S);
        } else {
            fprintf(stderr, "map:%d unknown keyword '%s'\n", ln, kw);
        }
    }
    fclose(f);

    if(m.sectors.empty()) {
        fprintf(stderr, "map: no sectors defined\n");
        return std::nullopt;
    }
    for(size_t i = 0; i < m.sectors.size(); ++i)
        for(int nb : m.sectors[i].neighbors)
            if(nb >= (int)m.sectors.size()) {
                fprintf(stderr, "map: sector %zu has bad neighbour %d\n", i, nb);
                return std::nullopt;
            }
    if(!havePlayer || m.startSector < 0 || m.startSector >= (int)m.sectors.size())
        m.startSector = 0;

    printf("loaded '%s': %zu sectors, %zu sprites\n", path.c_str(), m.sectors.size(),
           m.sprites.size());
    return m;
}

bool saveMap(const Map& m, const std::string& path) {
    FILE* f = fopen(path.c_str(), "w");
    if(!f) {
        fprintf(stderr, "cannot write map '%s'\n", path.c_str());
        return false;
    }

    fprintf(f, "# saved by the portal_engine height editor\n\n");
    for(const std::string& t : m.textures) fprintf(f, "texture %s\n", t.c_str());
    if(!m.textures.empty()) fprintf(f, "\n");
    fprintf(f, "player %g %g %d %g\n\n", m.playerStart.x, m.playerStart.y, m.startSector,
            m.startAngle * 180.0f / PI_F);
    for(const Sector& s : m.sectors) {
        // A door/lift animates floor or ceiling at runtime; persist its authored
        // rest height (moverRest) rather than wherever it happens to be mid-move.
        float floorOut = (s.mover == 2) ? s.moverRest : s.floor;
        float ceilOut = (s.mover == 1) ? s.moverRest : s.ceiling;
        fprintf(f, "sector %g %g %06x %06x %06x", floorOut, ceilOut,
                (unsigned)(s.floorColor & 0xFFFFFF), (unsigned)(s.ceilingColor & 0xFFFFFF),
                (unsigned)(s.wallColor & 0xFFFFFF));
        if(s.floorLight != 1.0f || s.ceilingLight != 1.0f)
            fprintf(f, " %g %g", s.floorLight, s.ceilingLight);
        fprintf(f, "\n");
        size_t loopK = 1; // emit a `loop` marker before each inner loop's walls
        for(size_t w = 0; w < s.vertices.size(); ++w) {
            if(loopK < s.loopStart.size() && (int)w == s.loopStart[loopK]) {
                fprintf(f, "  loop\n");
                ++loopK;
            }
            const TextureTransform& tx = s.wallTextures[w];
            int id = s.wallTextureIds[w];
            float wl = s.wallLight[w];
            // light is positional after texId, so emit the full form when it's
            // non-default
            if(wl != 1.0f)
                fprintf(f, "  wall %g %g %d %g %g %g %g %d %g\n", s.vertices[w].x, s.vertices[w].y,
                        s.neighbors[w], tx.uScale, tx.vScale, tx.uOffset, tx.vOffset, id, wl);
            else if(id >= 0)
                fprintf(f, "  wall %g %g %d %g %g %g %g %d\n", s.vertices[w].x, s.vertices[w].y,
                        s.neighbors[w], tx.uScale, tx.vScale, tx.uOffset, tx.vOffset, id);
            else if(!tx.isDefault())
                fprintf(f, "  wall %g %g %d %g %g %g %g\n", s.vertices[w].x, s.vertices[w].y,
                        s.neighbors[w], tx.uScale, tx.vScale, tx.uOffset, tx.vOffset);
            else fprintf(f, "  wall %g %g %d\n", s.vertices[w].x, s.vertices[w].y, s.neighbors[w]);
        }
        if(s.floorTextureId >= 0)
            fprintf(f, "  floortex %g %g %g %g %d\n", s.floorTexture.uScale, s.floorTexture.vScale,
                    s.floorTexture.uOffset, s.floorTexture.vOffset, s.floorTextureId);
        else if(!s.floorTexture.isDefault())
            fprintf(f, "  floortex %g %g %g %g\n", s.floorTexture.uScale, s.floorTexture.vScale,
                    s.floorTexture.uOffset, s.floorTexture.vOffset);
        if(s.ceilingTextureId >= 0)
            fprintf(f, "  ceiltex %g %g %g %g %d\n", s.ceilingTexture.uScale,
                    s.ceilingTexture.vScale, s.ceilingTexture.uOffset, s.ceilingTexture.vOffset,
                    s.ceilingTextureId);
        else if(!s.ceilingTexture.isDefault())
            fprintf(f, "  ceiltex %g %g %g %g\n", s.ceilingTexture.uScale, s.ceilingTexture.vScale,
                    s.ceilingTexture.uOffset, s.ceilingTexture.vOffset);
        if(s.ceilingIsSky) fprintf(f, "  ceilsky\n");
        if(s.mover) fprintf(f, "  mover %s %g\n", s.mover == 2 ? "lift" : "door", s.moverSpeed);
        fprintf(f, "\n");
    }
    for(const Sprite& s : m.sprites) {
        fprintf(f, "sprite %g %g %g %g %g %06x", s.position.x, s.position.y, s.z, s.radius,
                s.height, (unsigned)(s.color & 0xFFFFFF));
        if(s.textureId >= 0) fprintf(f, " %d", s.textureId);
        fprintf(f, "\n");
    }

    fclose(f);
    printf("saved map to '%s'\n", path.c_str());
    return true;
}
