#include "Player.h"
#include <cmath>
#include <algorithm>

// Signed area of triangle (a,b,c) — which side of a->b is c on.
static float orient(float ax, float ay, float bx, float by, float cx, float cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}
// Do segments p1->p2 and p3->p4 properly cross? (collinear cases ignored)
static bool segCross(float x1, float y1, float x2, float y2, float x3, float y3, float x4,
                     float y4) {
    float o1 = orient(x1, y1, x2, y2, x3, y3), o2 = orient(x1, y1, x2, y2, x4, y4);
    float o3 = orient(x3, y3, x4, y4, x1, y1), o4 = orient(x3, y3, x4, y4, x2, y2);
    return (o1 * o2 < 0) && (o3 * o4 < 0);
}

void Player::move(const Map& map, float dx, float dy) {
    for(int iter = 0; iter < 4; ++iter) { // slide, then re-check
        const Sector& sec = map.sectors[sector];
        int n = (int)sec.vertices.size(), hit = -1;
        for(int s = 0; s < n; ++s) {
            Vec2 a = sec.vertices[s], b = sec.vertices[(s + 1) % n];
            if(segCross(camera.x, camera.y, camera.x + dx, camera.y + dy, a.x, a.y, b.x, b.y)) {
                hit = s;
                break;
            }
        }
        if(hit < 0) break;

        Vec2 a = sec.vertices[hit], b = sec.vertices[(hit + 1) % n];
        int nb = sec.neighbors[hit];
        bool passable = false;
        if(nb >= 0) {
            float step = map.sectors[nb].floor - sec.floor;
            float head = map.sectors[nb].ceiling - map.sectors[nb].floor;
            if(step <= MAX_STEP && head >= PLAYER_HEIGHT) passable = true;
        }
        if(passable) {
            sector = nb;
            break;
        } // step through the portal

        float wx = b.x - a.x, wy = b.y - a.y; // slide along the wall
        float k = (dx * wx + dy * wy) / (wx * wx + wy * wy);
        dx = wx * k;
        dy = wy * k;
    }
    camera.x += dx;
    camera.y += dy;
}

void Player::keepInside(const Map& map) {
    const Sector& sec = map.sectors[sector];
    int n = (int)sec.vertices.size();
    for(int s = 0; s < n; ++s) {
        Vec2 a = sec.vertices[s], b = sec.vertices[(s + 1) % n];
        // Solid walls keep a full body radius; portals only a hair, so you can
        // walk right through them (the renderer's frustum clip handles being on
        // top of a portal, so no larger standoff is needed).
        float margin = (sec.neighbors[s] < 0) ? PLAYER_RADIUS : 0.01f;
        float ex = b.x - a.x, ey = b.y - a.y;
        float L2 = ex * ex + ey * ey;
        if(L2 < 1e-12f) continue;
        float t = ((camera.x - a.x) * ex + (camera.y - a.y) * ey) / L2;
        if(t < 0) t = 0;
        if(t > 1) t = 1;
        float dx = camera.x - (a.x + ex * t), dy = camera.y - (a.y + ey * t);
        float d = std::sqrt(dx * dx + dy * dy);
        if(d < margin) {
            if(d > 1e-6f) {
                camera.x += dx / d * (margin - d);
                camera.y += dy / d * (margin - d);
            } else {
                float L = std::sqrt(L2);
                camera.x += -ey / L * margin;
                camera.y += ex / L * margin;
            }
        }
    }
}

void Player::collideSprites(const Map& map) {
    float feet = map.sectors[sector].floor, head = feet + PLAYER_HEIGHT;
    for(const Sprite& s : map.sprites) {
        if(s.z + s.height < feet || s.z > head) continue; // no height overlap
        float dx = camera.x - s.position.x, dy = camera.y - s.position.y;
        float r = PLAYER_RADIUS + s.radius;
        float d2 = dx * dx + dy * dy;
        if(d2 >= r * r) continue;
        float d = std::sqrt(d2);
        if(d > 1e-6f) {
            camera.x += dx / d * (r - d);
            camera.y += dy / d * (r - d);
        } else {
            camera.x += r;
        }
    }
}

int Player::pickSector(const Map& map) const {
    float px = camera.x, py = camera.y, dx = camera.yawCos, dy = camera.yawSin;
    int sec = sector;
    for(int iter = 0; iter < 64; ++iter) {
        const Sector& s = map.sectors[sec];
        int n = (int)s.vertices.size();
        float bt = 1e30f;
        int bw = -1;
        for(int w = 0; w < n; ++w) {
            Vec2 a = s.vertices[w], b = s.vertices[(w + 1) % n];
            float ex = b.x - a.x, ey = b.y - a.y;
            float den = dx * ey - dy * ex;
            if(std::fabs(den) < 1e-9f) continue;
            float t = ((a.x - px) * ey - (a.y - py) * ex) / den;
            float u = ((a.x - px) * dy - (a.y - py) * dx) / den;
            if(t > 1e-4f && u >= 0.0f && u <= 1.0f && t < bt) {
                bt = t;
                bw = w;
            }
        }
        if(bw < 0) break;
        int nb = s.neighbors[bw];
        if(nb < 0) break;
        sec = nb;
        px += dx * (bt + 1e-3f);
        py += dy * (bt + 1e-3f);
    }
    return sec;
}

void Player::jump() {
    if(onGround) {
        zVelocity = JUMP_VELOCITY;
        onGround = false;
    }
}

void Player::settleEyeHeight(const Map& map, float dt) {
    float groundEye = map.sectors[sector].floor + PLAYER_EYE_HEIGHT;
    if(onGround && zVelocity == 0.0f) {
        // standing/walking: ease the eye to the floor (smooth over steps)
        camera.z += (groundEye - camera.z) * std::min(1.0f, dt * 12.0f);
    } else {
        // airborne: integrate gravity, land on the floor
        zVelocity -= GRAVITY * dt;
        camera.z += zVelocity * dt;
        if(camera.z <= groundEye) {
            camera.z = groundEye;
            zVelocity = 0.0f;
            onGround = true;
        } else onGround = false;
    }
}
