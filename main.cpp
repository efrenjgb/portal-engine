// Entry point + platform glue: load the map, open an SDL window, run the
// input/update/render loop, and host the in-engine height editor.
#include <SDL.h>
#include <cstdio>
#include <string>
#include <utility>
#include <algorithm>
#include "Map.h"
#include "Renderer.h"
#include "Player.h"

int main(int argc, char** argv){
    std::string mapPath = (argc > 1) ? argv[1] : "map.txt";
    auto loaded = loadMap(mapPath);
    if(!loaded) return 1;
    Map map = std::move(*loaded);
    std::string savePath = mapPath + ".save";

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

    printf("\n  Build-style portal engine (C++)\n"
             "  -------------------------------\n"
             "   WASD / arrows : move & strafe\n"
             "   mouse         : look (turn + pitch)\n"
             "   Q / E         : turn left / right\n"
             "   R / F         : look up / down\n"
             "   M             : release / recapture mouse\n"
             "   Tab           : toggle height-edit mode\n"
             "     T / G       :   raise / lower FLOOR   of aimed sector\n"
             "     Y / H       :   raise / lower CEILING of aimed sector\n"
             "   P             : set player start to current position\n"
             "   K             : save edited map (to <mapfile>.save)\n"
             "   Esc           : quit\n\n");

    bool running = true, mouseGrabbed = true, editMode = false;
    Uint32 prev = SDL_GetTicks();

    while(running){
        float mdx = 0, mdy = 0;
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type == SDL_QUIT) running = false;
            else if(e.type == SDL_MOUSEMOTION && mouseGrabbed){
                mdx += e.motion.xrel; mdy += e.motion.yrel;
            } else if(e.type == SDL_KEYDOWN){
                SDL_Keycode k = e.key.keysym.sym;
                if(k == SDLK_ESCAPE) running = false;
                if(k == SDLK_m){ mouseGrabbed = !mouseGrabbed;
                                 SDL_SetRelativeMouseMode(mouseGrabbed ? SDL_TRUE : SDL_FALSE); }
                if(k == SDLK_TAB) editMode = !editMode;
                if(k == SDLK_k)   saveMap(map, savePath);
                if(k == SDLK_p){
                    map.playerStart = { player.cam.x, player.cam.y };
                    map.startSector = player.sector;
                    map.startAngle  = player.cam.angle;
                    printf("player start set: %.2f %.2f sector %d facing %.0f deg\n",
                           map.playerStart.x, map.playerStart.y, map.startSector,
                           map.startAngle * 180.0f / PI_F);
                }
            }
        }

        Uint32 nowt = SDL_GetTicks();
        float dt = (nowt - prev) / 1000.0f;
        if(dt > 0.05f) dt = 0.05f;
        prev = nowt;

        // ---- look ----
        player.cam.angle -= mdx * 0.0030f;
        player.cam.pitch += mdy * 0.0018f;
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
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

        // ---- what's under the crosshair (from last frame's pick buffer) ----
        SurfaceRef aim = renderer.pickAt(W/2, H/2);

        // ---- height editor: acts on the aimed surface's sector ----
        if(editMode && aim.sector >= 0 && aim.sector < (int)map.sectors.size()){
            Sector& t = map.sectors[aim.sector];
            float rate = 2.0f * dt;
            if(ks[SDL_SCANCODE_T]) t.floor = std::min(t.floor + rate, t.ceil - 0.3f);
            if(ks[SDL_SCANCODE_G]) t.floor = std::max(t.floor - rate, -8.0f);
            if(ks[SDL_SCANCODE_Y]) t.ceil  = std::min(t.ceil  + rate,  18.0f);
            if(ks[SDL_SCANCODE_H]) t.ceil  = std::max(t.ceil  - rate,  t.floor + 0.3f);
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

        player.settleEyeHeight(map, dt);

        // ---- draw ----
        renderer.clear(0xFF0a0c12);
        renderer.renderWorld(map, player.cam, player.sector);
        renderer.drawSprites(map, player.cam);
        renderer.drawMinimap(map, player.cam, editMode ? aim : SurfaceRef{},
                             map.playerStart, map.startAngle);
        renderer.crosshair(editMode ? 0xFF40ff40 : 0xFFe0e0e0);

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
