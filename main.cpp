// Entry point + platform glue: load the map, open an SDL window, run the
// input/update/render loop, and host the in-engine height editor.
#include <SDL.h>
#include <cstdio>
#include <string>
#include <utility>
#include <algorithm>
#include <vector>
#include "Map.h"
#include "Texture.h"
#include "Renderer.h"
#include "Player.h"

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

    if(argc>2){
        std::vector<Texture> ts; for(auto&p:map.textures){auto t=loadImage(p);ts.push_back(t?std::move(*t):Texture{});}
        Renderer r; r.setTextures(&ts);
        int bad=0; for(int d=0;d<360;d+=20){ player.cam.angle=d*PI_F/180; player.cam.update(); r.clear(0xFF0a0c12); r.renderWorld(map,player.cam,player.sector); int bg=0; for(int i=0;i<Renderer::W*Renderer::H;i++) if(r.pixels()[i]==0xFF0a0c12u) bg++; if(bg>50){bad++; printf("angle %d: %d bg\n",d,bg);} }
        printf("angles with holes: %d\n", bad); return 0;
    }
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
             "   M             : release / recapture mouse\n",
             EDITOR ? "" : "  [play build]");
#if EDITOR
    printf("   Tab           : toggle edit mode\n"
             "     T / G       :   raise / lower FLOOR   of aimed sector\n"
             "     Y / H       :   raise / lower CEILING of aimed sector\n"
             "     [ / ]       :   shrink / grow texture on aimed surface\n"
             "     ; / '       :   pan texture horizontally\n"
             "     , / .       :   pan texture vertically\n"
             "   N             :   cycle image texture on aimed surface\n"
             "   P             : set player start to current position\n"
             "   K             : save edited map (to <mapfile>.save)\n");
#endif
    printf("   Esc           : quit\n\n");

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
#if EDITOR
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

        // ---- what's under the crosshair + the height editor (editor builds) ----
        SurfaceRef aim;                       // stays None in a play build
#if EDITOR
        aim = renderer.pickAt(W/2, H/2);      // from last frame's pick buffer
        if(editMode && aim.sector >= 0 && aim.sector < (int)map.sectors.size()){
            Sector& t = map.sectors[aim.sector];
            float rate = 2.0f * dt;
            if(ks[SDL_SCANCODE_T]) t.floor = std::min(t.floor + rate, t.ceil - 0.3f);
            if(ks[SDL_SCANCODE_G]) t.floor = std::max(t.floor - rate, -8.0f);
            if(ks[SDL_SCANCODE_Y]) t.ceil  = std::min(t.ceil  + rate,  18.0f);
            if(ks[SDL_SCANCODE_H]) t.ceil  = std::max(t.ceil  - rate,  t.floor + 0.3f);

            // texture wrap/pan on the exact surface under the crosshair
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
