#include "Renderer.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

// ---- procedural textures (file-local) --------------------------------------
static uint32_t texSample(uint32_t base, float u, float v){     // brick walls
    const float bw = 1.0f, bh = 0.5f;
    float vv  = v / bh;
    int   row = (int)std::floor(vv);
    float fu  = u / bw + ((row & 1) ? 0.5f : 0.0f);
    float du  = fu - std::floor(fu);
    float dv  = vv - std::floor(vv);
    if(du < 0.06f || dv < 0.10f) return 0xFF2c2c2c;             // mortar
    unsigned id = (unsigned)((int)std::floor(fu) * 73856093) ^ (unsigned)(row * 19349663);
    id = id * 1103515245u + 12345u;
    float var = 0.82f + 0.18f * ((id >> 16) & 255) / 255.0f;
    return shade(base, var);
}

static uint32_t planeTex(uint32_t base, float wx, float wy){   // tiled floors
    const float tile = 1.0f;
    float fu = wx/tile - std::floor(wx/tile);
    float fv = wy/tile - std::floor(wy/tile);
    if(fu < 0.04f || fv < 0.04f) return 0xFF1c1c1c;            // grout
    int checker = (((int)std::floor(wx/tile)) + ((int)std::floor(wy/tile))) & 1;
    return shade(base, checker ? 1.0f : 0.80f);
}

static uint32_t spriteTex(uint32_t base, float u, float v){    // barrel/drum
    float cu = (u - 0.5f) * 2.0f;
    const float r = 0.82f;
    if(std::fabs(cu) > r || v < 0.0f || v > 1.0f) return 0;     // transparent
    float round = std::sqrt(1.0f - (cu/r)*(cu/r));
    float f = 0.45f + 0.55f * round;
    if(v < 0.06f || v > 0.95f ||
       std::fabs(v-0.30f) < 0.04f || std::fabs(v-0.70f) < 0.04f) f *= 0.55f;
    return shade(base, f);
}

// Pack a surface identity into one word: kind(4) | wall(12) | sector(16).
// 0 == nothing (SurfaceRef::None), which is what the pick buffer clears to.
static inline uint32_t packSurf(int sector, int kind, int wall){
    return ((uint32_t)kind << 28) | (((uint32_t)wall & 0xFFF) << 16) | ((uint32_t)sector & 0xFFFF);
}

// ---- Renderer --------------------------------------------------------------
Renderer::Renderer()
    : F_((W / 2.0f) / std::tan(0.5f * 90.0f * PI_F / 180.0f)),
      fb_(W*H), zbuf_(W*H),
#if EDITOR
      pickbuf_(W*H),
#endif
      ytop_(W), ybot_(W) {}

void Renderer::clear(uint32_t bg){ std::fill(fb_.begin(), fb_.end(), bg); }

void Renderer::putpx(int x, int y, uint32_t c){
    if(x >= 0 && x < W && y >= 0 && y < H) fb_[y*W + x] = c;
}
void Renderer::line2d(int x0, int y0, int x1, int y1, uint32_t c){
    int dx = std::abs(x1-x0), sx = x0<x1?1:-1, dy = -std::abs(y1-y0), sy = y0<y1?1:-1, e = dx+dy;
    for(;;){ putpx(x0,y0,c); if(x0==x1 && y0==y1) break;
        int e2 = 2*e; if(e2 >= dy){ e += dy; x0 += sx; } if(e2 <= dx){ e += dx; y0 += sy; } }
}

// Choose the image texture for an id, or nullptr to fall back to procedural.
const Texture* Renderer::imageFor(int texId) const {
    if(texId >= 0 && textures_ && texId < (int)textures_->size() && !(*textures_)[texId].px.empty())
        return &(*textures_)[texId];
    return nullptr;
}

void Renderer::wallSpan(int x, float yTopf, float yBotf, float vTop, float vBot,
                        int clipT, int clipB, float u, uint32_t base, float depth,
                        [[maybe_unused]] uint32_t surf, const TexXform& tx, int texId){
    int y0 = std::max((int)yTopf, clipT);
    int y1 = std::min((int)yBotf, clipB);
    if(y0 < 0) y0 = 0; if(y1 > H-1) y1 = H-1;
    float span = yBotf - yTopf; if(span == 0) span = 1.0f;
    float fade = distFade(depth);
    float su = u / tx.us + tx.uo;                 // texture coord along the wall
    const Texture* img = imageFor(texId);
    for(int y = y0; y <= y1; ++y){
        int idx = y*W + x;
        if(depth >= zbuf_[idx]) continue;             // z-test: keep the nearer surface
        float fy = (y - yTopf) / span;
        float v  = (vTop + (vBot - vTop) * fy) / tx.vs + tx.vo;
        uint32_t c = img ? img->at(su, v) : texSample(base, su, v);
        fb_[idx]      = shade(c, fade);
        zbuf_[idx]    = depth;
#if EDITOR
        pickbuf_[idx] = surf;
#endif
    }
}

void Renderer::planeSpan(const Camera& P, int x, int y0, int y1, float pz, uint32_t base,
                         [[maybe_unused]] uint32_t surf, const TexXform& tx, int texId){
    if(y0 < 0) y0 = 0; if(y1 > H-1) y1 = H-1;
    float h = pz - P.z;
    const Texture* img = imageFor(texId);
    for(int y = y0; y <= y1; ++y){
        float denom = (H * 0.5f - y) - P.pitch * F_;
        float tz = h * F_ / denom;
        if(tz <= 0.0001f) continue;
        int idx = y*W + x;
        if(tz >= zbuf_[idx]) continue;                // z-test: keep the nearer surface
        float txc = (x - W * 0.5f) * tz / F_;
        float wx = P.x + P.vcos * tz + P.vsin * txc;
        float wy = P.y + P.vsin * tz - P.vcos * txc;
        float sx = wx / tx.us + tx.uo, sy = wy / tx.vs + tx.vo;
        uint32_t c = img ? img->at(sx, sy) : planeTex(base, sx, sy);
        fb_[idx]      = shade(c, distFade(tz));
        zbuf_[idx]    = tz;
#if EDITOR
        pickbuf_[idx] = surf;
#endif
    }
}

// Static sky backdrop (Build-style): the texture is locked to the screen — it
// doesn't move as you turn, look or walk. Just fills the ceiling region with the
// image (or procedural pattern), sampled by screen position.
void Renderer::skySpan(int x, int y0, int y1, uint32_t base, int texId,
                       [[maybe_unused]] uint32_t surf){
    if(y0 < 0) y0 = 0; if(y1 > H-1) y1 = H-1;
    const Texture* img = imageFor(texId);               // any assigned image, e.g. bricks
    float su = (float)x / (float)W;                     // one copy across the screen
    for(int y = y0; y <= y1; ++y){
        int idx = y*W + x;
        if(zbuf_[idx] < 1e9f) continue;                 // a nearer surface already won
        float sv = (float)y / (float)H;
        uint32_t c = img ? img->at(su, sv) : planeTex(base, su * 8.0f, sv * 4.0f);
        fb_[idx]      = c;                               // no distance fade: it's "far"
        zbuf_[idx]    = 1e9f;                            // behind everything
#if EDITOR
        pickbuf_[idx] = surf;                            // still picks as the ceiling
#endif
    }
}

void Renderer::renderWorld(const Map& map, const Camera& P, int playerSector){
    for(int x = 0; x < W; ++x){ ytop_[x] = 0; ybot_[x] = H-1; }
    std::fill(zbuf_.begin(), zbuf_.end(), 1e9f);
#if EDITOR
    std::fill(pickbuf_.begin(), pickbuf_.end(), 0u);
#endif
    // A sector may be visible through more than one portal opening (e.g. once a
    // doorway portal is split into two sub-portals in the editor), so we draw it
    // once *per window* rather than only once. A small per-sector visit cap
    // bounds the work; back-facing portals (the one we came through) are culled,
    // so there's no flood back into the sector we came from.
    constexpr int MAX_VISITS = 12;
    std::vector<char> visits(map.sectors.size(), 0);
    std::vector<int>  et(W), eb(W);   // per-sector-entry snapshot of the window

    struct Item { int sect, sx1, sx2; };
    Item queue[256];
    int head = 0, tail = 0, count = 0;
    queue[head] = { playerSector, 0, W-1 };
    head = (head+1) & 255; count++;

    while(count > 0){
        Item now = queue[tail];
        tail = (tail+1) & 255; count--;
        if(visits[now.sect] >= MAX_VISITS) continue;
        visits[now.sect]++;
        const Sector& sec = map.sectors[now.sect];
        int n = (int)sec.vert.size();

        // Freeze the incoming window: this sector's own walls clamp to et/eb, so
        // a portal earlier in the list can't chop a solid wall that comes later
        // (concave sectors). Portals still shrink the live ytop_/ybot_, which the
        // sectors we queue inherit; per-pixel depth sorts near vs far in-sector.
        for(int x = now.sx1; x <= now.sx2; ++x){ et[x] = ytop_[x]; eb[x] = ybot_[x]; }

        for(int s = 0; s < n; ++s){
            Vec2 a = sec.vert[s], b = sec.vert[(s+1) % n];

            float vx1 = a.x - P.x, vy1 = a.y - P.y;
            float vx2 = b.x - P.x, vy2 = b.y - P.y;
            float tx1 = vx1*P.vsin - vy1*P.vcos, tz1 = vx1*P.vcos + vy1*P.vsin;
            float tx2 = vx2*P.vsin - vy2*P.vcos, tz2 = vx2*P.vcos + vy2*P.vsin;

            float wlen = std::sqrt((b.x-a.x)*(b.x-a.x) + (b.y-a.y)*(b.y-a.y));
            float u1 = 0.0f, u2 = wlen;

            if(tz1 <= 0 && tz2 <= 0) continue;               // fully behind us

            float cross = tx1*tz2 - tx2*tz1;                 // back-face cull
            if(cross <= 0) continue;

            // Clip the wall to the view frustum in camera space: the near plane
            // and the two side (screen-edge) planes. Afterwards both endpoints
            // are inside the view, so the projected x lands within [0, W] —
            // bounded and precise (no texture jitter on grazing walls) and on the
            // correct side, so there's no need to keep the camera away from
            // portals. NEARZ must stay well above zero so 1/z and x stay sane.
            const float NEARZ = NEAR_PLANE;
            const float slope = (W * 0.5f) / F_;             // tan(FOV/2): screen-edge tx/tz

            // Near plane: if both ends are nearer than it (you're right on a
            // portal) clamp them; otherwise slide the near one along the wall.
            if(tz1 < NEARZ && tz2 < NEARZ){
                tz1 = NEARZ; tz2 = NEARZ;
            } else if(tz1 < NEARZ){
                float t = (NEARZ - tz1) / (tz2 - tz1); tx1 += (tx2-tx1)*t; u1 += (u2-u1)*t; tz1 = NEARZ;
            } else if(tz2 < NEARZ){
                float t = (NEARZ - tz2) / (tz1 - tz2); tx2 += (tx1-tx2)*t; u2 += (u1-u2)*t; tz2 = NEARZ;
            }

            // Side planes: clip the segment (tx,tz,u) to each half-space d >= 0.
            auto clipSide = [&](float d1, float d2) -> bool {
                if(d1 < 0 && d2 < 0) return false;           // wholly outside the view
                if(d1 < 0){      float t = d1/(d1-d2); tx1 += (tx2-tx1)*t; tz1 += (tz2-tz1)*t; u1 += (u2-u1)*t; }
                else if(d2 < 0){ float t = d1/(d1-d2); tx2  = tx1 + (tx2-tx1)*t; tz2 = tz1 + (tz2-tz1)*t; u2 = u1 + (u2-u1)*t; }
                return true;
            };
            if(!clipSide(tx1 + slope*tz1, tx2 + slope*tz2)) continue;   // left  edge
            if(!clipSide(slope*tz1 - tx1, slope*tz2 - tx2)) continue;   // right edge

            // project: screen_x = W/2 + tx*F/tz (not mirrored, now within [0,W])
            float xs1 = F_ / tz1, xs2 = F_ / tz2;
            float fx1 = W * 0.5f + tx1 * xs1;
            float fx2 = W * 0.5f + tx2 * xs2;
            if(fx1 > fx2){                                    // front faces come out R-to-L
                std::swap(fx1, fx2); std::swap(tz1, tz2); std::swap(xs1, xs2); std::swap(u1, u2);
            }
            if(fx2 - fx1 < 0.01f) continue;                  // sub-pixel sliver
            if(fx2 < now.sx1 || fx1 > now.sx2) continue;     // outside this sector's window

            float yc = sec.ceil  - P.z;
            float yf = sec.floor - P.z;
            int   nb = sec.neigh[s];
            float nyc = 0, nyf = 0;
            if(nb >= 0){ nyc = map.sectors[nb].ceil - P.z; nyf = map.sectors[nb].floor - P.z; }

            auto YPROJ = [&](float hgt, float tz, float sc){ return H/2.0f - (hgt + tz*P.pitch) * sc; };
            float y1a = YPROJ(yc , tz1, xs1), y1b = YPROJ(yf , tz1, xs1);
            float y2a = YPROJ(yc , tz2, xs2), y2b = YPROJ(yf , tz2, xs2);
            float n1a = YPROJ(nyc, tz1, xs1), n1b = YPROJ(nyf, tz1, xs1);
            float n2a = YPROJ(nyc, tz2, xs2), n2b = YPROJ(nyf, tz2, xs2);

            float iz1 = 1.0f/tz1, iz2 = 1.0f/tz2;
            float uoz1 = u1*iz1, uoz2 = u2*iz2;

            int beginx = std::max((int)fx1, now.sx1), endx = std::min((int)fx2, now.sx2);
            float invspan = 1.0f / (fx2 - fx1);              // float span: subpixel-correct

            for(int x = beginx; x <= endx; ++x){
                float t   = (x + 0.5f - fx1) * invspan;      // pixel centre vs true edge
                float iz  = iz1 + (iz2 - iz1) * t;
                float dep = 1.0f / iz;
                float u   = (uoz1 + (uoz2 - uoz1) * t) / iz;

                float yaf = y1a + (y2a - y1a) * t;
                float ybf = y1b + (y2b - y1b) * t;
                int   wt  = et[x], wb = eb[x];      // frozen entry window

                int cya = clampi((int)yaf, wt, wb);
                int cyb = clampi((int)ybf, wt, wb);

                uint32_t ceilSurf = packSurf(now.sect, SurfaceRef::Ceiling, 0);
                if(sec.ceilSky) skySpan(x, wt, cya-1, sec.ceilCol, sec.ceilTexId, ceilSurf);
                else            planeSpan(P, x, wt, cya-1, sec.ceil, sec.ceilCol, ceilSurf, sec.ceilTex, sec.ceilTexId);
                planeSpan(P, x, cyb+1, wb, sec.floor, sec.floorCol, packSurf(now.sect, SurfaceRef::Floor, 0), sec.floorTex, sec.floorTexId);

                uint32_t wsurf = packSurf(now.sect, SurfaceRef::Wall, s);
                const TexXform& wtx = sec.wallTex[s];
                int wid = sec.wallTexId[s];
                if(nb < 0){
                    wallSpan(x, yaf, ybf, sec.ceil, sec.floor, wt, wb, u, sec.wallCol, dep, wsurf, wtx, wid);
                } else {
                    float naf = n1a + (n2a - n1a) * t;
                    float nbf = n1b + (n2b - n1b) * t;
                    wallSpan(x, yaf, naf, sec.ceil, map.sectors[nb].ceil,  wt, wb, u, sec.wallCol, dep, wsurf, wtx, wid);
                    wallSpan(x, nbf, ybf, map.sectors[nb].floor, sec.floor, wt, wb, u, sec.wallCol, dep, wsurf, wtx, wid);
                    // shrink the LIVE window (monotonically) for queued children
                    ytop_[x] = std::max(ytop_[x], clampi(std::max((int)yaf, (int)naf), wt, H-1));
                    ybot_[x] = std::min(ybot_[x], clampi(std::min((int)ybf, (int)nbf), 0,  wb));
                }
            }

            if(nb >= 0 && endx >= beginx && visits[nb] < MAX_VISITS && count < 255){
                queue[head] = { nb, beginx, endx };
                head = (head+1) & 255; count++;
            }
        }
    }
}

void Renderer::drawSprites(const Map& map, const Camera& P){
    int ns = (int)map.sprites.size();
    std::vector<int> order(ns);
    std::vector<float> depth(ns);
    for(int i = 0; i < ns; ++i){
        float rx = map.sprites[i].pos.x - P.x, ry = map.sprites[i].pos.y - P.y;
        depth[i] = rx*P.vcos + ry*P.vsin;
        order[i] = i;
    }
    for(int i = 1; i < ns; ++i){                  // insertion sort, far to near
        int k = order[i]; float d = depth[k]; int j = i-1;
        while(j >= 0 && depth[order[j]] < d){ order[j+1] = order[j]; --j; }
        order[j+1] = k;
    }

    for(int o = 0; o < ns; ++o){
        const Sprite& sp = map.sprites[order[o]];
        float rx = sp.pos.x - P.x, ry = sp.pos.y - P.y;
        float tz = rx*P.vcos + ry*P.vsin;
        if(tz < 0.2f) continue;
        float tx = rx*P.vsin - ry*P.vcos;

        float sc  = F_ / tz;
        float cx  = W*0.5f + tx * sc;
        float yfeet = H*0.5f - ((sp.z             - P.z) + tz*P.pitch) * sc;
        float ytopf = H*0.5f - ((sp.z + sp.height - P.z) + tz*P.pitch) * sc;
        float wpx = sp.radius * sc;

        int x0 = (int)(cx - wpx), x1 = (int)(cx + wpx);
        int yt = (int)ytopf,      yb = (int)yfeet;
        if(x1 <= x0 || yb <= yt) continue;
        float fade = distFade(tz);

        for(int x = std::max(x0,0); x <= std::min(x1,W-1); ++x){
            float uu = (x - x0) / (float)(x1 - x0);
            for(int y = std::max(yt,0); y <= std::min(yb,H-1); ++y){
                if(tz >= zbuf_[y*W + x]) continue;
                float vv = (y - yt) / (float)(yb - yt);
                uint32_t c = spriteTex(sp.col, uu, vv);
                if(c) fb_[y*W + x] = shade(c, fade);
            }
        }
    }
}

void Renderer::drawMinimap(const Map& map, const Camera& P, SurfaceRef aim,
                           Vec2 start, float startAngle){
    const float sc = 7.0f; const int ox = 14, oy = 14, maxy = 20;
    auto MX = [&](float wx){ return ox + (int)(wx * sc); };
    auto MY = [&](float wy){ return oy + (int)((maxy - wy) * sc); };   // north up

    for(int i = 0; i < (int)map.sectors.size(); ++i){
        const Sector& s = map.sectors[i];
        int np = (int)s.vert.size();
        for(int w = 0; w < np; ++w){
            Vec2 a = s.vert[w], b = s.vert[(w+1) % np];
            uint32_t c = (s.neigh[w] >= 0) ? 0xFF35c06a : 0xFFb0b8c0;
            if(i == aim.sector) c = 0xFFff30ff;                              // aimed sector
            if(aim.kind == SurfaceRef::Wall && i == aim.sector && w == aim.wall)
                c = 0xFFffe020;                                             // aimed wall = yellow
            line2d(MX(a.x), MY(a.y), MX(b.x), MY(b.y), c);
        }
    }
    for(const Sprite& s : map.sprites){
        int sx = MX(s.pos.x), sy = MY(s.pos.y);
        for(int dx=-1;dx<=1;dx++) for(int dy=-1;dy<=1;dy++) putpx(sx+dx, sy+dy, s.col);
    }
    {   // stored player start = cyan
        int sx = MX(start.x), sy = MY(start.y);
        line2d(sx, sy, MX(start.x + std::cos(startAngle)*1.2f),
                       MY(start.y + std::sin(startAngle)*1.2f), 0xFF20e0ff);
        putpx(sx, sy, 0xFF20e0ff);
    }
    int px = MX(P.x), py = MY(P.y);
    line2d(px, py, MX(P.x + P.vcos*1.6f), MY(P.y + P.vsin*1.6f), 0xFFffd040);
    for(int dx=-1;dx<=1;dx++) for(int dy=-1;dy<=1;dy++) putpx(px+dx, py+dy, 0xFFff4040);
}

void Renderer::crosshair(uint32_t col){
    for(int i = -5; i <= 5; ++i){ putpx(W/2+i, H/2, col); putpx(W/2, H/2+i, col); }
}

void Renderer::drawMapEditor(const Map& m, float sc, float ox, float oy,
                             int hovSec, int hovVert, int hwSec, int hwWall,
                             Vec2 pp, float pa){
    auto SX = [&](float wx){ return (int)(ox + wx*sc); };
    auto SY = [&](float wy){ return (int)(oy - wy*sc); };

    // faint unit grid (only when it won't be a dense mush)
    if(sc >= 4.0f){
        int gx0 = (int)std::floor((0 - ox)/sc),     gx1 = (int)std::ceil((W - ox)/sc);
        int gy0 = (int)std::floor((oy - (H-1))/sc), gy1 = (int)std::ceil((oy - 0)/sc);
        if(gx1 - gx0 < 400) for(int gx = gx0; gx <= gx1; ++gx) line2d(SX((float)gx), 0, SX((float)gx), H-1, 0xFF15181f);
        if(gy1 - gy0 < 400) for(int gy = gy0; gy <= gy1; ++gy) line2d(0, SY((float)gy), W-1, SY((float)gy), 0xFF15181f);
    }

    // walls (portal = green, solid = grey; aimed wall = yellow)
    for(int s = 0; s < (int)m.sectors.size(); ++s){
        const Sector& S = m.sectors[s];
        int n = (int)S.vert.size();
        for(int w = 0; w < n; ++w){
            Vec2 a = S.vert[w], b = S.vert[(w+1)%n];
            uint32_t c = (S.neigh[w] >= 0) ? 0xFF35c06a : 0xFFb0b8c0;
            if(s == hwSec && w == hwWall) c = 0xFFffe020;
            line2d(SX(a.x), SY(a.y), SX(b.x), SY(b.y), c);
        }
    }
    // vertices (hovered one is a bigger yellow box)
    for(int s = 0; s < (int)m.sectors.size(); ++s){
        const Sector& S = m.sectors[s];
        for(int i = 0; i < (int)S.vert.size(); ++i){
            int vx = SX(S.vert[i].x), vy = SY(S.vert[i].y);
            bool hot = (s == hovSec && i == hovVert);
            uint32_t c = hot ? 0xFFffe020 : 0xFFdfe6f0;
            int r = hot ? 3 : 2;
            for(int dy=-r; dy<=r; ++dy) for(int dx=-r; dx<=r; ++dx) putpx(vx+dx, vy+dy, c);
        }
    }
    // player marker + facing
    int px = SX(pp.x), py = SY(pp.y);
    line2d(px, py, SX(pp.x + std::cos(pa)*1.6f), SY(pp.y + std::sin(pa)*1.6f), 0xFFffd040);
    for(int dy=-2; dy<=2; ++dy) for(int dx=-2; dx<=2; ++dx) putpx(px+dx, py+dy, 0xFFff4040);
}

void Renderer::drawPendingSector(const std::vector<Vec2>& pts, float sc, float ox, float oy,
                                 int mx, int my){
    if(pts.empty()) return;
    auto SX = [&](float wx){ return (int)(ox + wx*sc); };
    auto SY = [&](float wy){ return (int)(oy - wy*sc); };
    const uint32_t edge = 0xFFffd24a, band = 0xFF8a7320;

    for(size_t i = 0; i + 1 < pts.size(); ++i)               // committed edges
        line2d(SX(pts[i].x), SY(pts[i].y), SX(pts[i+1].x), SY(pts[i+1].y), edge);
    line2d(SX(pts.back().x), SY(pts.back().y), mx, my, band); // rubber-band to cursor
    if(pts.size() >= 2)                                       // hint of the closing edge
        line2d(mx, my, SX(pts[0].x), SY(pts[0].y), band);

    for(size_t i = 0; i < pts.size(); ++i){                  // placed points
        int vx = SX(pts[i].x), vy = SY(pts[i].y);
        int r = (i == 0) ? 3 : 2;                            // first point bigger (click to close)
        uint32_t c = (i == 0) ? 0xFFffffff : edge;
        for(int dy=-r; dy<=r; ++dy) for(int dx=-r; dx<=r; ++dx) putpx(vx+dx, vy+dy, c);
    }
}

#if EDITOR
SurfaceRef Renderer::pickAt(int x, int y) const {
    SurfaceRef r;
    if(x < 0 || x >= W || y < 0 || y >= H) return r;
    uint32_t s = pickbuf_[(size_t)y*W + x];
    int kind = (int)((s >> 28) & 0xF);
    if(kind == 0) return r;                       // background / nothing
    r.kind   = (SurfaceRef::Kind)kind;
    r.wall   = (int)((s >> 16) & 0xFFF);
    r.sector = (int)(s & 0xFFFF);
    return r;
}
#endif
