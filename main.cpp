// Entry point + platform glue: load the map, open an SDL window, run the
// input/update/render loop, and host the in-engine height editor.
#include <SDL.h>
#include <cstdio>
#include <cmath>
#include <string>
#include <utility>
#include <algorithm>
#include <vector>
#include <filesystem>
#include "Map.h"
#include "Texture.h"
#include "Renderer.h"
#include "Player.h"

#if EDITOR
// ---- 2D map-editor helpers (operate on the Map's vertices) -----------------
static bool verticesEqual(Vec2 a, Vec2 b){ float dx=a.x-b.x, dy=a.y-b.y; return dx*dx+dy*dy < 1e-4f; }

struct VHit { int sector = -1, index = -1; };

// nearest vertex to screen point (within ~8 px)
static VHit pickVertex(const Map& m, float sc, float ox, float oy, int mx, int my){
    VHit h; float best = 8.0f*8.0f;
    for(int s = 0; s < (int)m.sectors.size(); ++s)
        for(int i = 0; i < (int)m.sectors[s].vertices.size(); ++i){
            float dx = ox + m.sectors[s].vertices[i].x*sc - mx;
            float dy = oy - m.sectors[s].vertices[i].y*sc - my;
            float d = dx*dx + dy*dy;
            if(d < best){ best = d; h = {s, i}; }
        }
    return h;
}
// nearest wall to screen point (within ~7 px); proj = foot on that wall in world
static VHit pickWall(const Map& m, float sc, float ox, float oy, int mx, int my, Vec2& proj){
    VHit h; float best = 7.0f*7.0f;
    for(int s = 0; s < (int)m.sectors.size(); ++s){
        const Sector& S = m.sectors[s];
        int n = (int)S.vertices.size();
        for(int w = 0; w < n; ++w){
            Vec2 a = S.vertices[w], b = S.vertices[(w+1)%n];
            float ax = ox+a.x*sc, ay = oy-a.y*sc, bx = ox+b.x*sc, by = oy-b.y*sc;
            float ex = bx-ax, ey = by-ay, L2 = ex*ex+ey*ey; if(L2 < 1e-6f) continue;
            float t = ((mx-ax)*ex + (my-ay)*ey)/L2; if(t < 0) t = 0; if(t > 1) t = 1;
            float dx = mx - (ax+ex*t), dy = my - (ay+ey*t);
            float d = dx*dx + dy*dy;
            if(d < best){ best = d; h = {s, w}; proj = { a.x + (b.x-a.x)*t, a.y + (b.y-a.y)*t }; }
        }
    }
    return h;
}
// nearest sprite to a screen point (within ~9 px); -1 if none
static int pickSprite(const Map& m, float sc, float ox, float oy, int mx, int my){
    int best = -1; float bestD = 9.0f*9.0f;
    for(int i = 0; i < (int)m.sprites.size(); ++i){
        float dx = (ox + m.sprites[i].position.x*sc) - mx;
        float dy = (oy - m.sprites[i].position.y*sc) - my;
        float d = dx*dx + dy*dy;
        if(d < bestD){ bestD = d; best = i; }
    }
    return best;
}
// every (sector,index) vertex coincident with p — so a drag keeps portals joined
static void collectCoincident(const Map& m, Vec2 p, std::vector<std::pair<int,int>>& out){
    for(int s = 0; s < (int)m.sectors.size(); ++s)
        for(int i = 0; i < (int)m.sectors[s].vertices.size(); ++i)
            if(verticesEqual(m.sectors[s].vertices[i], p)) out.push_back({s, i});
}
// split wall w of sector s at point p (and the matching wall of a portal neighbour)
static void splitWall(Map& m, int s, int w, Vec2 p){
    auto insertAt = [&](Sector& S, int at){
        S.vertices.insert(S.vertices.begin()+at+1, p);
        S.neighbors.insert(S.neighbors.begin()+at+1, S.neighbors[at]);
        S.wallTextures.insert(S.wallTextures.begin()+at+1, S.wallTextures[at]);
        S.wallTextureIds.insert(S.wallTextureIds.begin()+at+1, S.wallTextureIds[at]);
    };
    Sector& S = m.sectors[s];
    int n = (int)S.vertices.size();
    Vec2 a = S.vertices[w], b = S.vertices[(w+1)%n];
    int nb = S.neighbors[w];
    insertAt(S, w);
    if(nb >= 0){                                   // split the neighbour's matching (reversed) wall
        Sector& N = m.sectors[nb];
        int nn = (int)N.vertices.size();
        for(int i = 0; i < nn; ++i)
            if(verticesEqual(N.vertices[i], b) && verticesEqual(N.vertices[(i+1)%nn], a)){ insertAt(N, i); break; }
    }
}
// delete the vertex at p (and every coincident copy) from each sector that uses
// it, provided that sector keeps at least 3 vertices. The two walls meeting at
// the vertex merge into one. Deleting a *coincident* point (a portal seam, or a
// point you just split) removes it from both sides symmetrically, so portals
// stay matched; deleting a lone sector corner reshapes only that sector.
static bool deleteVertex(Map& m, Vec2 p){
    std::vector<std::pair<int,int>> hits;
    collectCoincident(m, p, hits);
    if(hits.empty()) return false;
    for(auto& h : hits) if((int)m.sectors[h.first].vertices.size() <= 3) return false;
    std::sort(hits.begin(), hits.end(),                // erase high indices first
              [](const std::pair<int,int>& a, const std::pair<int,int>& b){ return a.second > b.second; });
    for(auto& h : hits){
        Sector& S = m.sectors[h.first]; int i = h.second;
        S.vertices.erase(S.vertices.begin()+i);
        S.neighbors.erase(S.neighbors.begin()+i);
        S.wallTextures.erase(S.wallTextures.begin()+i);
        S.wallTextureIds.erase(S.wallTextureIds.begin()+i);
    }
    return true;
}
// Recompute every wall's neighbour link purely from geometry: a wall a->b is a
// portal to whichever sector owns the reversed wall b->a. Walls with no such
// partner become solid. So a portal forms wherever two sectors' walls are made
// to coincide (drag them together) and breaks when they no longer match (after
// a delete or a drag apart) — portals always stay consistent with the geometry.
static void rebuildPortals(Map& m){
    int ns = (int)m.sectors.size();
    for(int s = 0; s < ns; ++s){
        Sector& S = m.sectors[s];
        int n = (int)S.vertices.size();
        for(int w = 0; w < n; ++w){
            Vec2 a = S.vertices[w], b = S.vertices[(w+1)%n];
            int found = -1;
            for(int t = 0; t < ns && found < 0; ++t){
                if(t == s) continue;
                const Sector& N = m.sectors[t]; int nn = (int)N.vertices.size();
                for(int i = 0; i < nn; ++i)
                    if(verticesEqual(N.vertices[i], b) && verticesEqual(N.vertices[(i+1)%nn], a)){ found = t; break; }
            }
            S.neighbors[w] = found;
        }
    }
}
// point-in-polygon (even-odd rule) for inheriting a new sector's look.
static bool pointInSector(const Sector& S, Vec2 p){
    bool in = false; int n = (int)S.vertices.size();
    for(int i = 0, j = n-1; i < n; j = i++){
        Vec2 a = S.vertices[i], b = S.vertices[j];
        if(((a.y > p.y) != (b.y > p.y)) &&
           (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)) in = !in;
    }
    return in;
}
// Create a new sector from drawn points. Forces CCW winding (the renderer's
// back-face cull needs it), inherits heights/colours from whatever sector the
// new polygon sits inside (else neutral defaults), and rebuilds portals so any
// edge shared with an existing sector bonds automatically.
static bool addSector(Map& m, std::vector<Vec2> pts){
    int n = (int)pts.size();
    if(n < 3) return false;
    double area2 = 0;                                  // signed area * 2
    for(int i = 0; i < n; ++i){ Vec2 a = pts[i], b = pts[(i+1)%n]; area2 += (double)a.x*b.y - (double)b.x*a.y; }
    if(std::fabs(area2) < 0.5) return false;           // degenerate / collinear
    if(area2 < 0) std::reverse(pts.begin(), pts.end());// CW -> CCW

    Sector S;
    S.floor = 0.0f; S.ceiling = 4.5f;                     // neutral defaults
    S.floorColor = 0xFF243447; S.ceilingColor = 0xFF1b2230; S.wallColor = 0xFF6f7d8c;
    Vec2 c{0,0}; for(auto& v : pts){ c.x += v.x; c.y += v.y; } c.x /= n; c.y /= n;
    for(const Sector& E : m.sectors)                   // inherit from the sector we're inside
        if(pointInSector(E, c)){
            S.floor = E.floor; S.ceiling = E.ceiling;
            S.floorColor = E.floorColor; S.ceilingColor = E.ceilingColor; S.wallColor = E.wallColor;
            break;
        }
    S.vertices = pts;
    S.neighbors.assign(n, -1);
    S.wallTextures.assign(n, TextureTransform{});
    S.wallTextureIds.assign(n, -1);
    m.sectors.push_back(std::move(S));
    rebuildPortals(m);
    return true;
}
#endif

int main(int argc, char** argv){
    // Args: an optional map file (first non-flag arg) and flags. --novsync uncaps
    // the frame rate (renderer created without SDL_RENDERER_PRESENTVSYNC).
    std::string mapPath = "map.txt";
    bool vsync = true;
    for(int i = 1; i < argc; ++i){
        if(std::string(argv[i]) == "--novsync") vsync = false;
        else mapPath = argv[i];
    }
    auto loaded = loadMap(mapPath);
    if(!loaded) return 1;
    Map map = std::move(*loaded);
    // Edits save to "<mapfile>.save" so the source map is never clobbered — but if
    // we already loaded a ".save" file, overwrite it in place rather than piling on
    // another suffix (".save.save.save…").
    [[maybe_unused]] std::string savePath =
        (mapPath.size() >= 5 && mapPath.compare(mapPath.size()-5, 5, ".save") == 0)
        ? mapPath : mapPath + ".save";

    Player player;
    player.sector    = map.startSector;
    player.camera.x     = map.playerStart.x;
    player.camera.y     = map.playerStart.y;
    player.camera.angle = map.startAngle;
    player.camera.z     = map.sectors[player.sector].floor + PLAYER_EYE_HEIGHT;
    player.camera.update();

    if(SDL_Init(SDL_INIT_VIDEO) != 0){
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }
    const int W = Renderer::W, H = Renderer::H;
    SDL_Window*   win = SDL_CreateWindow("Build-style portal engine (C++)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, 0);
    Uint32 renFlags = SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0u);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, renFlags);
    printf("vsync %s\n", vsync ? "on" : "off (--novsync)");
    SDL_Texture*  tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, W, H);
    SDL_SetRelativeMouseMode(SDL_TRUE);

    Renderer renderer;

    // Load image textures referenced by the map (empty entry => procedural).
    std::vector<Texture> texSet;
    for(const std::string& tpath : map.textures){
        auto t = loadImage(tpath);
        texSet.push_back(t ? std::move(*t) : Texture{});
    }
    renderer.setTextures(&texSet);

    printf("\n  Build-style portal engine (C++)%s\n"
             "  -------------------------------\n"
             "   WASD / arrows : move & strafe\n"
             "   mouse         : look (turn + pitch)\n"
             "   Q / E         : turn left / right\n"
             "   R / F         : look up / down\n"
             "   Space         : jump\n"
             "   M             : release / recapture mouse\n",
             EDITOR ? "" : "  [play build]");
#if EDITOR
    printf("   Tab           : toggle edit mode\n"
             "     T / G       :   raise / lower the aimed SPRITE's height, else the aimed sector's\n"
             "                       CEILING (look up) or FLOOR (look down)\n"
             "     [ / ]       :   shrink / grow texture on aimed surface\n"
             "     ; / '       :   pan texture horizontally\n"
             "     , / .       :   pan texture vertically\n"
             "   N             :   cycle image texture on aimed surface\n"
             "   B             :   browse + pick a texture for the aimed wall/sprite (F: all/solid/masked)\n"
             "   O             :   toggle sky backdrop on aimed ceiling\n"
             "   Enter         : 2D map view  (G grid snap, hold Alt to bypass | Z undo | Del/X delete vertex/sprite)\n"
             "                     drag a vertex to move it; drag a sprite (diamond) to reposition it; N adds a sprite;\n"
             "                     click a wall to split in a vertex; coincide two walls to bond a portal;\n"
             "                     B draws a new sector (click points, click the first to close)\n"
             "   P             : set player start to current position\n"
             "   K             : save edited map (to <mapfile>.save)\n");
#endif
    printf("   Esc           : quit\n\n");

    bool running = true, mouseGrabbed = true, editMode = false;
    Uint32 prev = SDL_GetTicks();
    float  fps = 0.0f;                      // smoothed frames/second, for the HUD
#if EDITOR
    bool  mapView = false;                 // 2D overhead vertex editor (Enter)
    float mvScale = 24.0f, mvOx = 0, mvOy = 0;
    int   mouseX = W/2, mouseY = H/2;
    std::vector<std::pair<int,int>> dragVerts;
    int   dragSprite = -1;                    // sprite being dragged in 2D, or -1
    bool  drawing = false;                    // drawing a new sector (B)
    std::vector<Vec2> drawPts;                // its points so far
    struct Snapshot { std::vector<Sector> sectors; std::vector<Sprite> sprites; };
    std::vector<Snapshot> undo;               // geometry+sprite snapshots for undo (Z)
    auto  pushUndo = [&](){ undo.push_back({ map.sectors, map.sprites });
                            if(undo.size() > 64) undo.erase(undo.begin()); };
    bool  gridSnap = true;                  // snap edits to the unit grid (G);
    float gridSize = 1.0f;                   // hold Alt to bypass it for fine placement
    auto  snap = [&](Vec2 p){ if(gridSnap && !(SDL_GetModState() & KMOD_ALT)){
                                  p.x = std::round(p.x/gridSize)*gridSize;
                                  p.y = std::round(p.y/gridSize)*gridSize; } return p; };
    auto s2wx = [&](int sx){ return (sx - mvOx) / mvScale; };          // screen->world
    auto s2wy = [&](int sy){ return (mvOy - sy) / mvScale; };
    auto fitView = [&](){                                              // frame the whole map
        float minx=1e9f, miny=1e9f, maxx=-1e9f, maxy=-1e9f;
        for(auto& S : map.sectors) for(auto& v : S.vertices){
            minx=std::min(minx,v.x); maxx=std::max(maxx,v.x);
            miny=std::min(miny,v.y); maxy=std::max(maxy,v.y); }
        float wsp = maxx-minx+1e-3f, hsp = maxy-miny+1e-3f;
        mvScale = std::min(W/wsp, H/hsp) * 0.85f;
        mvOx = W*0.5f - (minx+maxx)*0.5f*mvScale;
        mvOy = H*0.5f + (miny+maxy)*0.5f*mvScale;
    };
    // transient on-screen messages (newest at the bottom, fade out near the end)
    struct Toast { std::string text; float ttl; };
    std::vector<Toast> toasts;
    auto showMessage = [&](std::string s){ toasts.push_back({ std::move(s), 3.0f });
                                           if(toasts.size() > 10) toasts.erase(toasts.begin()); };

    // ---- texture picker (B) ----
    bool browsing = false; int browsePage = 0, browseHover = -1;
    SurfaceRef pickTarget; int targetSprite = -1;     // what the picker assigns to
    std::vector<std::string> browsePaths;             // textures/duke/*.png (lazy-loaded)
    std::vector<Texture>     browseTex;
    std::vector<char>        browseMasked;            // true = has transparency (sprite-like)
    std::vector<int>         browseView;              // pool indices passing the active filter
    int  browseFilter = 0;                            // 0 = all, 1 = solid (walls), 2 = masked (sprites)
    bool browseLoaded = false;
    auto filterName = [&]{ return browseFilter == 1 ? "SOLID" : browseFilter == 2 ? "MASKED" : "ALL"; };
    auto rebuildView = [&]{
        browseView.clear();
        for(int i = 0; i < (int)browseTex.size(); ++i)
            if(browseFilter == 0 || (browseFilter == 1) == (!browseMasked[i])) browseView.push_back(i);
        browsePage = 0;
    };
    auto loadBrowse = [&](){
        if(browseLoaded) return;
        browseLoaded = true;
        namespace fs = std::filesystem;
        std::error_code ec;
        for(auto& e : fs::directory_iterator("textures/duke", ec))
            if(e.path().extension() == ".png") browsePaths.push_back(e.path().string());
        std::sort(browsePaths.begin(), browsePaths.end());
        for(auto& p : browsePaths){
            auto t = loadImage(p);
            Texture tex = t ? std::move(*t) : Texture{};
            // classify: a meaningful fraction of transparent texels => sprite-like
            size_t clear = 0; for(uint32_t px : tex.pixels) if((px >> 24) < 128) ++clear;
            bool masked = !tex.pixels.empty() && clear * 100 > tex.pixels.size() * 5;   // >5% clear
            browseMasked.push_back(masked ? 1 : 0);
            browseTex.push_back(std::move(tex));
        }
        printf("texture picker: loaded %zu tiles from textures/duke\n", browseTex.size());
    };
    // return the texSet index for a path, loading+appending it (and recording it
    // in the map) the first time so only picked textures get saved.
    auto ensureTexture = [&](const std::string& path) -> int {
        for(int i = 0; i < (int)map.textures.size(); ++i) if(map.textures[i] == path) return i;
        auto t = loadImage(path);
        map.textures.push_back(path);
        texSet.push_back(t ? std::move(*t) : Texture{});
        renderer.setTextures(&texSet);                // vector may have reallocated
        return (int)texSet.size() - 1;
    };
#endif

    while(running){
        float mdx = 0, mdy = 0;
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type == SDL_QUIT) running = false;
            else if(e.type == SDL_MOUSEMOTION){
#if EDITOR
                if(browsing || mapView){ mouseX = e.motion.x; mouseY = e.motion.y; } else
#endif
                if(mouseGrabbed){ mdx += e.motion.xrel; mdy += e.motion.yrel; }
            }
#if EDITOR
            else if(e.type == SDL_MOUSEWHEEL && browsing){
                int per = Renderer::BR_COLS * Renderer::BR_ROWS;
                int pages = browseView.empty() ? 1 : ((int)browseView.size() + per - 1) / per;
                browsePage = clampi(browsePage + (e.wheel.y > 0 ? -1 : 1), 0, pages - 1);
            }
            else if(e.type == SDL_MOUSEBUTTONDOWN && browsing && e.button.button == SDL_BUTTON_LEFT){
                const int per = Renderer::BR_COLS * Renderer::BR_ROWS;
                const int cw = W / Renderer::BR_COLS, ch = (H - Renderer::BR_HEAD) / Renderer::BR_ROWS;
                int mx = e.button.x, my = e.button.y;
                if(my >= Renderer::BR_HEAD){
                    int col = mx / cw, row = (my - Renderer::BR_HEAD) / ch;
                    int vi = browsePage*per + row*Renderer::BR_COLS + col;   // index into the filtered view
                    if(col >= 0 && col < Renderer::BR_COLS && row >= 0 && row < Renderer::BR_ROWS &&
                       vi >= 0 && vi < (int)browseView.size()){
                        int real = browseView[vi];
                        pushUndo();
                        int tid = ensureTexture(browsePaths[real]);
                        if(targetSprite >= 0 && targetSprite < (int)map.sprites.size())
                            map.sprites[targetSprite].textureId = tid;
                        else if(pickTarget.sector >= 0 && pickTarget.sector < (int)map.sectors.size()){
                            Sector& s = map.sectors[pickTarget.sector];
                            if(pickTarget.kind == SurfaceRef::Wall && pickTarget.wall < (int)s.wallTextureIds.size())
                                s.wallTextureIds[pickTarget.wall] = tid;
                            else if(pickTarget.kind == SurfaceRef::Floor)   s.floorTextureId   = tid;
                            else if(pickTarget.kind == SurfaceRef::Ceiling) s.ceilingTextureId = tid;
                        }
                        showMessage("texture set (tile " + std::to_string(real) + ")");
                        browsing = false;
                        SDL_SetRelativeMouseMode(mouseGrabbed ? SDL_TRUE : SDL_FALSE);
                    }
                }
            }
            else if(e.type == SDL_MOUSEWHEEL && mapView){
                float wx = s2wx(mouseX), wy = s2wy(mouseY);            // zoom toward cursor
                mvScale = clampf(mvScale * (e.wheel.y > 0 ? 1.15f : e.wheel.y < 0 ? 0.87f : 1.0f), 2.0f, 400.0f);
                mvOx = mouseX - wx*mvScale; mvOy = mouseY + wy*mvScale;
            }
            else if(e.type == SDL_MOUSEBUTTONDOWN && mapView && e.button.button == SDL_BUTTON_RIGHT){
                if(drawing){ drawing = false; drawPts.clear(); printf("draw cancelled\n"); }
            }
            else if(e.type == SDL_MOUSEBUTTONDOWN && mapView && e.button.button == SDL_BUTTON_LEFT){
                mouseX = e.button.x; mouseY = e.button.y;
                if(drawing){                                          // place / close a new-sector point
                    bool closed = false;
                    if(drawPts.size() >= 3){                          // click near the first point closes
                        float sx = mvOx + drawPts[0].x*mvScale, sy = mvOy - drawPts[0].y*mvScale;
                        float dx = sx - mouseX, dy = sy - mouseY;
                        if(dx*dx + dy*dy < 100.0f){
                            pushUndo();
                            if(addSector(map, drawPts)){ printf("created sector %d\n", (int)map.sectors.size()-1); showMessage("created sector " + std::to_string((int)map.sectors.size()-1)); }
                            else { undo.pop_back(); printf("invalid sector (need 3+ non-collinear points)\n"); showMessage("invalid sector"); }
                            drawPts.clear(); drawing = false; closed = true;
                        }
                    }
                    if(!closed){                                      // snap to an existing vertex, else grid
                        VHit v = pickVertex(map, mvScale, mvOx, mvOy, mouseX, mouseY);
                        drawPts.push_back(v.index >= 0 ? map.sectors[v.sector].vertices[v.index]
                                                     : snap({ s2wx(mouseX), s2wy(mouseY) }));
                    }
                } else {
                    VHit v = pickVertex(map, mvScale, mvOx, mvOy, mouseX, mouseY);
                    dragVerts.clear(); dragSprite = -1;
                    if(v.index >= 0){                                   // grab a vertex (+coincident)
                        pushUndo();
                        collectCoincident(map, map.sectors[v.sector].vertices[v.index], dragVerts);
                    } else if((dragSprite = pickSprite(map, mvScale, mvOx, mvOy, mouseX, mouseY)) >= 0){
                        pushUndo();                                    // grab a sprite to reposition
                    } else {                                          // else split the wall here
                        Vec2 proj; VHit w = pickWall(map, mvScale, mvOx, mvOy, mouseX, mouseY, proj);
                        if(w.index >= 0){ pushUndo(); proj = snap(proj); splitWall(map, w.sector, w.index, proj); rebuildPortals(map); collectCoincident(map, proj, dragVerts); }
                    }
                }
            }
            else if(e.type == SDL_MOUSEBUTTONUP && mapView && e.button.button == SDL_BUTTON_LEFT){
                dragVerts.clear(); dragSprite = -1;
            }
#endif
            else if(e.type == SDL_KEYDOWN){
                SDL_Keycode k = e.key.keysym.sym;
#if EDITOR
                if(browsing){                      // ---- texture picker is modal ----
                    int per = Renderer::BR_COLS * Renderer::BR_ROWS;
                    int pages = browseView.empty() ? 1 : ((int)browseView.size() + per - 1) / per;
                    if(k == SDLK_ESCAPE || k == SDLK_b){ browsing = false;
                        SDL_SetRelativeMouseMode(mouseGrabbed ? SDL_TRUE : SDL_FALSE); }
                    else if(k == SDLK_f){ browseFilter = (browseFilter+1)%3; rebuildView();
                                          showMessage(std::string("filter: ") + filterName()); }
                    else if(k==SDLK_RIGHT || k==SDLK_DOWN || k==SDLK_PAGEDOWN) browsePage = clampi(browsePage+1, 0, pages-1);
                    else if(k==SDLK_LEFT  || k==SDLK_UP   || k==SDLK_PAGEUP)   browsePage = clampi(browsePage-1, 0, pages-1);
                } else {
#endif
                if(k == SDLK_ESCAPE) running = false;
                if(k == SDLK_SPACE)  player.jump();
                if(k == SDLK_m){ mouseGrabbed = !mouseGrabbed;
                                 SDL_SetRelativeMouseMode(mouseGrabbed ? SDL_TRUE : SDL_FALSE); }
#if EDITOR
                if(k == SDLK_b && !mapView){        // open the picker on the aimed wall/sprite
                    SurfaceRef a = renderer.pickAt(W/2, H/2);
                    if(a.kind == SurfaceRef::None) showMessage("aim at a wall or sprite first");
                    else {
                        if(a.kind == SurfaceRef::Sprite){ targetSprite = a.sprite; pickTarget = SurfaceRef{}; }
                        else { pickTarget = a; targetSprite = -1; }
                        loadBrowse();
                        browseFilter = (a.kind == SurfaceRef::Sprite) ? 2 : 1;  // sprite->masked, surface->solid
                        rebuildView();
                        browsing = true; browsePage = 0;
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                    }
                }
                if(k == SDLK_RETURN || k == SDLK_KP_ENTER){
                    mapView = !mapView;
                    drawing = false; drawPts.clear();   // leaving the view cancels a draw
                    if(mapView) fitView();
                    SDL_SetRelativeMouseMode((!mapView && mouseGrabbed) ? SDL_TRUE : SDL_FALSE);
                }
                if(mapView){                       // ---- 2D map-editor keys ----
                    if(k == SDLK_g){ gridSnap = !gridSnap; printf("grid snap %s\n", gridSnap ? "on" : "off");
                                     showMessage(gridSnap ? "grid snap on" : "grid snap off"); }
                    if(k == SDLK_b){                // start / finish drawing a new sector
                        if(drawing){
                            if(drawPts.size() >= 3){
                                pushUndo();
                                if(addSector(map, drawPts)){ printf("created sector %d\n", (int)map.sectors.size()-1); showMessage("created sector " + std::to_string((int)map.sectors.size()-1)); }
                                else { undo.pop_back(); printf("invalid sector (need 3+ non-collinear points)\n"); showMessage("invalid sector"); }
                            } else { printf("draw cancelled\n"); showMessage("draw cancelled"); }
                            drawing = false; drawPts.clear();
                        } else { drawing = true; drawPts.clear();
                                 printf("draw sector: click points; click the first point or press B to close, right-click cancels\n");
                                 showMessage("draw sector: click points, B to close"); }
                    }
                    if(drawing){
                        if(k == SDLK_BACKSPACE && !drawPts.empty()) drawPts.pop_back();
                    } else {
                        if(k == SDLK_z && !undo.empty()){
                            map.sectors = undo.back().sectors; map.sprites = undo.back().sprites;
                            undo.pop_back(); dragVerts.clear(); dragSprite = -1;
                            printf("undo (%zu left)\n", undo.size()); showMessage("undo");
                        }
                        if(k == SDLK_DELETE || k == SDLK_BACKSPACE || k == SDLK_x){
                            VHit v = pickVertex(map, mvScale, mvOx, mvOy, mouseX, mouseY);
                            int sp;
                            if(v.index >= 0){               // delete a vertex...
                                pushUndo();
                                if(!deleteVertex(map, map.sectors[v.sector].vertices[v.index])){
                                    undo.pop_back();          // nothing removed: drop the snapshot
                                    printf("can't delete: a sector would drop below 3 vertices\n");
                                    showMessage("can't delete (min 3 vertices)");
                                } else { rebuildPortals(map); dragVerts.clear(); printf("deleted vertex\n"); showMessage("deleted vertex"); }
                            } else if((sp = pickSprite(map, mvScale, mvOx, mvOy, mouseX, mouseY)) >= 0){
                                pushUndo();                 // ...else a sprite under the cursor
                                map.sprites.erase(map.sprites.begin() + sp);
                                printf("deleted sprite %d\n", sp); showMessage("deleted sprite");
                            }
                        }
                        if(k == SDLK_n){                    // drop a new sprite at the cursor
                            Vec2 wp = snap({ s2wx(mouseX), s2wy(mouseY) });
                            pushUndo();
                            Sprite s; s.position = wp; s.z = 0.0f;
                            s.radius = 0.35f; s.height = 1.1f; s.color = 0xFFc0a040;
                            for(const Sector& S : map.sectors)         // rest on that sector's floor
                                if(pointInSector(S, wp)){ s.z = S.floor; break; }
                            map.sprites.push_back(s);
                            printf("added sprite %d\n", (int)map.sprites.size()-1);
                            showMessage("added sprite " + std::to_string((int)map.sprites.size()-1));
                        }
                    }
                }
                if(k == SDLK_TAB) editMode = !editMode;
                if(k == SDLK_n && !mapView){   // cycle image texture on the aimed surface (3D)
                    SurfaceRef a = renderer.pickAt(W/2, H/2);
                    int n = (int)texSet.size(), *idp = nullptr;
                    if(a.sector >= 0 && a.sector < (int)map.sectors.size()){
                        Sector& sc = map.sectors[a.sector];
                        if(a.kind == SurfaceRef::Wall && a.wall < (int)sc.wallTextureIds.size()) idp = &sc.wallTextureIds[a.wall];
                        else if(a.kind == SurfaceRef::Floor)   idp = &sc.floorTextureId;
                        else if(a.kind == SurfaceRef::Ceiling) idp = &sc.ceilingTextureId;
                    }
                    if(idp){ *idp = (*idp + 1 >= n) ? -1 : *idp + 1;
                             printf("texture id = %d %s\n", *idp, *idp < 0 ? "(procedural)" : "");
                             showMessage(*idp < 0 ? "texture: procedural" : "texture id " + std::to_string(*idp)); }
                }
                if(k == SDLK_o && !mapView){   // toggle sky backdrop on the aimed ceiling (3D)
                    SurfaceRef a = renderer.pickAt(W/2, H/2);
                    if(a.kind == SurfaceRef::Ceiling && a.sector >= 0 && a.sector < (int)map.sectors.size()){
                        bool& sky = map.sectors[a.sector].ceilingIsSky;
                        sky = !sky;
                        printf("sector %d ceiling sky = %s\n", a.sector, sky ? "on" : "off");
                        showMessage(sky ? "ceiling sky on" : "ceiling sky off");
                    }
                }
                if(k == SDLK_k){ bool ok = saveMap(map, savePath);
                                 showMessage(ok ? "saved " + savePath : "save failed"); }
                if(k == SDLK_p){
                    map.playerStart = { player.camera.x, player.camera.y };
                    map.startSector = player.sector;
                    map.startAngle  = player.camera.angle;
                    showMessage("player start set");
                    printf("player start set: %.2f %.2f sector %d facing %.0f deg\n",
                           map.playerStart.x, map.playerStart.y, map.startSector,
                           map.startAngle * 180.0f / PI_F);
                }
                }  // end if(!browsing)
#endif
            }
        }

        Uint32 nowt = SDL_GetTicks();
        float dt = (nowt - prev) / 1000.0f;
        if(dt > 0.0f) fps += (1.0f/dt - fps) * 0.1f;   // EMA from the true frame time
        if(dt > 0.05f) dt = 0.05f;
#if EDITOR
        for(auto& t : toasts) t.ttl -= dt;             // expire transient messages
        toasts.erase(std::remove_if(toasts.begin(), toasts.end(),
                     [](const Toast& t){ return t.ttl <= 0.0f; }), toasts.end());
#endif
        prev = nowt;

        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        SurfaceRef aim;                       // None in play build / map view
#if EDITOR
        if(browsing){ /* texture picker is modal: freeze the world */ } else
        if(mapView){
            float pan = 320.0f * dt;          // arrow keys pan the 2D view
            if(ks[SDL_SCANCODE_LEFT])  mvOx += pan;
            if(ks[SDL_SCANCODE_RIGHT]) mvOx -= pan;
            if(ks[SDL_SCANCODE_UP])    mvOy += pan;
            if(ks[SDL_SCANCODE_DOWN])  mvOy -= pan;
            if(!dragVerts.empty()){           // drag held vertex (+ coincident copies)
                Vec2 wp = snap({ s2wx(mouseX), s2wy(mouseY) });
                for(auto& pr : dragVerts) map.sectors[pr.first].vertices[pr.second] = wp;
                rebuildPortals(map);          // bond/break portals as walls meet or part
            }
            if(dragSprite >= 0 && dragSprite < (int)map.sprites.size())  // drag a sprite (x,y)
                map.sprites[dragSprite].position = snap({ s2wx(mouseX), s2wy(mouseY) });
        } else
#endif
        {
        // ---- look ----
        player.camera.angle -= mdx * 0.0030f;
        player.camera.pitch += mdy * 0.0018f;
        if(ks[SDL_SCANCODE_Q]) player.camera.angle += 1.8f * dt;
        if(ks[SDL_SCANCODE_E]) player.camera.angle -= 1.8f * dt;
        if(ks[SDL_SCANCODE_R]) player.camera.pitch -= 1.2f * dt;
        if(ks[SDL_SCANCODE_F]) player.camera.pitch += 1.2f * dt;
        player.camera.pitch = clampf(player.camera.pitch, -0.55f, 0.55f);
        player.camera.update();

        // ---- move ----
        float fwd = 0, str = 0;
        if(ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP])    fwd += 1;
        if(ks[SDL_SCANCODE_S] || ks[SDL_SCANCODE_DOWN])  fwd -= 1;
        if(ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT]) str += 1;
        if(ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT])  str -= 1;
        if(fwd || str){
            float sp = MOVE_SPEED * dt;
            float dx = (player.camera.yawCos*fwd + player.camera.yawSin*str) * sp;
            float dy = (player.camera.yawSin*fwd - player.camera.yawCos*str) * sp;
            player.move(map, dx, dy);
        }
        player.collideSprites(map);
        player.keepInside(map);

        // ---- what's under the crosshair + the height editor (editor builds) ----
#if EDITOR
        aim = renderer.pickAt(W/2, H/2);      // from last frame's pick buffer
        if(editMode){
            float rate = 2.0f * dt;
            // T/G act on whatever the crosshair is on: a sprite's vertical height,
            // else the CEILING when looking up / FLOOR when looking down (pitch > 0
            // looks down) of the sector under the crosshair.
            if(ks[SDL_SCANCODE_T] || ks[SDL_SCANCODE_G]){
                if(aim.kind == SurfaceRef::Sprite &&
                   aim.sprite >= 0 && aim.sprite < (int)map.sprites.size()){
                    Sprite& sp = map.sprites[aim.sprite];
                    float lo = -8.0f, hi = 18.0f;     // clamp to its sector: feet on the
                    for(const Sector& S : map.sectors) // floor, head under the ceiling
                        if(pointInSector(S, sp.position)){ lo = S.floor; hi = std::max(S.floor, S.ceiling - sp.height); break; }
                    if(ks[SDL_SCANCODE_T]) sp.z = std::min(sp.z + rate, hi);
                    if(ks[SDL_SCANCODE_G]) sp.z = std::max(sp.z - rate, lo);
                } else if(aim.sector >= 0 && aim.sector < (int)map.sectors.size()){
                    Sector& t = map.sectors[aim.sector];
                    if(player.camera.pitch < 0.0f){      // looking up -> ceiling
                        if(ks[SDL_SCANCODE_T]) t.ceiling = std::min(t.ceiling + rate, 18.0f);
                        if(ks[SDL_SCANCODE_G]) t.ceiling = std::max(t.ceiling - rate, t.floor + 0.3f);
                    } else {                          // looking down / level -> floor
                        if(ks[SDL_SCANCODE_T]) t.floor = std::min(t.floor + rate, t.ceiling - 0.3f);
                        if(ks[SDL_SCANCODE_G]) t.floor = std::max(t.floor - rate, -8.0f);
                    }
                }
            }

            // texture wrap/pan still acts on the exact surface under the crosshair
            if(aim.sector >= 0 && aim.sector < (int)map.sectors.size()){
                Sector& t = map.sectors[aim.sector];
                TextureTransform* tx = nullptr;
                if(aim.kind == SurfaceRef::Wall && aim.wall < (int)t.wallTextures.size()) tx = &t.wallTextures[aim.wall];
                else if(aim.kind == SurfaceRef::Floor)   tx = &t.floorTexture;
                else if(aim.kind == SurfaceRef::Ceiling) tx = &t.ceilingTexture;
                if(tx){
                    float pan = 1.5f * dt, sf = 1.0f + 1.5f * dt;
                    if(ks[SDL_SCANCODE_RIGHTBRACKET]){ tx->uScale *= sf; tx->vScale *= sf; }
                    if(ks[SDL_SCANCODE_LEFTBRACKET]) { tx->uScale /= sf; tx->vScale /= sf; }
                    tx->uScale = clampf(tx->uScale, 0.1f, 16.0f);
                    tx->vScale = clampf(tx->vScale, 0.1f, 16.0f);
                    if(ks[SDL_SCANCODE_APOSTROPHE]) tx->uOffset += pan;
                    if(ks[SDL_SCANCODE_SEMICOLON])  tx->uOffset -= pan;
                    if(ks[SDL_SCANCODE_PERIOD])     tx->vOffset += pan;
                    if(ks[SDL_SCANCODE_COMMA])      tx->vOffset -= pan;
                }
            }
        }
        // report the targeted surface when it changes (groundwork for texturing)
        static int lastK = -1, lastS = -1, lastW = -1, lastSp = -1;
        if(editMode && ((int)aim.kind != lastK || aim.sector != lastS || aim.wall != lastW || aim.sprite != lastSp)){
            if(aim.kind == SurfaceRef::Wall)
                printf("aim: sector %d WALL %d\n", aim.sector, aim.wall);
            else if(aim.kind == SurfaceRef::Floor)   printf("aim: sector %d FLOOR\n",   aim.sector);
            else if(aim.kind == SurfaceRef::Ceiling) printf("aim: sector %d CEILING\n", aim.sector);
            else if(aim.kind == SurfaceRef::Sprite)  printf("aim: SPRITE %d\n",         aim.sprite);
            lastK = (int)aim.kind; lastS = aim.sector; lastW = aim.wall; lastSp = aim.sprite;
        }
#endif
        player.settleEyeHeight(map, dt);
        }   // end 3D update branch

        // ---- draw ----
        renderer.clear(0xFF0a0c12);
#if EDITOR
        if(mapView){
            VHit hv, hw; Vec2 proj; int hovSprite = -1;
            if(!drawing){                          // no hover highlight while drawing
                hv = pickVertex(map, mvScale, mvOx, mvOy, mouseX, mouseY);
                if(hv.index < 0) hw = pickWall(map, mvScale, mvOx, mvOy, mouseX, mouseY, proj);
                hovSprite = dragSprite >= 0 ? dragSprite
                          : (hv.index < 0 ? pickSprite(map, mvScale, mvOx, mvOy, mouseX, mouseY) : -1);
            }
            renderer.drawMapEditor(map, mvScale, mvOx, mvOy, hv.sector, hv.index, hw.sector, hw.index,
                                   { player.camera.x, player.camera.y }, player.camera.angle, hovSprite);
            if(drawing) renderer.drawPendingSector(drawPts, mvScale, mvOx, mvOy, mouseX, mouseY);
        } else
#endif
        {
            renderer.renderWorld(map, player.camera, player.sector);
            renderer.drawSprites(map, player.camera);
            renderer.drawMinimap(map, player.camera, editMode ? aim : SurfaceRef{},
                                 map.playerStart, map.startAngle);
            renderer.crosshair(editMode ? 0xFF40ff40 : 0xFFe0e0e0);
        }
#if EDITOR
        if(browsing){                                  // texture picker over the frozen scene
            const int cw = W/Renderer::BR_COLS, ch = (H-Renderer::BR_HEAD)/Renderer::BR_ROWS;
            browseHover = -1;
            if(mouseY >= Renderer::BR_HEAD){
                int col = mouseX/cw, row = (mouseY-Renderer::BR_HEAD)/ch;
                if(col >= 0 && col < Renderer::BR_COLS && row >= 0 && row < Renderer::BR_ROWS)
                    browseHover = row*Renderer::BR_COLS + col;
            }
            renderer.drawTextureBrowser(browseTex, browseView, browsePage, browseHover, filterName());
        }
#endif

        // ---- HUD (over everything) ----
        {
            char hud[32];
            int len = snprintf(hud, sizeof hud, "FPS %d", (int)(fps + 0.5f));
            int scale = 2, tw = len * 6 * scale, tx = W - tw - 6, ty = 6;
            renderer.drawText(tx + 1, ty + 1, hud, 0xFF000000, scale);   // shadow
            renderer.drawText(tx,     ty,     hud, 0xFFf0e070, scale);   // text
        }
#if EDITOR
        for(int i = 0; i < (int)toasts.size(); ++i){       // transient messages, newest at bottom
            float a = std::min(1.0f, toasts[i].ttl / 0.75f);            // fade out in the last 0.75s
            uint8_t alpha = (uint8_t)(a * 255);
            int scale = 2, y = H - 14 - ((int)toasts.size()-1 - i) * (7*scale + 4), x = 8;
            renderer.drawText(x + 1, y + 1, toasts[i].text.c_str(), (uint32_t)alpha << 24, scale);     // shadow
            renderer.drawText(x,     y,     toasts[i].text.c_str(), ((uint32_t)alpha << 24) | 0xe0f0ff, scale);
        }
#endif

        SDL_UpdateTexture(tex, nullptr, renderer.pixels(), W * sizeof(uint32_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
