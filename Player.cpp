#include "Player.h"
#include <cmath>
#include <algorithm>

// Signed area of triangle (a,b,c) — which side of a->b is c on.
static float orient(float ax, float ay, float bx, float by, float cx, float cy){
    return (bx-ax)*(cy-ay) - (by-ay)*(cx-ax);
}
// Do segments p1->p2 and p3->p4 properly cross? (collinear cases ignored)
static bool segCross(float x1,float y1,float x2,float y2,
                     float x3,float y3,float x4,float y4){
    float o1 = orient(x1,y1,x2,y2,x3,y3), o2 = orient(x1,y1,x2,y2,x4,y4);
    float o3 = orient(x3,y3,x4,y4,x1,y1), o4 = orient(x3,y3,x4,y4,x2,y2);
    return (o1*o2 < 0) && (o3*o4 < 0);
}

void Player::move(const Map& map, float dx, float dy){
    for(int iter = 0; iter < 4; ++iter){            // slide, then re-check
        const Sector& sec = map.sectors[sector];
        int n = (int)sec.vert.size(), hit = -1;
        for(int s = 0; s < n; ++s){
            Vec2 a = sec.vert[s], b = sec.vert[(s+1) % n];
            if(segCross(cam.x, cam.y, cam.x+dx, cam.y+dy, a.x, a.y, b.x, b.y)){ hit = s; break; }
        }
        if(hit < 0) break;

        Vec2 a = sec.vert[hit], b = sec.vert[(hit+1) % n];
        int nb = sec.neigh[hit];
        bool passable = false;
        if(nb >= 0){
            float step = map.sectors[nb].floor - sec.floor;
            float head = map.sectors[nb].ceil  - map.sectors[nb].floor;
            if(step <= MAX_STEP && head >= BODY) passable = true;
        }
        if(passable){ sector = nb; break; }         // step through the portal

        float wx = b.x-a.x, wy = b.y-a.y;           // slide along the wall
        float k = (dx*wx + dy*wy) / (wx*wx + wy*wy);
        dx = wx*k; dy = wy*k;
    }
    cam.x += dx; cam.y += dy;
}

void Player::keepInside(const Map& map){
    const Sector& sec = map.sectors[sector];
    int n = (int)sec.vert.size();
    for(int s = 0; s < n; ++s){
        Vec2 a = sec.vert[s], b = sec.vert[(s+1) % n];
        // Portals: stand a bit farther off than the renderer's near plane, so a
        // portal wall is never *inside* the near plane (which would make the
        // flood drop it and black out the view). Solid walls use PLAYER_R.
        float margin = (sec.neigh[s] < 0) ? PLAYER_R : (NEAR_PLANE + 0.03f);
        float ex = b.x - a.x, ey = b.y - a.y;
        float L2 = ex*ex + ey*ey;
        if(L2 < 1e-12f) continue;
        float t = ((cam.x - a.x)*ex + (cam.y - a.y)*ey) / L2;
        if(t < 0) t = 0; if(t > 1) t = 1;
        float dx = cam.x - (a.x + ex*t), dy = cam.y - (a.y + ey*t);
        float d = std::sqrt(dx*dx + dy*dy);
        if(d < margin){
            if(d > 1e-6f){ cam.x += dx/d * (margin - d); cam.y += dy/d * (margin - d); }
            else { float L = std::sqrt(L2); cam.x += -ey/L*margin; cam.y += ex/L*margin; }
        }
    }
}

void Player::collideSprites(const Map& map){
    float feet = map.sectors[sector].floor, head = feet + BODY;
    for(const Sprite& s : map.sprites){
        if(s.z + s.height < feet || s.z > head) continue;   // no height overlap
        float dx = cam.x - s.pos.x, dy = cam.y - s.pos.y;
        float r  = PLAYER_R + s.radius;
        float d2 = dx*dx + dy*dy;
        if(d2 >= r*r) continue;
        float d = std::sqrt(d2);
        if(d > 1e-6f){ cam.x += dx/d * (r - d); cam.y += dy/d * (r - d); }
        else         { cam.x += r; }
    }
}

int Player::pickSector(const Map& map) const {
    float px = cam.x, py = cam.y, dx = cam.vcos, dy = cam.vsin;
    int sec = sector;
    for(int iter = 0; iter < 64; ++iter){
        const Sector& s = map.sectors[sec];
        int n = (int)s.vert.size();
        float bt = 1e30f; int bw = -1;
        for(int w = 0; w < n; ++w){
            Vec2 a = s.vert[w], b = s.vert[(w+1) % n];
            float ex = b.x - a.x, ey = b.y - a.y;
            float den = dx*ey - dy*ex;
            if(std::fabs(den) < 1e-9f) continue;
            float t = ((a.x-px)*ey - (a.y-py)*ex) / den;
            float u = ((a.x-px)*dy - (a.y-py)*dx) / den;
            if(t > 1e-4f && u >= 0.0f && u <= 1.0f && t < bt){ bt = t; bw = w; }
        }
        if(bw < 0) break;
        int nb = s.neigh[bw];
        if(nb < 0) break;
        sec = nb;
        px += dx * (bt + 1e-3f);
        py += dy * (bt + 1e-3f);
    }
    return sec;
}

void Player::settleEyeHeight(const Map& map, float dt){
    float ground = map.sectors[sector].floor + EYE;
    cam.z += (ground - cam.z) * std::min(1.0f, dt * 12.0f);
}
