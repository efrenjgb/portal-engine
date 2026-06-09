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
./portal_engine [mapfile] [--novsync]   # mapfile defaults to ./map.txt; --novsync uncaps FPS
```

The `EDITOR` macro (default 1, set in `Vec2.h`) `#if`-strips all editor input, the
2D map editor, and the per-pixel pick buffer. `make play` passes `-DEDITOR=0`.
After a `make play`, run plain `make` to get the editor build back.

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

**Texture picker (EDITOR).** `B` in the 3D view opens `Renderer::drawTextureBrowser`,
a modal thumbnail grid of the PNGs under `textures/duke/` (lazy-loaded into a browse
pool on first open). Clicking a tile calls `ensureTexture` (appends it to `map.textures`
+ `texSet` only if new, so saves stay minimal) and sets the aimed wall/floor/ceiling
`textureId` or the aimed sprite's `textureId`. Image textures carry alpha (loadImage
forces RGBA); walls stay opaque via `shade`, sprites cut out on alpha < 128.
`F` filters the grid All/Solid/Masked — tiles are classified once at load by their
transparent-texel fraction (>5% = masked/sprite-like), and the picker opens on Solid
for a surface, Masked for a sprite. (BUILD's `.ART` has no wall-vs-sprite flag, so
this is a transparency heuristic, not metadata.)

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

## Conventions

- **Commit per logical change** (this repo's history is meant to be read step by step),
  and write plain commit messages with **no `Co-Authored-By` trailer**.
- The IDE linter reports false errors here (missing `SDL.h`, no `std::optional`/`std::string`)
  because it doesn't pass `-std=c++17` or the SDL include path. Ignore them; trust `make`
  (which builds clean with `-Wall -Wextra`).
- Gotcha: positive `pitch` looks **down**.
