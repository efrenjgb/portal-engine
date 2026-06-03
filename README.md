# A tiny Build-engine-style portal renderer

A small C++17 + SDL2 prototype that renders a 3D scene the way Ken Silverman's
**BUILD** engine (Duke Nukem 3D, Shadow Warrior, Blood) did — using **sectors,
walls, and a portal flood**, with no BSP tree.

```sh
make run                 # builds (c++ -std=c++17, sdl2-config) and launches map.txt
./build_engine [mapfile] # defaults to ./map.txt
make play                # stripped build: no editor, no pick buffer (-DEDITOR=0)
```

The default build includes the in-engine editor and its per-pixel pick buffer.
`make play` compiles with `-DEDITOR=0`, which `#if`-strips the editor input and
the pick buffer entirely (saving ~2.25 MB and a per-pixel write) — a lean
"runtime" binary. Run plain `make` afterwards to get the editor build back.

The world is read from a **text map file** (`map.txt`) at startup, so you can
edit the level without touching the code. See the comments at the top of `map.txt`
for the format (sectors with floor/ceiling heights and colours, walls with
per-vertex neighbour links, sprites, and the player start).

### Code layout

| File | Responsibility |
|---|---|
| `Vec2.h` | value math + colour helpers (`shade`, `distFade`) |
| `Camera.h` | the camera pose (position, yaw, pitch) |
| `Map.h` / `Map.cpp` | `Sector`/`Sprite`/`Map` data + text load/save |
| `Renderer.h` / `Renderer.cpp` | portal flood, floor casting, sprites, minimap (no SDL) |
| `Player.h` / `Player.cpp` | movement, collision, sector picking |
| `main.cpp` | SDL window/input, the editor, the main loop |

The renderer only touches a pixel buffer, never SDL, so it could be unit-tested
headlessly. (Originally this was a single C file; see the git history.)

**Controls:** `WASD`/arrows move & strafe · mouse looks (turn + pitch) ·
`Q`/`E` turn · `R`/`F` pitch · `Space` jump · `M` release mouse · `Esc` quit.
`Tab` toggles **edit mode**, which acts on the surface under the crosshair:
`T`/`G` raise/lower the *floor or ceiling* you're aiming at; `[`/`]` shrink/grow
the texture, `;`/`'` pan it horizontally and `,`/`.` vertically, and `N` cycles
the surface's image texture. `P` sets the
player start to where you're standing (shown cyan on the minimap). `K` saves the
edited world to `<mapfile>.save` (reload it with `./build_engine <mapfile>.save`).
The green lines on the top-left minimap are **portals**; grey lines are solid
walls; in edit mode the **magenta** outline is the sector you're aiming at and a
**yellow** edge is the specific wall under the crosshair.

---

## How a Build engine differs from Doom

| | Doom | Build |
|---|---|---|
| World model | lines + a precomputed **BSP** tree | **sectors** = 2D polygons with floor/ceil heights |
| Heights | one floor, one ceiling per sector | same, but sectors edited freely in real time |
| Hidden-surface removal | BSP back-to-front + a 1D occlusion array | **portal flood** + per-column occlusion |
| Looking up/down | not really | a **y-shear** fake (we do this too) |

Both are "2.5D": the world is fundamentally a 2D floor plan, and walls are always
vertical. That constraint is what makes the math cheap enough for a 1996 CPU —
and what makes it a great thing to learn from.

## The eleven techniques in this prototype

**1. Sectors & walls (`Sector` struct, the map data).**
The world is just three polygons. Each has a `floor`/`ceil` height and a list of
vertices in counter-clockwise order. Wall `s` is the edge from `vert[s]` to
`vert[s+1]`. `neigh[s]` says which sector is on the other side of that wall, or
`-1` if it's solid. A "doorway" is made by splitting a wall into solid–portal–solid
segments (see how sector 0's north wall has a gap at x = 2..8).

**2. The portal flood (`render_world`).**
A queue holds *(sector, screen-column-window)* jobs. We start with the player's
sector and the full-width window `[0..W-1]`. For each wall we transform, clip,
project, and draw — and whenever a wall is a portal, we queue the neighbour
sector restricted to the screen columns of that opening. Visiting only what's
visible through openings means we never draw hidden geometry. No BSP needed.

**3. Per-column vertical occlusion (`ytop[]` / `ybottom[]`).**
For every screen column we remember the still-empty vertical band. Drawing a
solid wall, or the upper/lower **steps** of a portal, shrinks that band. The next
sector drawn through the portal is clamped to it. These two arrays *are* the
entire occlusion system — they're why you see the floor step up into the corridor
and the ceiling change height, all correctly occluded.

**4. Projection + perspective-correct texturing.**
Each wall endpoint is rotated into camera space (`tx` lateral, `tz` depth) and
projected with `screen_x = W/2 + tx*F/tz`, so a point to the player's right lands
on the right of the screen (not mirrored). With this convention a front-facing
wall comes out right-to-left, so we swap its endpoints to keep the column loop
running left-to-right. The top and bottom edges of a wall are straight 3D lines,
so they project to straight screen lines — meaning their screen Y is **linear in
screen X** (no per-pixel divide needed vertically). Horizontally, `1/z` and `u/z`
are linear in screen X, so we interpolate those and divide once per column to get
a perspective-correct texture coordinate `u`. Within a column the depth is
constant, so the vertical texture coordinate is linear too. That's the classic
affine-per-column / perspective-per-span trick.

**5. The pitch "look up/down" shear (`YPROJ` macro).**
True pitch would tilt the camera and break the "walls are vertical" assumption.
Build fakes it: it adds `tz * pitch` to every projected height. Cheap, and it's
exactly what Duke Nukem 3D did — which is why looking far up/down there always
looked a little stretched.

**6. Textured floors & ceilings via floor casting (`plane_span`).**
Walls are drawn per *column*; floors and ceilings are the dual — drawn per
*pixel* by inverse projection. A floor is a flat horizontal plane, so an entire
screen row sits at one depth `tz` (distance from the camera depends only on how
far the pixel is below the horizon). We invert the projection to get `tz`,
recover the column's lateral offset `tx`, rotate back into world space to get
`(wx, wy)`, and sample the texture there. Because we sample in world
coordinates, the tiled floor stays locked to the ground as you move — the same
idea as a Wolfenstein-style floor caster, fitted into the portal renderer.

**7. Partial-height portals: doorways, lintels and windows.**
A portal's opening runs from `max(floorA, floorB)` at the bottom to
`min(ceilA, ceilB)` at the top; the leftover is drawn as solid wall — a riser
below and a header (lintel) above. So just by choosing a neighbour's floor and
ceiling you get different openings, with no special cases in the renderer:
the corridor's low ceiling puts a header over each doorway, and the start
room's east wall has a **window** into a small alcove whose floor is a high
sill and whose ceiling is a low header, leaving a mid-height band. The window
is non-walkable for free: its sill is taller than `MAX_STEP`, so the same
collision check that lets you step through a doorway refuses it. A horizontal
opening (the doorway/window gap *across* the wall) is made by splitting the
wall into solid–portal–solid segments in the map data.

**8. Sprites (billboards) with a per-pixel depth buffer (`draw_sprites`).**
Items/monsters are flat camera-facing billboards. As walls, floors and ceilings
are drawn they record their depth into `zbuf[x + y*W]` (one value per *pixel*).
Each sprite is then projected (same `tx`, `screen_x` and height-shear as a wall),
the sprites are sorted far-to-near, and each pixel is drawn only where the sprite
is nearer than what's already in `zbuf`. Per-pixel depth (rather than one value
per column) is what lets a lintel, a step or a floor correctly hide part of a
sprite — e.g. a barrel seen through a far doorway is clipped to the opening
instead of poking over the header. (The period-accurate Build/Doom alternative
is to sort sprites and walls per sector and clip with per-column spans; a depth
buffer is the simpler, more general way to the same result.)

**9. Live editing + surface picking (`Tab`, `Renderer::pickAt`).**
Build's signature trick was editing the world while walking around in it. As the
renderer draws each wall/floor/ceiling it also stamps a packed *surface ID* into
a per-pixel pick buffer (alongside the depth buffer). `pickAt(x, y)` then decodes
the pixel under the crosshair into a `SurfaceRef` — `{sector, kind (floor /
ceiling / wall), wall index}` — so we know exactly which surface you're looking
at, even through a doorway or window. Press `Tab` and `T`/`G`, `Y`/`H` change the
aimed sector's floor/ceiling in real time (nothing is precomputed — the next
frame just renders the new heights). The pick is the groundwork for per-surface
editing like textures: it's specific down to the individual wall.

**10. Per-surface texture wrapping (`TexXform`).**
Every wall and every sector floor/ceiling carries a texture transform
`{us, vs, uo, vo}` — scale and offset. The samplers map `world / scale + offset`,
so larger scale stretches the texture (fewer repeats) and the offset pans it.
In edit mode the texture keys (`[` `]` `;` `'` `,` `.`) adjust the transform of
the exact surface under the crosshair (from the pick buffer), and it's saved
per-surface in the map file. Defaults (`1,1,0,0`) reproduce the world-locked
tiling, so existing maps look unchanged.

**11. Image textures (`Texture`, `loadImage`).**
Surfaces can use an image instead of the procedural pattern. Textures are listed
in the map (`texture <file>`, index = id) and loaded into a `Texture` (an RGBA
buffer) at startup; each wall/floor/ceiling has a texture id (-1 = procedural).
The samplers wrap the image with the same `TexXform` scale/offset as everything
else. `loadImage` decodes **PNG/JPG/BMP/TGA** via the vendored single-header
`stb_image.h` (its implementation is isolated in `stb_image_impl.cpp`), with a
tiny built-in **PPM** reader as a fallback. Sample PNGs live in `textures/`
(regenerate with `tools/gen_textures.py`); in edit mode `N` cycles a surface's
texture and it's saved per-surface.

Distance shading (fog) and procedural textures (brick walls, tiled floors) are
the fallback when a surface has no image assigned.

## Things deliberately left out (good next steps)

- **Sloped floors**, and **"sector over sector"** stacking (Build's room-over-room
  trick via teleporting portals).

## Where to read more

- Ken Silverman's original BUILD source and notes (advsys.net/ken).
- Bisqwit's "Creating a Doom-style 3D engine in C" video + code — the compact
  teaching renderer this prototype's structure follows.

Vendored: `stb_image.h` (Sean Barrett / nothings.org, public domain) for image
decoding.
