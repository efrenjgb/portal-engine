// Text map loader/writer. Format (one record per line, '#' starts a comment):
//   player <x> <y> <sector> <angle_deg>
//   sector <floor> <ceil> <floorcol> <ceilcol> <wallcol>   colours = RRGGBB hex
//     wall <x> <y> <neighbour>     one per vertex, CCW; -1 = solid
//   sprite <x> <y> <z> <radius> <height> <col>
#include "Map.h"
#include <cstdio>
#include <cstring>

std::optional<Map> loadMap(const std::string& path){
    FILE* f = fopen(path.c_str(), "r");
    if(!f){ fprintf(stderr, "cannot open map '%s'\n", path.c_str()); return std::nullopt; }

    Map m;
    char line[256];
    int  cur = -1, ln = 0;
    bool havePlayer = false;

    while(fgets(line, sizeof line, f)){
        ++ln;
        char* p = line; while(*p == ' ' || *p == '\t') ++p;
        if(*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
        char kw[16];
        if(sscanf(p, "%15s", kw) != 1) continue;

        if(!strcmp(kw, "player")){
            float x, y, a; int s;
            if(sscanf(p, "%*s %f %f %d %f", &x, &y, &s, &a) != 4){ fprintf(stderr, "map:%d bad player\n", ln); fclose(f); return std::nullopt; }
            m.playerStart = {x, y}; m.startSector = s; m.startAngle = a * PI_F / 180.0f;
            havePlayer = true;
        } else if(!strcmp(kw, "sector")){
            float fl, ce; unsigned fc, cc, wc;
            if(sscanf(p, "%*s %f %f %x %x %x", &fl, &ce, &fc, &cc, &wc) != 5){ fprintf(stderr, "map:%d bad sector\n", ln); fclose(f); return std::nullopt; }
            Sector S; S.floor = fl; S.ceil = ce;
            S.floorCol = 0xFF000000u|fc; S.ceilCol = 0xFF000000u|cc; S.wallCol = 0xFF000000u|wc;
            m.sectors.push_back(std::move(S));
            cur = (int)m.sectors.size() - 1;
        } else if(!strcmp(kw, "texture")){
            char file[256];
            if(sscanf(p, "%*s %255s", file) != 1){ fprintf(stderr, "map:%d bad texture\n", ln); fclose(f); return std::nullopt; }
            m.textures.push_back(file);
        } else if(!strcmp(kw, "wall")){
            if(cur < 0){ fprintf(stderr, "map:%d wall before sector\n", ln); fclose(f); return std::nullopt; }
            float x, y; int nb, texId = -1; TexXform tx;
            int got = sscanf(p, "%*s %f %f %d %f %f %f %f %d", &x, &y, &nb,
                             &tx.us, &tx.vs, &tx.uo, &tx.vo, &texId);
            if(got != 3 && got != 7 && got != 8){ fprintf(stderr, "map:%d bad wall\n", ln); fclose(f); return std::nullopt; }
            m.sectors[cur].vert.push_back({x, y});
            m.sectors[cur].neigh.push_back(nb);
            m.sectors[cur].wallTex.push_back(tx);
            m.sectors[cur].wallTexId.push_back(texId);
        } else if(!strcmp(kw, "floortex") || !strcmp(kw, "ceiltex")){
            if(cur < 0){ fprintf(stderr, "map:%d %s before sector\n", ln, kw); fclose(f); return std::nullopt; }
            TexXform tx; int texId = -1;
            int got = sscanf(p, "%*s %f %f %f %f %d", &tx.us, &tx.vs, &tx.uo, &tx.vo, &texId);
            if(got != 4 && got != 5){ fprintf(stderr, "map:%d bad %s\n", ln, kw); fclose(f); return std::nullopt; }
            if(kw[0] == 'f'){ m.sectors[cur].floorTex = tx; m.sectors[cur].floorTexId = texId; }
            else            { m.sectors[cur].ceilTex  = tx; m.sectors[cur].ceilTexId  = texId; }
        } else if(!strcmp(kw, "ceilsky")){
            if(cur < 0){ fprintf(stderr, "map:%d ceilsky before sector\n", ln); fclose(f); return std::nullopt; }
            m.sectors[cur].ceilSky = true;
        } else if(!strcmp(kw, "sprite")){
            float x, y, z, r, h; unsigned col;
            if(sscanf(p, "%*s %f %f %f %f %f %x", &x, &y, &z, &r, &h, &col) != 6){ fprintf(stderr, "map:%d bad sprite\n", ln); fclose(f); return std::nullopt; }
            Sprite S; S.pos = {x, y}; S.z = z; S.radius = r; S.height = h; S.col = 0xFF000000u|col;
            m.sprites.push_back(S);
        } else {
            fprintf(stderr, "map:%d unknown keyword '%s'\n", ln, kw);
        }
    }
    fclose(f);

    if(m.sectors.empty()){ fprintf(stderr, "map: no sectors defined\n"); return std::nullopt; }
    for(size_t i = 0; i < m.sectors.size(); ++i)
        for(int nb : m.sectors[i].neigh)
            if(nb >= (int)m.sectors.size()){
                fprintf(stderr, "map: sector %zu has bad neighbour %d\n", i, nb); return std::nullopt;
            }
    if(!havePlayer || m.startSector < 0 || m.startSector >= (int)m.sectors.size()) m.startSector = 0;

    printf("loaded '%s': %zu sectors, %zu sprites\n", path.c_str(), m.sectors.size(), m.sprites.size());
    return m;
}

bool saveMap(const Map& m, const std::string& path){
    FILE* f = fopen(path.c_str(), "w");
    if(!f){ fprintf(stderr, "cannot write map '%s'\n", path.c_str()); return false; }

    fprintf(f, "# saved by the build_engine height editor\n\n");
    for(const std::string& t : m.textures) fprintf(f, "texture %s\n", t.c_str());
    if(!m.textures.empty()) fprintf(f, "\n");
    fprintf(f, "player %g %g %d %g\n\n",
            m.playerStart.x, m.playerStart.y, m.startSector, m.startAngle * 180.0f / PI_F);
    for(const Sector& s : m.sectors){
        fprintf(f, "sector %g %g %06x %06x %06x\n", s.floor, s.ceil,
                (unsigned)(s.floorCol & 0xFFFFFF),
                (unsigned)(s.ceilCol  & 0xFFFFFF),
                (unsigned)(s.wallCol  & 0xFFFFFF));
        for(size_t w = 0; w < s.vert.size(); ++w){
            const TexXform& tx = s.wallTex[w];
            int id = s.wallTexId[w];
            if(id >= 0)
                fprintf(f, "  wall %g %g %d %g %g %g %g %d\n", s.vert[w].x, s.vert[w].y,
                        s.neigh[w], tx.us, tx.vs, tx.uo, tx.vo, id);
            else if(!tx.isDefault())
                fprintf(f, "  wall %g %g %d %g %g %g %g\n", s.vert[w].x, s.vert[w].y,
                        s.neigh[w], tx.us, tx.vs, tx.uo, tx.vo);
            else
                fprintf(f, "  wall %g %g %d\n", s.vert[w].x, s.vert[w].y, s.neigh[w]);
        }
        if(s.floorTexId >= 0)
            fprintf(f, "  floortex %g %g %g %g %d\n", s.floorTex.us, s.floorTex.vs, s.floorTex.uo, s.floorTex.vo, s.floorTexId);
        else if(!s.floorTex.isDefault())
            fprintf(f, "  floortex %g %g %g %g\n", s.floorTex.us, s.floorTex.vs, s.floorTex.uo, s.floorTex.vo);
        if(s.ceilTexId >= 0)
            fprintf(f, "  ceiltex %g %g %g %g %d\n", s.ceilTex.us, s.ceilTex.vs, s.ceilTex.uo, s.ceilTex.vo, s.ceilTexId);
        else if(!s.ceilTex.isDefault())
            fprintf(f, "  ceiltex %g %g %g %g\n", s.ceilTex.us, s.ceilTex.vs, s.ceilTex.uo, s.ceilTex.vo);
        if(s.ceilSky) fprintf(f, "  ceilsky\n");
        fprintf(f, "\n");
    }
    for(const Sprite& s : m.sprites)
        fprintf(f, "sprite %g %g %g %g %g %06x\n", s.pos.x, s.pos.y, s.z,
                s.radius, s.height, (unsigned)(s.col & 0xFFFFFF));

    fclose(f);
    printf("saved map to '%s'\n", path.c_str());
    return true;
}
