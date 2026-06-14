# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A from-scratch software renderer in the style of Ken Silverman's **BUILD** engine
(Duke Nukem 3D): sector/portal world model, portal-flood hidden-surface removal,
no BSP. C++17 + SDL2, single window, software rasterized into one pixel buffer.
It is a learning project — favor clarity and match the surrounding style.

## Build / run

```sh
make          # editor build -> ./portal_engine  (EDITOR=1: in-engine editor + pick buffer)
make run      # build, then ./portal_engine (loads ./map.txt)
make play     # stripped build (-DEDITOR=0): no editor, no pick buffer; cleans first
make clean
./portal_engine [mapfile] [--novsync] [--res WxH] [--extract <file.grp>]
                                        # mapfile defaults to ./map.txt;
                                        # --novsync uncaps FPS; --res sets the framebuffer
                                        # (default 1024x768, 4:3). e.g. --res 1600x1200
                                        # --extract decodes a BUILD .GRP to textures/duke/
```

**GRP texture extraction.** `GrpExtract.{h,cpp}` is a C++ port of `tools/grp_extract.py`
built into the binary (writes PNGs via the vendored `stb_image_write.h`, impl in
`stb_image_impl.cpp`). At startup `main.cpp` runs it for an explicit `--extract <grp>`,
or auto-extracts a `*.grp` found in the cwd / beside the binary when `textures/duke/`
is empty — so a release binary produces the Duke art with no Python. It's SDL-free and
in both builds; pixel-identical to the Python tool (same magenta colour-key + `anim.txt`).

The `EDITOR` macro (default 1, set in `Vec2.h`) `#if`-strips all editor input, the
2D map editor, and the per-pixel pick buffer. `make play` passes `-DEDITOR=0`.
After a `make play`, run plain `make` to get the editor build back.

The `Makefile` is the primary local path (macOS/Linux). `CMakeLists.txt` mirrors
it for cross-platform / Windows builds (`-DPORTAL_EDITOR=OFF` is the `make play`
equivalent) and is what `.github/workflows/release.yml` drives to publish
self-contained release zips per OS (bundled SDL2 + `map.txt` + base textures;
`.github/smoke-test.sh` headless-launches each to catch a broken binary). When
changing sources, build flags, or the asset set, keep all three in sync.

## Testing

There is no test framework. The renderer is deliberately **SDL-free** (`Renderer.cpp`
only fills a pixel buffer), so verification is done with standalone headless programs:

- Write a throwaway `/tmp/*.cpp` that `#include`s the headers and links
  `Map.cpp Renderer.cpp Player.cpp Texture.cpp stb_image_impl.cpp` (NOT `main.cpp`):
  ```sh
  c++ -std=c++17 -O2 -I. /tmp/t.cpp Map.cpp Renderer.cpp Player.cpp Texture.cpp stb_image_impl.cpp -o /tmp/t
  ```
- Drive it with a `Renderer` + `Camera`/`Player`, call `renderWorld`, then inspect
  `renderer.pixels()`. Two patterns used heavily:
  - **Hole/regression scan**: count `0xFF0a0c12` (background) pixels over a full
    angle sweep — should be 0 for a valid map.
  - **Visual check**: dump the buffer as a PPM and `sips -s format png in.ppm --out out.png`,
    then view the PNG. For a before/after fix, build the old version from `git stash`
    into a second binary and diff the rendered PPMs pixel-by-pixel.
- NEVER add debug probes to `main.cpp`; use these standalone files.

The editor (`EDITOR=1`) build is the one to test renderer changes against, since it
also exercises the pick buffer.

## Architecture

The whole world is 2.5D: a list of **sectors**, each a CCW polygon (`vertices`) with
a single `floor`/`ceiling` height. Wall `s` is the edge `vertices[s] -> vertices[s+1]`;
`neighbors[s]` is the sector on the far side (`-1` = solid wall, else a **portal**).
There is no BSP.

**Rendering (`Renderer::renderWorld`, the core).** A BFS queue of
`(sector, screen-column-window)` jobs starting at the player's sector. For each wall:
transform to camera space, **frustum-clip** (near plane + both side planes — this is
what keeps projected x in `[0,W]` and kills texture jitter on grazing walls), project
with the **un-mirrored** `screen_x = W/2 + tx*F/tz` (front faces come out right-to-left,
so endpoints are swapped to iterate L→R), and draw. Portals draw the upper/lower steps
as wall, leave the opening, and queue the neighbour clamped to the opening's columns.
Pitch is a fake **y-shear** (`YPROJ` adds `tz*pitch`), not a real camera tilt.

Three cooperating occlusion mechanisms — understand all three before touching the loop:
1. **Per-column `columnTop_/columnBottom_`** window: the fast path; correct for convex sectors.
2. **Per-sector-entry snapshot `et/eb`**: each sector clamps its *own* walls to a
   frozen copy of the window so an earlier portal in the wall list can't chop a later
   solid wall (required for concave sectors made in the editor). Portals shrink the
   *live* `columnTop_/columnBottom_` for the sectors they queue.
3. **Per-pixel z-buffer (`depthBuffer_`)**: `wallSpan`/`planeSpan`/`skySpan` test it and keep
   the nearer surface. This is the backstop for concave/overlapping geometry and also
   does sprite occlusion. Any new surface writer must z-test.

A sector may be drawn **once per portal window** (concave sectors and split portals
make it visible through several openings); `MAX_VISITS` bounds revisits and back-facing
portals are culled, so there's no flood back into the sector you came from.

Floors/ceilings are drawn per-pixel by inverse projection (`planeSpan`, "floor casting").
Sky is a static screen-locked backdrop (`skySpan`, `ceilingIsSky` per sector). Sprites are
billboards sorted far-to-near and z-tested (`drawSprites`).

Lighting is **per-surface** (like BUILD): each wall has its own `wallLight[w]` and the
sector has `floorLight`/`ceilingLight` (1 = normal). The spans multiply their distance
fade by the relevant value; sprites use their sector's `floorLight` (`sectorLightAt`).
`shade()` clamps channels so light > 1 brightens. `Y`/`H` edit the exact surface under
the crosshair. Map format: floor/ceil light are two optional trailing floats on the
`sector` line; wall light is an optional field after the wall's `texId` (a legacy
single sector light is read and spread to floor/ceiling/walls). `wallLight` is kept
parallel to `vertices` everywhere walls are inserted/erased.

**Texture picker (EDITOR).** `B` in the 3D view opens `Renderer::drawTextureBrowser`,
a modal thumbnail grid of the PNGs under `textures/duke/` (lazy-loaded into a browse
pool on first open). Clicking a tile calls `ensureTexture` (appends it to `map.textures`
+ `texSet` only if new, so saves stay minimal) and sets the aimed wall/floor/ceiling
`textureId` or the aimed sprite's `textureId`. Transparency is a **magenta
colour-key**: `grp_extract.py` exports BUILD's index-255 key as opaque magenta, and
`isClear()` (Texture.h, magenta *or* alpha < 128) is honoured on *every* render
path — `wallSpan`/`planeSpan` skip keyed texels (read through to whatever's behind)
and `drawSprites` cuts them out. So a masked tile reads through on a wall/floor
instead of showing a black hole.
`F` filters the grid All/Solid/Masked — tiles are classified once at load by their
magenta-key fraction (>5% keyed = masked/sprite-like, via `isClear`), and the picker
opens on Solid for a surface, Masked for a sprite. `F` toggles the two filters
Solid/Masked. (BUILD's `.ART` has no wall-vs-sprite flag, so this is a transparency
heuristic, not metadata.) Picking a tile for a sprite sizes it to the texture's pixel
aspect (`fitSprite`, world-per-texel `k`); `[`/`]` resizes the aimed sprite uniformly
in 3D (keeping aspect), while `T`/`G` still move its height (z) along the sector.

**Animated textures.** `grp_extract.py` decodes BUILD's `picanm` into
`textures/duke/anim.txt` (`base_tile num type speed` per animated tile; frames are
the consecutive tiles `base..base+num`). At load, `main.cpp` reads it and, for every
map texture that is an animated base, loads its frame tiles into `texSet`
(and `map.textures`, to keep them 1:1) and records a `TexAnim` (Texture.h);
`animOf[texId]` indexes into `anims`, or `-1`. `Renderer::imageFor` resolves an
animated id to the current frame via `animFrame()` off `animTime_`, which the main
loop drives with `advanceAnim(dt)`. `setupAnim` runs at load and when a tile is
picked in the editor, so animated tiles animate immediately. `type` is 1=oscillating
2=forward 3=backward; rate is `ANIM_HZ / (1<<speed)` (Renderer.cpp).

**HUD text** is a built-in 5x7 bitmap font (`Renderer::drawText`, font table in
`Renderer.cpp`) drawn straight into the framebuffer — no SDL_ttf. It uppercases input
and covers digits, A-Z and a little punctuation; the top byte of the colour is an
alpha that alpha-blends (so text can fade). The main loop uses it for an FPS counter
(top-right, smoothed EMA of the real frame time) and a small **toast queue**
(`showMessage`, bottom-left, fades out) that editor actions push to — save, set start,
draw/delete, grid toggle, etc.

**Surface picking (EDITOR only).** As it draws, the renderer stamps a packed
`kind|wall|sector` id into `pickBuffer_` (and `drawSprites` stamps `kind=Sprite|index`
for visible sprite pixels); `pickAt(x,y)` decodes the pixel under the crosshair into
a `SurfaceRef` (a wall/floor/ceiling of a sector, or a sprite). This drives all
in-engine editing (which exact thing a command affects — e.g. `T`/`G` move a sprite's
height when aimed at one, else the sector's floor/ceiling).

**Module roles** (relationships that aren't obvious from filenames):
- `Renderer.{h,cpp}` — all rasterization; **no SDL dependency** (keep it that way).
- `main.cpp` — the only SDL/platform code: window, input loop, height editor, and the
  full-screen 2D map editor (all `#if EDITOR`).
- `Map.{h,cpp}` — `Sector`/`Sprite`/`Map` data + the text map loader/saver.
- `Player.{h,cpp}` — movement, wall/sprite collision, sector finding, jump/gravity.
- `Texture.{h,cpp}` + vendored `stb_image.h` (impl isolated in `stb_image_impl.cpp`).
- `GrpExtract.{h,cpp}` — SDL-free BUILD `.GRP` -> PNG extractor; writes via vendored
  `stb_image_write.h` (impl also in `stb_image_impl.cpp`).

## Map format & the 2D editor

Maps are a text file (see header comment in `map.txt`): `texture`, `player`,
`sector floor ceil floorcol ceilcol wallcol` (colors RRGGBB hex), `wall x y neigh
[us vs uo vo [texId]]`, `floortex`/`ceiltex`, `ceilsky`, `sprite`. Per-surface texture
transforms are `TextureTransform{uScale,vScale,uOffset,vOffset}` (sample = world/scale + offset); `texId = -1`
means the procedural texture. The editor saves to `<mapfile>.save` (never overwrites
the original); load it back with `./portal_engine <mapfile>.save`. Loading a file that
already ends in `.save` saves back to it in place (no `.save.save` chaining).

In the 2D editor (`Enter`), **portals are derived from geometry**: after every edit
`rebuildPortals` relinks each wall `a->b` to whichever sector owns the reversed wall
`b->a`. So a portal is created by making two sectors' walls coincide (grid snap helps)
and broken when they no longer match. Coincident vertices (portal seams) are moved/
deleted together to keep portals matched. Drawing a new sector (`B`, `addSector`)
snaps each point to the grid or an existing vertex, forces CCW winding, inherits
heights/colours via `pointInSector`, and relies on the same `rebuildPortals` step to
bond any shared edge — so there is no explicit "connect" operation anywhere. Sprites
are drawn as diamonds and can be dragged to set their x/y (`pickSprite`); their
height is set in 3D. `N` adds a sprite at the cursor (resting on the sector floor via
`pointInSector`); Delete/X removes the hovered sprite (or vertex). Undo (`Z`)
snapshots both sectors and sprites.

**Inner loops & cutouts.** A `Sector` is an outer CCW polygon plus zero or more
inner **hole loops** (CW) — `loopStart[k]` marks each loop's first vertex; the
parallel wall arrays stay one-per-wall across all loops. `Sector::wallEnd(w)` gives
a wall's far vertex wrapping within *its own* loop, and every wall-iteration site
(renderer, collision, `rebuildPortals`, `pointInSector`, editor) uses it — so it's a
no-op for ordinary single-loop sectors. Drawing a polygon (`B`) **fully inside** an
existing sector makes a **cutout** (`addCutout`): the parent gains a hole loop
(`pts` reversed) and a new inner sector (CCW, inheriting the parent's look) is
created; `rebuildPortals` bonds them into portals both ways. Then raise the inner
floor to its ceiling for a **column**, lower the inner ceiling for a **recess**, or
lower the inner floor for a **pit**. Map format: a `loop` line begins an inner loop.
Rendering a cutout takes three cooperating parts, all independent of whether the
portal flood reaches the inner sector (a single-range column window can't reliably
reach many overlapping holes):
1. **Inverted cull for hole walls.** Inner loops are wound CW, so the back-face test
   is flipped for them (`holeWall ? cross>=0 : cross<=0`) — otherwise a cutout's
   camera-facing drop faces are culled and the away faces drawn. This is what makes
   the rim risers appear. The parent also draws the drop-face step (`wallSpan`) even
   when the inner floor dips below / ceiling rises above ours (the step would
   otherwise be an empty span).
2. **planeSpan skip.** The parent floor/ceiling skips pixels over a *far-side* cutout
   (a pit, or an inner ceiling above ours — `farHoleInner`) so it doesn't paint over
   it. A nearer platform/recess is left to the z-buffer.
3. **`drawCutouts`.** After a sector's walls are in the depth buffer, paint each
   cutout's inner floor/ceiling clipped to its hole loop and z-tested — so it fills
   the cutout's full screen extent (even a deep pit's centre) yet loses to the rim
   risers (no floor-over-wall).

**Doors & lifts.** A sector can be a `mover` (Sector in `Map.h`): a **door** (1)
animates its **ceiling** between the floor (closed) and `moverRest` (the authored
ceiling = open); a **lift** (2) animates its **floor** between `moverRest` (down)
and the highest neighbouring floor (up). Endpoints are derived from geometry — no
extra authoring. `updateMovers` (main.cpp, runs in both builds) eases the surface
toward its target each frame; the `use` key (`E`) calls `Player::aimMoverSector`
(a forward portal raycast, so no pick buffer needed) and toggles the one you face.
A closed door's height gap is below `PLAYER_HEIGHT`, so the existing move/collision
code blocks you until it opens — no special case. `C` in the 3D editor cycles the
aimed sector none/door/lift. Map format: a `mover door|lift [speed]` line on the
sector; the saver writes the surface's rest height (not its mid-animation value).

## Conventions

- **Commit per logical change** (this repo's history is meant to be read step by step),
  and write plain commit messages with **no `Co-Authored-By` trailer**.
- The IDE linter reports false errors here (missing `SDL.h`, no `std::optional`/`std::string`)
  because it doesn't pass `-std=c++17` or the SDL include path. Ignore them; trust `make`
  (which builds clean with `-Wall -Wextra`).
- Gotcha: positive `pitch` looks **down**.
