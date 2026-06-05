// Entry point + platform glue: load the map, open an SDL window, run the
// input/update/render loop, and host the in-engine height editor.
#include <SDL.h>
#include <cstdio>
#include <cmath>
#include <string>
#include <utility>
#include <algorithm>
#include <vector>
#include "Map.h"
#include "Texture.h"
#include "Renderer.h"
#include "Player.h"

#if EDITOR
// ---- 2D map-editor helpers (operate on the Map's vertices) -----------------
static bool vclose(Vec2 a, Vec2 b){ float dx=a.x-b.x, dy=a.y-b.y; return dx*dx+dy*dy < 1e-4f; }

struct VHit { int sec = -1, idx = -1; };

// nearest vertex to screen point (within ~8 px)
static VHit pickVertex(const Map& m, float sc, float ox, float oy, int mx, int my){
    VHit h; float best = 8.0f*8.0f;
    for(int s = 0; s < (int)m.sectors.size(); ++s)
        for(int i = 0; i < (int)m.sectors[s].vert.size(); ++i){
            float dx = ox + m.sectors[s].vert[i].x*sc - mx;
            float dy = oy - m.sectors[s].vert[i].y*sc - my;
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
        int n = (int)S.vert.size();
        for(int w = 0; w < n; ++w){
            Vec2 a = S.vert[w], b = S.vert[(w+1)%n];
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
// every (sector,index) vertex coincident with p — so a drag keeps portals joined
static void collectCoincident(const Map& m, Vec2 p, std::vector<std::pair<int,int>>& out){
    for(int s = 0; s < (int)m.sectors.size(); ++s)
        for(int i = 0; i < (int)m.sectors[s].vert.size(); ++i)
            if(vclose(m.sectors[s].vert[i], p)) out.push_back({s, i});
}
// split wall w of sector s at point p (and the matching wall of a portal neighbour)
static void splitWall(Map& m, int s, int w, Vec2 p){
    auto ins = [&](Sector& S, int at){
        S.vert.insert(S.vert.begin()+at+1, p);
        S.neigh.insert(S.neigh.begin()+at+1, S.neigh[at]);
        S.wallTex.insert(S.wallTex.begin()+at+1, S.wallTex[at]);
        S.wallTexId.insert(S.wallTexId.begin()+at+1, S.wallTexId[at]);
    };
    Sector& S = m.sectors[s];
    int n = (int)S.vert.size();
    Vec2 a = S.vert[w], b = S.vert[(w+1)%n];
    int nb = S.neigh[w];
    ins(S, w);
    if(nb >= 0){                                   // split the neighbour's matching (reversed) wall
        Sector& N = m.sectors[nb];
        int nn = (int)N.vert.size();
        for(int i = 0; i < nn; ++i)
            if(vclose(N.vert[i], b) && vclose(N.vert[(i+1)%nn], a)){ ins(N, i); break; }
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
    for(auto& h : hits) if((int)m.sectors[h.first].vert.size() <= 3) return false;
    std::sort(hits.begin(), hits.end(),                // erase high indices first
              [](const std::pair<int,int>& a, const std::pair<int,int>& b){ return a.second > b.second; });
    for(auto& h : hits){
        Sector& S = m.sectors[h.first]; int i = h.second;
        S.vert.erase(S.vert.begin()+i);
        S.neigh.erase(S.neigh.begin()+i);
        S.wallTex.erase(S.wallTex.begin()+i);
        S.wallTexId.erase(S.wallTexId.begin()+i);
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
        int n = (int)S.vert.size();
        for(int w = 0; w < n; ++w){
            Vec2 a = S.vert[w], b = S.vert[(w+1)%n];
            int found = -1;
            for(int t = 0; t < ns && found < 0; ++t){
                if(t == s) continue;
                const Sector& N = m.sectors[t]; int nn = (int)N.vert.size();
                for(int i = 0; i < nn; ++i)
                    if(vclose(N.vert[i], b) && vclose(N.vert[(i+1)%nn], a)){ found = t; break; }
            }
            S.neigh[w] = found;
        }
    }
}
// point-in-polygon (even-odd rule) for inheriting a new sector's look.
static bool pointInSector(const Sector& S, Vec2 p){
    bool in = false; int n = (int)S.vert.size();
    for(int i = 0, j = n-1; i < n; j = i++){
        Vec2 a = S.vert[i], b = S.vert[j];
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
    S.floor = 0.0f; S.ceil = 4.5f;                     // neutral defaults
    S.floorCol = 0xFF243447; S.ceilCol = 0xFF1b2230; S.wallCol = 0xFF6f7d8c;
    Vec2 c{0,0}; for(auto& v : pts){ c.x += v.x; c.y += v.y; } c.x /= n; c.y /= n;
    for(const Sector& E : m.sectors)                   // inherit from the sector we're inside
        if(pointInSector(E, c)){
            S.floor = E.floor; S.ceil = E.ceil;
            S.floorCol = E.floorCol; S.ceilCol = E.ceilCol; S.wallCol = E.wallCol;
            break;
        }
    S.vert = pts;
    S.neigh.assign(n, -1);
    S.wallTex.assign(n, TexXform{});
    S.wallTexId.assign(n, -1);
    m.sectors.push_back(std::move(S));
    rebuildPortals(m);
    return true;
}
#endif

int main(int argc, char** argv){
    std::string mapPath = (argc > 1) ? argv[1] : "map.txt";
    auto loaded = loadMap(mapPath);
    if(!loaded) return 1;
    Map map = std::move(*loaded);
    [[maybe_unused]] std::string savePath = mapPath + ".save";   // used by the editor

    Player player;
    player.sector    = map.startSector;
    player.cam.x     = map.playerStart.x;
    player.cam.y     = map.playerStart.y;
    player.cam.angle = map.startAngle;
    player.cam.z     = map.sectors[player.sector].floor + EYE;
    player.cam.update();

    if(SDL_Init(SDL_INIT_VIDEO) != 0){
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }
    const int W = Renderer::W, H = Renderer::H;
    SDL_Window*   win = SDL_CreateWindow("Build-style portal engine (C++)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
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
             "     T / G       :   raise / lower this sector's CEILING (look up) or FLOOR (look down)\n"
             "     [ / ]       :   shrink / grow texture on aimed surface\n"
             "     ; / '       :   pan texture horizontally\n"
             "     , / .       :   pan texture vertically\n"
             "   N             :   cycle image texture on aimed surface\n"
             "   O             :   toggle sky backdrop on aimed ceiling\n"
             "   Enter         : 2D map view  (G grid snap | Z undo | Del/X delete vertex)\n"
             "                     drag a vertex to move it; click a wall to split in a vertex;\n"
             "                     make two sectors' walls coincide to bond a portal;\n"
             "                     B draws a new sector (click points, click the first to close)\n"
             "   P             : set player start to current position\n"
             "   K             : save edited map (to <mapfile>.save)\n");
#endif
    printf("   Esc           : quit\n\n");

    bool running = true, mouseGrabbed = true, editMode = false;
    Uint32 prev = SDL_GetTicks();
#if EDITOR
    bool  mapView = false;                 // 2D overhead vertex editor (Enter)
    float mvScale = 24.0f, mvOx = 0, mvOy = 0;
    int   mouseX = W/2, mouseY = H/2;
    std::vector<std::pair<int,int>> dragVerts;
    bool  drawing = false;                   // drawing a new sector (B)
    std::vector<Vec2> drawPts;               // its points so far
    std::vector<std::vector<Sector>> undo;  // geometry snapshots for undo (Z)
    auto  pushUndo = [&](){ undo.push_back(map.sectors);
                            if(undo.size() > 64) undo.erase(undo.begin()); };
    bool  gridSnap = true;                  // snap edits to the unit grid (G)
    float gridSize = 1.0f;
    auto  snap = [&](Vec2 p){ if(gridSnap){ p.x = std::round(p.x/gridSize)*gridSize;
                                            p.y = std::round(p.y/gridSize)*gridSize; } return p; };
    auto s2wx = [&](int sx){ return (sx - mvOx) / mvScale; };          // screen->world
    auto s2wy = [&](int sy){ return (mvOy - sy) / mvScale; };
    auto fitView = [&](){                                              // frame the whole map
        float minx=1e9f, miny=1e9f, maxx=-1e9f, maxy=-1e9f;
        for(auto& S : map.sectors) for(auto& v : S.vert){
            minx=std::min(minx,v.x); maxx=std::max(maxx,v.x);
            miny=std::min(miny,v.y); maxy=std::max(maxy,v.y); }
        float wsp = maxx-minx+1e-3f, hsp = maxy-miny+1e-3f;
        mvScale = std::min(W/wsp, H/hsp) * 0.85f;
        mvOx = W*0.5f - (minx+maxx)*0.5f*mvScale;
        mvOy = H*0.5f + (miny+maxy)*0.5f*mvScale;
    };
#endif

    while(running){
        float mdx = 0, mdy = 0;
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type == SDL_QUIT) running = false;
            else if(e.type == SDL_MOUSEMOTION){
#if EDITOR
                if(mapView){ mouseX = e.motion.x; mouseY = e.motion.y; } else
#endif
                if(mouseGrabbed){ mdx += e.motion.xrel; mdy += e.motion.yrel; }
            }
#if EDITOR
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
                            if(addSector(map, drawPts)) printf("created sector %d\n", (int)map.sectors.size()-1);
                            else { undo.pop_back(); printf("invalid sector (need 3+ non-collinear points)\n"); }
                            drawPts.clear(); drawing = false; closed = true;
                        }
                    }
                    if(!closed){                                      // snap to an existing vertex, else grid
                        VHit v = pickVertex(map, mvScale, mvOx, mvOy, mouseX, mouseY);
                        drawPts.push_back(v.idx >= 0 ? map.sectors[v.sec].vert[v.idx]
                                                     : snap({ s2wx(mouseX), s2wy(mouseY) }));
                    }
                } else {
                    VHit v = pickVertex(map, mvScale, mvOx, mvOy, mouseX, mouseY);
                    dragVerts.clear();
                    if(v.idx >= 0){                                   // grab a vertex (+coincident)
                        pushUndo();
                        collectCoincident(map, map.sectors[v.sec].vert[v.idx], dragVerts);
                    } else {                                          // else split the wall here
                        Vec2 proj; VHit w = pickWall(map, mvScale, mvOx, mvOy, mouseX, mouseY, proj);
                        if(w.idx >= 0){ pushUndo(); proj = snap(proj); splitWall(map, w.sec, w.idx, proj); rebuildPortals(map); collectCoincident(map, proj, dragVerts); }
                    }
                }
            }
            else if(e.type == SDL_MOUSEBUTTONUP && mapView && e.button.button == SDL_BUTTON_LEFT){
                dragVerts.clear();
            }
#endif
            else if(e.type == SDL_KEYDOWN){
                SDL_Keycode k = e.key.keysym.sym;
                if(k == SDLK_ESCAPE) running = false;
                if(k == SDLK_SPACE)  player.jump();
                if(k == SDLK_m){ mouseGrabbed = !mouseGrabbed;
                                 SDL_SetRelativeMouseMode(mouseGrabbed ? SDL_TRUE : SDL_FALSE); }
#if EDITOR
                if(k == SDLK_RETURN || k == SDLK_KP_ENTER){
                    mapView = !mapView;
                    drawing = false; drawPts.clear();   // leaving the view cancels a draw
                    if(mapView) fitView();
                    SDL_SetRelativeMouseMode((!mapView && mouseGrabbed) ? SDL_TRUE : SDL_FALSE);
                }
                if(mapView){                       // ---- 2D map-editor keys ----
                    if(k == SDLK_g){ gridSnap = !gridSnap; printf("grid snap %s\n", gridSnap ? "on" : "off"); }
                    if(k == SDLK_b){                // start / finish drawing a new sector
                        if(drawing){
                            if(drawPts.size() >= 3){
                                pushUndo();
                                if(addSector(map, drawPts)) printf("created sector %d\n", (int)map.sectors.size()-1);
                                else { undo.pop_back(); printf("invalid sector (need 3+ non-collinear points)\n"); }
                            } else printf("draw cancelled\n");
                            drawing = false; drawPts.clear();
                        } else { drawing = true; drawPts.clear();
                                 printf("draw sector: click points; click the first point or press B to close, right-click cancels\n"); }
                    }
                    if(drawing){
                        if(k == SDLK_BACKSPACE && !drawPts.empty()) drawPts.pop_back();
                    } else {
                        if(k == SDLK_z && !undo.empty()){
                            map.sectors = undo.back(); undo.pop_back(); dragVerts.clear();
                            printf("undo (%zu left)\n", undo.size());
                        }
                        if(k == SDLK_DELETE || k == SDLK_BACKSPACE || k == SDLK_x){
                            VHit v = pickVertex(map, mvScale, mvOx, mvOy, mouseX, mouseY);
                            if(v.idx >= 0){
                                pushUndo();
                                if(!deleteVertex(map, map.sectors[v.sec].vert[v.idx])){
                                    undo.pop_back();          // nothing removed: drop the snapshot
                                    printf("can't delete: a sector would drop below 3 vertices\n");
                                } else { rebuildPortals(map); dragVerts.clear(); printf("deleted vertex\n"); }
                            }
                        }
                    }
                }
                if(k == SDLK_TAB) editMode = !editMode;
                if(k == SDLK_n){   // cycle image texture on the aimed surface
                    SurfaceRef a = renderer.pickAt(W/2, H/2);
                    int n = (int)texSet.size(), *idp = nullptr;
                    if(a.sector >= 0 && a.sector < (int)map.sectors.size()){
                        Sector& sc = map.sectors[a.sector];
                        if(a.kind == SurfaceRef::Wall && a.wall < (int)sc.wallTexId.size()) idp = &sc.wallTexId[a.wall];
                        else if(a.kind == SurfaceRef::Floor)   idp = &sc.floorTexId;
                        else if(a.kind == SurfaceRef::Ceiling) idp = &sc.ceilTexId;
                    }
                    if(idp){ *idp = (*idp + 1 >= n) ? -1 : *idp + 1;
                             printf("texture id = %d %s\n", *idp, *idp < 0 ? "(procedural)" : ""); }
                }
                if(k == SDLK_o){   // toggle sky backdrop on the aimed ceiling
                    SurfaceRef a = renderer.pickAt(W/2, H/2);
                    if(a.kind == SurfaceRef::Ceiling && a.sector >= 0 && a.sector < (int)map.sectors.size()){
                        bool& sky = map.sectors[a.sector].ceilSky;
                        sky = !sky;
                        printf("sector %d ceiling sky = %s\n", a.sector, sky ? "on" : "off");
                    }
                }
                if(k == SDLK_k)   saveMap(map, savePath);
                if(k == SDLK_p){
                    map.playerStart = { player.cam.x, player.cam.y };
                    map.startSector = player.sector;
                    map.startAngle  = player.cam.angle;
                    printf("player start set: %.2f %.2f sector %d facing %.0f deg\n",
                           map.playerStart.x, map.playerStart.y, map.startSector,
                           map.startAngle * 180.0f / PI_F);
                }
#endif
            }
        }

        Uint32 nowt = SDL_GetTicks();
        float dt = (nowt - prev) / 1000.0f;
        if(dt > 0.05f) dt = 0.05f;
        prev = nowt;

        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        SurfaceRef aim;                       // None in play build / map view
#if EDITOR
        if(mapView){
            float pan = 320.0f * dt;          // arrow keys pan the 2D view
            if(ks[SDL_SCANCODE_LEFT])  mvOx += pan;
            if(ks[SDL_SCANCODE_RIGHT]) mvOx -= pan;
            if(ks[SDL_SCANCODE_UP])    mvOy += pan;
            if(ks[SDL_SCANCODE_DOWN])  mvOy -= pan;
            if(!dragVerts.empty()){           // drag held vertex (+ coincident copies)
                Vec2 wp = snap({ s2wx(mouseX), s2wy(mouseY) });
                for(auto& pr : dragVerts) map.sectors[pr.first].vert[pr.second] = wp;
                rebuildPortals(map);          // bond/break portals as walls meet or part
            }
        } else
#endif
        {
        // ---- look ----
        player.cam.angle -= mdx * 0.0030f;
        player.cam.pitch += mdy * 0.0018f;
        if(ks[SDL_SCANCODE_Q]) player.cam.angle += 1.8f * dt;
        if(ks[SDL_SCANCODE_E]) player.cam.angle -= 1.8f * dt;
        if(ks[SDL_SCANCODE_R]) player.cam.pitch -= 1.2f * dt;
        if(ks[SDL_SCANCODE_F]) player.cam.pitch += 1.2f * dt;
        player.cam.pitch = clampf(player.cam.pitch, -0.55f, 0.55f);
        player.cam.update();

        // ---- move ----
        float fwd = 0, str = 0;
        if(ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP])    fwd += 1;
        if(ks[SDL_SCANCODE_S] || ks[SDL_SCANCODE_DOWN])  fwd -= 1;
        if(ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT]) str += 1;
        if(ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT])  str -= 1;
        if(fwd || str){
            float sp = MOVE_SPD * dt;
            float dx = (player.cam.vcos*fwd + player.cam.vsin*str) * sp;
            float dy = (player.cam.vsin*fwd - player.cam.vcos*str) * sp;
            player.move(map, dx, dy);
        }
        player.collideSprites(map);
        player.keepInside(map);

        // ---- what's under the crosshair + the height editor (editor builds) ----
#if EDITOR
        aim = renderer.pickAt(W/2, H/2);      // from last frame's pick buffer
        if(editMode){
            float rate = 2.0f * dt;
            // T/G raise/lower the CEILING when looking up, the FLOOR when looking
            // down (pitch > 0 looks down) — acting on the sector you're standing
            // in, so you needn't aim precisely at a horizontal surface.
            if((ks[SDL_SCANCODE_T] || ks[SDL_SCANCODE_G]) &&
               player.sector >= 0 && player.sector < (int)map.sectors.size()){
                Sector& t = map.sectors[player.sector];
                if(player.cam.pitch < 0.0f){      // looking up -> ceiling
                    if(ks[SDL_SCANCODE_T]) t.ceil = std::min(t.ceil + rate, 18.0f);
                    if(ks[SDL_SCANCODE_G]) t.ceil = std::max(t.ceil - rate, t.floor + 0.3f);
                } else {                          // looking down / level -> floor
                    if(ks[SDL_SCANCODE_T]) t.floor = std::min(t.floor + rate, t.ceil - 0.3f);
                    if(ks[SDL_SCANCODE_G]) t.floor = std::max(t.floor - rate, -8.0f);
                }
            }

            // texture wrap/pan still acts on the exact surface under the crosshair
            if(aim.sector >= 0 && aim.sector < (int)map.sectors.size()){
                Sector& t = map.sectors[aim.sector];
                TexXform* tx = nullptr;
                if(aim.kind == SurfaceRef::Wall && aim.wall < (int)t.wallTex.size()) tx = &t.wallTex[aim.wall];
                else if(aim.kind == SurfaceRef::Floor)   tx = &t.floorTex;
                else if(aim.kind == SurfaceRef::Ceiling) tx = &t.ceilTex;
                if(tx){
                    float pan = 1.5f * dt, sf = 1.0f + 1.5f * dt;
                    if(ks[SDL_SCANCODE_RIGHTBRACKET]){ tx->us *= sf; tx->vs *= sf; }
                    if(ks[SDL_SCANCODE_LEFTBRACKET]) { tx->us /= sf; tx->vs /= sf; }
                    tx->us = clampf(tx->us, 0.1f, 16.0f);
                    tx->vs = clampf(tx->vs, 0.1f, 16.0f);
                    if(ks[SDL_SCANCODE_APOSTROPHE]) tx->uo += pan;
                    if(ks[SDL_SCANCODE_SEMICOLON])  tx->uo -= pan;
                    if(ks[SDL_SCANCODE_PERIOD])     tx->vo += pan;
                    if(ks[SDL_SCANCODE_COMMA])      tx->vo -= pan;
                }
            }
        }
        // report the targeted surface when it changes (groundwork for texturing)
        static int lastK = -1, lastS = -1, lastW = -1;
        if(editMode && ((int)aim.kind != lastK || aim.sector != lastS || aim.wall != lastW)){
            if(aim.kind == SurfaceRef::Wall)
                printf("aim: sector %d WALL %d\n", aim.sector, aim.wall);
            else if(aim.kind == SurfaceRef::Floor)   printf("aim: sector %d FLOOR\n",   aim.sector);
            else if(aim.kind == SurfaceRef::Ceiling) printf("aim: sector %d CEILING\n", aim.sector);
            lastK = (int)aim.kind; lastS = aim.sector; lastW = aim.wall;
        }
#endif
        player.settleEyeHeight(map, dt);
        }   // end 3D update branch

        // ---- draw ----
        renderer.clear(0xFF0a0c12);
#if EDITOR
        if(mapView){
            VHit hv, hw; Vec2 proj;
            if(!drawing){                          // no hover highlight while drawing
                hv = pickVertex(map, mvScale, mvOx, mvOy, mouseX, mouseY);
                if(hv.idx < 0) hw = pickWall(map, mvScale, mvOx, mvOy, mouseX, mouseY, proj);
            }
            renderer.drawMapEditor(map, mvScale, mvOx, mvOy, hv.sec, hv.idx, hw.sec, hw.idx,
                                   { player.cam.x, player.cam.y }, player.cam.angle);
            if(drawing) renderer.drawPendingSector(drawPts, mvScale, mvOx, mvOy, mouseX, mouseY);
        } else
#endif
        {
            renderer.renderWorld(map, player.cam, player.sector);
            renderer.drawSprites(map, player.cam);
            renderer.drawMinimap(map, player.cam, editMode ? aim : SurfaceRef{},
                                 map.playerStart, map.startAngle);
            renderer.crosshair(editMode ? 0xFF40ff40 : 0xFFe0e0e0);
        }

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
