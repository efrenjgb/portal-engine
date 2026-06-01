/* ============================================================================
 *  build_engine.c  —  a tiny "Build engine"-style portal renderer
 *  ----------------------------------------------------------------------------
 *  Duke Nukem 3D / Shadow Warrior were built on Ken Silverman's BUILD engine.
 *  Unlike Doom (which used a precomputed BSP), Build describes the world as a
 *  set of SECTORS: 2D convex-ish polygons, each with its own FLOOR and CEILING
 *  height. Walls are the edges of those polygons. A wall is either SOLID (faces
 *  the void) or a PORTAL that connects to a neighbouring sector.
 *
 *  Rendering is done with a PORTAL FLOOD:
 *    1. Start in the sector the camera is standing in. Push it on a queue with
 *       a horizontal screen window [sx1..sx2] (initially the whole screen).
 *    2. For each wall: transform its two endpoints into camera space, clip them
 *       against the near plane, and project to screen columns x1..x2.
 *    3. For every screen column the wall covers, draw the ceiling above it, the
 *       floor below it, and the wall itself — but clamped to a per-column
 *       vertical window [ytop[x]..ybottom[x]] that records "what's still empty".
 *    4. If the wall is a PORTAL, we don't fill the middle: we draw only the
 *       upper "step" (where our ceiling is lower than the neighbour's) and the
 *       lower "step" (where our floor is higher), shrink the per-column window
 *       to the remaining opening, and queue the neighbour sector to be drawn
 *       through that opening.
 *
 *  That's the whole magic. No z-buffer, no overdraw of hidden geometry — the
 *  per-column window + the screen window do all the occlusion. This is exactly
 *  how Build (and Bisqwit's well-known teaching renderer) work.
 *
 *  Build:  cc build_engine.c -o build_engine $(sdl2-config --cflags --libs) -lm
 *  Run:    ./build_engine
 * ==========================================================================*/

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>

/* ----------------------------- configuration ---------------------------- */
#define W   960          /* render width  (pixels)                          */
#define H   600          /* render height (pixels)                          */

#define FOV_H     (90.0f * (float)M_PI / 180.0f)  /* horizontal field of view */
#define EYE       1.62f  /* camera height above the floor (metres)          */
#define MAX_STEP  0.51f  /* tallest step you can walk up                    */
#define BODY      1.70f  /* min ceiling clearance to fit through a portal   */
#define MOVE_SPD  3.4f   /* metres / second                                 */
#define FOG_DIST  34.0f  /* distance at which shading reaches its darkest    */

static float F;          /* focal length in pixels, derived from FOV_H      */

/* ----------------------------- small helpers ---------------------------- */
typedef struct { float x, y; } vec2;

static int   maxi(int a,int b){ return a>b?a:b; }
static int   mini(int a,int b){ return a<b?a:b; }
static int   clampi(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }
static float maxf(float a,float b){ return a>b?a:b; }
static float minf(float a,float b){ return a<b?a:b; }

/* ------------------------------- the map -------------------------------- *
 *  Each sector is a CCW loop of vertices. Wall s runs from vert[s] to
 *  vert[(s+1)%n]. neighbors[s] is the index of the sector on the far side of
 *  that wall, or -1 if the wall is solid.
 *
 *  Three sectors connected in a line by full-width doorway portals, each at a
 *  different floor/ceiling height so you can see (and walk up) the steps:
 *
 *        y
 *        ^   +-----------+   sector 2  (floor 0.90, ceil 3.80)  — tall room
 *        |   |           |
 *        |   |    .---.   |
 *        |   +----| 1 |---+   sector 1  (floor 0.45, ceil 2.50)  — low corridor
 *        |        '---'
 *        |   +----.   .---+   sector 0  (floor 0.00, ceil 3.00)  — start room
 *        |   |           |
 *        |   +-----------+
 *        +-------------------> x
 */
typedef struct {
    float    floor, ceil;     /* heights, world units (z up)                */
    vec2    *vert;            /* npoints vertices, CCW                       */
    int     *neigh;          /* npoints neighbour sector indices (-1=solid) */
    int      npoints;
    uint32_t floorcol, ceilcol, wallcol;  /* base colours                   */
} Sector;

/* sector 0 — start room (with a doorway gap in its north wall at x=3..7) */
static vec2 s0v[] = {{0,0},{10,0},{10,8},{7,8},{3,8},{0,8}};
static int  s0n[] = {  -1,    -1,    -1,    1,   -1,   -1};

/* sector 1 — narrow corridor connecting the two rooms */
static vec2 s1v[] = {{3,8},{7,8},{7,12},{3,12}};
static int  s1n[] = {   0,   -1,     2,    -1};

/* sector 2 — tall back room (doorway gap in its south wall at x=3..7) */
static vec2 s2v[] = {{0,12},{3,12},{7,12},{10,12},{10,20},{0,20}};
static int  s2n[] = {   -1,     1,    -1,     -1,     -1,    -1};

static Sector sectors[] = {
    { 0.00f, 3.00f, s0v, s0n, 6, 0xFF243447, 0xFF1b2230, 0xFF6f7d8c },
    { 0.45f, 2.50f, s1v, s1n, 4, 0xFF3a2d22, 0xFF241c15, 0xFF9c7b52 },
    { 0.90f, 3.80f, s2v, s2n, 6, 0xFF203a2a, 0xFF14241a, 0xFF5f9c74 },
};
#define NSECT ((int)(sizeof(sectors)/sizeof(sectors[0])))

/* ------------------------------- the player ----------------------------- */
struct {
    float x, y, z;        /* eye position (z is computed from sector floor)  */
    float angle;          /* yaw, radians; forward = (cos,sin)               */
    float pitch;          /* look up/down, applied as a vertical shear       */
    float vsin, vcos;     /* cached sin/cos of angle                         */
    int   sector;         /* which sector the eye is currently inside        */
} P = { 5.0f, 4.0f, 0.0f, (float)M_PI/2.0f, 0.0f, 1.0f, 0.0f, 0 };

/* ------------------------------ framebuffer ----------------------------- */
static uint32_t *fb;      /* W*H, 0xAARRGGBB                                  */

static uint32_t shade(uint32_t c, float f){
    if(f < 0) f = 0; if(f > 1) f = 1;
    int r = (int)(((c>>16)&255) * f);
    int g = (int)(((c>> 8)&255) * f);
    int b = (int)(( c     &255) * f);
    return 0xFF000000u | (r<<16) | (g<<8) | b;
}
static float distfade(float depth){            /* 1 near -> small far */
    float f = 1.0f - depth / FOG_DIST;
    return f < 0.12f ? 0.12f : f;
}

/* Procedural brick texture, sampled in WORLD coordinates so it stays glued to
 * the wall (no swimming). u = distance along the wall, v = height. */
static uint32_t tex_sample(uint32_t base, float u, float v){
    const float bw = 1.0f, bh = 0.5f;          /* brick width / height        */
    float vv  = v / bh;
    int   row = (int)floorf(vv);
    float fu  = u / bw + ((row & 1) ? 0.5f : 0.0f);   /* offset every other row */
    float du  = fu - floorf(fu);
    float dv  = vv - floorf(vv);
    if(du < 0.06f || dv < 0.10f) return 0xFF2c2c2c;    /* mortar lines         */
    unsigned id = (unsigned)((int)floorf(fu) * 73856093) ^ (unsigned)(row * 19349663);
    id = id * 1103515245u + 12345u;                    /* deterministic jitter */
    float var = 0.82f + 0.18f * ((id >> 16) & 255) / 255.0f;
    return shade(base, var);
}

/* Solid vertical span of one colour, clipped to the screen. */
static void vspan(int x, int y0, int y1, uint32_t c){
    if(x < 0 || x >= W) return;
    if(y0 < 0) y0 = 0; if(y1 > H-1) y1 = H-1;
    for(int y = y0; y <= y1; ++y) fb[y*W + x] = c;
}

/* Textured vertical span of a wall piece.
 *  [yTopf..yBotf] = the wall piece's full screen extent (used for v interp)
 *  [clipT..clipB] = the visible window we're allowed to draw into
 *  vTop/vBot      = world heights at the top/bottom of this piece            */
static void wall_span(int x, float yTopf, float yBotf, float vTop, float vBot,
                      int clipT, int clipB, float u, uint32_t base, float depth){
    int y0 = maxi((int)yTopf, clipT);
    int y1 = mini((int)yBotf, clipB);
    if(y0 < 0) y0 = 0; if(y1 > H-1) y1 = H-1;
    float span = yBotf - yTopf; if(span == 0) span = 1.0f;
    float fade = distfade(depth);
    for(int y = y0; y <= y1; ++y){
        float fy = (y - yTopf) / span;             /* 0 at top, 1 at bottom   */
        float v  = vTop + (vBot - vTop) * fy;       /* world height (linear:   */
                                                    /* depth is const in a col)*/
        fb[y*W + x] = shade(tex_sample(base, u, v), fade);
    }
}

/* Intersection point of the infinite lines (p1->p2) and (p3->p4). */
static vec2 intersect(float x1,float y1,float x2,float y2,
                      float x3,float y3,float x4,float y4){
    float d = (x1-x2)*(y3-y4) - (y1-y2)*(x3-x4);
    if(fabsf(d) < 1e-9f) d = 1e-9f;
    float a = x1*y2 - y1*x2;
    float b = x3*y4 - y3*x4;
    vec2 r;
    r.x = (a*(x3-x4) - (x1-x2)*b) / d;
    r.y = (a*(y3-y4) - (y1-y2)*b) / d;
    return r;
}

/* =========================================================================
 *  THE RENDERER  —  portal flood fill
 * =======================================================================*/
static void render_world(void){
    static int ytop[W], ybot[W];           /* per-column open vertical window */
    int seen[NSECT];
    for(int x = 0; x < W; ++x){ ytop[x] = 0; ybot[x] = H-1; }
    for(int i = 0; i < NSECT; ++i) seen[i] = 0;

    /* circular queue of sectors-to-draw, each with a screen window */
    typedef struct { int sect, sx1, sx2; } Item;
    Item queue[64];
    int head = 0, tail = 0, count = 0;
    queue[head] = (Item){ P.sector, 0, W-1 };
    head = (head+1) & 63; count++;

    while(count > 0){
        Item now = queue[tail];
        tail = (tail+1) & 63; count--;

        if(seen[now.sect]) continue;       /* simple maps: draw each once     */
        seen[now.sect] = 1;
        Sector *sec = &sectors[now.sect];

        for(int s = 0; s < sec->npoints; ++s){
            vec2 a = sec->vert[s];
            vec2 b = sec->vert[(s+1) % sec->npoints];

            /* --- transform endpoints into camera space ------------------- *
             *  tz = depth along the view direction, tx = lateral offset.    */
            float vx1 = a.x - P.x, vy1 = a.y - P.y;
            float vx2 = b.x - P.x, vy2 = b.y - P.y;
            float tx1 = vx1*P.vsin - vy1*P.vcos, tz1 = vx1*P.vcos + vy1*P.vsin;
            float tx2 = vx2*P.vsin - vy2*P.vcos, tz2 = vx2*P.vcos + vy2*P.vsin;

            float wlen = sqrtf((b.x-a.x)*(b.x-a.x) + (b.y-a.y)*(b.y-a.y));
            float u1 = 0.0f, u2 = wlen;     /* texture coord along the wall    */

            if(tz1 <= 0 && tz2 <= 0) continue;          /* fully behind us     */

            /* --- clip against the near plane / view frustum -------------- *
             *  Frustum side slope is tan(FOV/2) = (W/2)/F. We find where the
             *  wall crosses each frustum edge and snap the behind-camera end
             *  to it, remapping its texture coordinate proportionally.       */
            if(tz1 <= 0 || tz2 <= 0){
                float nz = 0.0001f, fz = 256.0f;
                float slope = (float)(W/2) / F;          /* near/far x extents */
                vec2 il = intersect(tx1,tz1, tx2,tz2, -nz*slope,nz, -fz*slope,fz);
                vec2 ir = intersect(tx1,tz1, tx2,tz2,  nz*slope,nz,  fz*slope,fz);
                vec2 ip = (il.y > 0) ? il : ir;
                /* fraction of the wall (by depth) at the clip point, so the
                 * texture coordinate of the snapped endpoint stays correct */
                float oz1 = tz1, oz2 = tz2;
                float t = (oz2 != oz1) ? (ip.y - oz1) / (oz2 - oz1) : 0.0f;
                if(t < 0) t = 0; if(t > 1) t = 1;
                if(tz1 < nz){ u1 = 0.0f + wlen * t; tx1 = ip.x; tz1 = ip.y; }
                if(tz2 < nz){ u2 = 0.0f + wlen * t; tx2 = ip.x; tz2 = ip.y; }
            }

            /* --- project to screen columns ------------------------------ *
             *  Compact-renderer convention: screen_x = W/2 - tx*F/tz.       */
            float xs1 = F / tz1, xs2 = F / tz2;
            int   x1  = (int)(W/2 - tx1 * xs1);
            int   x2  = (int)(W/2 - tx2 * xs2);
            if(x1 >= x2) continue;                       /* back-face cull     */
            if(x2 < now.sx1 || x1 > now.sx2) continue;   /* outside window     */

            /* heights of our floor/ceiling (relative to the eye) */
            float yc = sec->ceil  - P.z;
            float yf = sec->floor - P.z;
            int nb = sec->neigh[s];
            float nyc = 0, nyf = 0;
            if(nb >= 0){ nyc = sectors[nb].ceil - P.z; nyf = sectors[nb].floor - P.z; }

            /* project ceiling/floor edges at both ends. The pitch shear adds
             * tz*pitch to each height so looking up/down tilts the world. */
            #define YPROJ(hgt, tz, sc) (H/2.0f - ((hgt) + (tz)*P.pitch) * (sc))
            float y1a = YPROJ(yc , tz1, xs1), y1b = YPROJ(yf , tz1, xs1);
            float y2a = YPROJ(yc , tz2, xs2), y2b = YPROJ(yf , tz2, xs2);
            float n1a = YPROJ(nyc, tz1, xs1), n1b = YPROJ(nyf, tz1, xs1);
            float n2a = YPROJ(nyc, tz2, xs2), n2b = YPROJ(nyf, tz2, xs2);

            /* perspective-correct horizontal texture coord uses 1/z, which is
             * linear in screen-x. Vertical screen edges are linear in x too. */
            float iz1 = 1.0f/tz1, iz2 = 1.0f/tz2;
            float uoz1 = u1*iz1,  uoz2 = u2*iz2;

            int beginx = maxi(x1, now.sx1), endx = mini(x2, now.sx2);
            float invspan = 1.0f / (float)(x2 - x1);

            for(int x = beginx; x <= endx; ++x){
                float t   = (x - x1) * invspan;
                float iz  = iz1 + (iz2 - iz1) * t;       /* 1/depth at column   */
                float dep = 1.0f / iz;                   /* perspective depth   */
                float u   = (uoz1 + (uoz2 - uoz1) * t) / iz;

                float yaf = y1a + (y2a - y1a) * t;       /* our ceiling edge    */
                float ybf = y1b + (y2b - y1b) * t;       /* our floor edge      */
                int   wt  = ytop[x], wb = ybot[x];       /* current open window */

                int cya = clampi((int)yaf, wt, wb);
                int cyb = clampi((int)ybf, wt, wb);

                /* ceiling above the wall, floor below it (flat-shaded) */
                vspan(x, wt, cya-1, shade(sec->ceilcol , distfade(dep)));
                vspan(x, cyb+1, wb, shade(sec->floorcol, distfade(dep)));

                if(nb < 0){
                    /* solid wall: textured top-to-bottom */
                    wall_span(x, yaf, ybf, sec->ceil, sec->floor,
                              wt, wb, u, sec->wallcol, dep);
                } else {
                    float naf = n1a + (n2a - n1a) * t;   /* neighbour ceiling   */
                    float nbf = n1b + (n2b - n1b) * t;   /* neighbour floor     */
                    /* upper step: our ceiling down to the (lower) neighbour's */
                    wall_span(x, yaf, naf, sec->ceil, sectors[nb].ceil,
                              wt, wb, u, sec->wallcol, dep);
                    /* lower step: (higher) neighbour floor down to ours       */
                    wall_span(x, nbf, ybf, sectors[nb].floor, sec->floor,
                              wt, wb, u, sec->wallcol, dep);
                    /* shrink the open window to the portal opening that's left */
                    ytop[x] = clampi(maxi((int)yaf, (int)naf), wt, H-1);
                    ybot[x] = clampi(mini((int)ybf, (int)nbf), 0,  wb);
                }
            }

            /* queue the neighbour to be drawn through this opening */
            if(nb >= 0 && endx >= beginx && count < 63){
                queue[head] = (Item){ nb, beginx, endx };
                head = (head+1) & 63; count++;
            }
            #undef YPROJ
        }
    }
}

/* =========================================================================
 *  2D minimap overlay (top-left) — handy for understanding what you see
 * =======================================================================*/
static void putpx(int x,int y,uint32_t c){ if(x>=0&&x<W&&y>=0&&y<H) fb[y*W+x]=c; }
static void line2d(int x0,int y0,int x1,int y1,uint32_t c){
    int dx=abs(x1-x0), sx=x0<x1?1:-1, dy=-abs(y1-y0), sy=y0<y1?1:-1, e=dx+dy;
    for(;;){ putpx(x0,y0,c); if(x0==x1&&y0==y1) break;
        int e2=2*e; if(e2>=dy){e+=dy;x0+=sx;} if(e2<=dx){e+=dx;y0+=sy;} }
}
static void draw_minimap(void){
    const float sc = 7.0f; const int ox = 14, oy = 14, maxy = 20;
    #define MX(wx) (ox + (int)((wx)*sc))
    #define MY(wy) (oy + (int)((maxy-(wy))*sc))    /* flip so north is up */
    for(int i = 0; i < NSECT; ++i){
        Sector *s = &sectors[i];
        for(int w = 0; w < s->npoints; ++w){
            vec2 a = s->vert[w], b = s->vert[(w+1)%s->npoints];
            uint32_t c = (s->neigh[w] >= 0) ? 0xFF35c06a : 0xFFb0b8c0; /* portal=green */
            line2d(MX(a.x), MY(a.y), MX(b.x), MY(b.y), c);
        }
    }
    int px = MX(P.x), py = MY(P.y);
    line2d(px, py, MX(P.x + P.vcos*1.6f), MY(P.y + P.vsin*1.6f), 0xFFffd040);
    for(int dx=-1;dx<=1;dx++) for(int dy=-1;dy<=1;dy++) putpx(px+dx,py+dy,0xFFff4040);
    #undef MX
    #undef MY
}

/* =========================================================================
 *  movement, collision and sector tracking
 * =======================================================================*/
static float point_side(float px,float py, vec2 a, vec2 b){
    /* >0 means the point is on the interior (left) side of CCW edge a->b */
    return (b.x-a.x)*(py-a.y) - (b.y-a.y)*(px-a.x);
}
static int box_overlap(float ax,float ay,float bx,float by,
                       float cx,float cy,float dx,float dy){
    return minf(ax,bx) <= maxf(cx,dx) && minf(cx,dx) <= maxf(ax,bx)
        && minf(ay,by) <= maxf(cy,dy) && minf(cy,dy) <= maxf(ay,by);
}

static void move_player(float dx, float dy){
    Sector *sec = &sectors[P.sector];
    for(int s = 0; s < sec->npoints; ++s){
        vec2 a = sec->vert[s], b = sec->vert[(s+1) % sec->npoints];
        /* about to step over to the outside of this wall? */
        if(box_overlap(P.x, P.y, P.x+dx, P.y+dy, a.x, a.y, b.x, b.y)
           && point_side(P.x+dx, P.y+dy, a, b) < 0){
            int nb = sec->neigh[s];
            int passable = 0;
            if(nb >= 0){
                float step = sectors[nb].floor - sec->floor;
                float head = sectors[nb].ceil  - sectors[nb].floor;
                if(step <= MAX_STEP && head >= BODY) passable = 1;
            }
            if(passable){
                P.sector = nb;                  /* walk through the portal     */
            } else {
                /* slide: keep only the component of motion along the wall */
                float wx = b.x-a.x, wy = b.y-a.y;
                float k = (dx*wx + dy*wy) / (wx*wx + wy*wy);
                dx = wx*k; dy = wy*k;
            }
        }
    }
    P.x += dx; P.y += dy;
}

/* =========================================================================
 *  main loop
 * =======================================================================*/
int main(void){
    F = (W / 2.0f) / tanf(FOV_H * 0.5f);

    if(SDL_Init(SDL_INIT_VIDEO) != 0){
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }
    SDL_Window  *win = SDL_CreateWindow("Build-style portal engine",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, 0);
    SDL_Renderer*ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, W, H);
    fb = malloc(sizeof(uint32_t) * W * H);
    SDL_SetRelativeMouseMode(SDL_TRUE);

    printf("\n  Build-style portal engine\n"
             "  -------------------------\n"
             "   WASD / arrows : move & strafe\n"
             "   mouse         : look (turn + pitch)\n"
             "   Q / E         : turn left / right (keyboard)\n"
             "   R / F         : look up / down (keyboard)\n"
             "   M             : release / recapture mouse\n"
             "   Esc           : quit\n\n");

    int running = 1, mouse_grabbed = 1;
    Uint32 prev = SDL_GetTicks();

    while(running){
        float mdx = 0, mdy = 0;
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type == SDL_QUIT) running = 0;
            else if(e.type == SDL_MOUSEMOTION && mouse_grabbed){
                mdx += e.motion.xrel; mdy += e.motion.yrel;
            } else if(e.type == SDL_KEYDOWN){
                if(e.key.keysym.sym == SDLK_ESCAPE) running = 0;
                if(e.key.keysym.sym == SDLK_m){
                    mouse_grabbed = !mouse_grabbed;
                    SDL_SetRelativeMouseMode(mouse_grabbed ? SDL_TRUE : SDL_FALSE);
                }
            }
        }

        Uint32 nowt = SDL_GetTicks();
        float dt = (nowt - prev) / 1000.0f;
        if(dt > 0.05f) dt = 0.05f;                 /* clamp big hitches */
        prev = nowt;

        /* ---- look ---- */
        P.angle += mdx * 0.0030f;
        P.pitch -= mdy * 0.0018f;
        const Uint8 *k = SDL_GetKeyboardState(NULL);
        if(k[SDL_SCANCODE_Q]) P.angle -= 1.8f * dt;
        if(k[SDL_SCANCODE_E]) P.angle += 1.8f * dt;
        if(k[SDL_SCANCODE_R]) P.pitch += 1.2f * dt;
        if(k[SDL_SCANCODE_F]) P.pitch -= 1.2f * dt;
        P.pitch = maxf(-0.55f, minf(0.55f, P.pitch));
        P.vsin = sinf(P.angle); P.vcos = cosf(P.angle);

        /* ---- move ---- */
        float fwd = 0, str = 0;
        if(k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP])    fwd += 1;
        if(k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN])  fwd -= 1;
        if(k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) str += 1;
        if(k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT])  str -= 1;
        if(fwd || str){
            float sp = MOVE_SPD * dt;
            /* forward = (cos,sin); strafe-right = (sin,-cos) */
            float dx = (P.vcos*fwd + P.vsin*str) * sp;
            float dy = (P.vsin*fwd - P.vcos*str) * sp;
            move_player(dx, dy);
        }

        /* ---- keep the eye at the right height for the current floor ---- */
        float ground = sectors[P.sector].floor + EYE;
        P.z += (ground - P.z) * minf(1.0f, dt * 12.0f);   /* smooth step */

        /* ---- draw ---- */
        for(int i = 0; i < W*H; ++i) fb[i] = 0xFF0a0c12;  /* clear to dark */
        render_world();
        draw_minimap();
        /* crosshair */
        for(int i=-5;i<=5;i++){ putpx(W/2+i,H/2,0xFFe0e0e0); putpx(W/2,H/2+i,0xFFe0e0e0); }

        SDL_UpdateTexture(tex, NULL, fb, W * sizeof(uint32_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }

    free(fb);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
