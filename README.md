# A tiny Build-engine-style portal renderer

A ~450-line C + SDL2 prototype that renders a 3D scene the way Ken Silverman's
**BUILD** engine (Duke Nukem 3D, Shadow Warrior, Blood) did — using **sectors,
walls, and a portal flood**, with no z-buffer and no BSP tree.

```sh
make run        # builds with sdl2-config and launches
# or:
cc build_engine.c -o build_engine $(sdl2-config --cflags --libs) -lm
```

**Controls:** `WASD`/arrows move & strafe · mouse looks (turn + pitch) ·
`Q`/`E` turn · `R`/`F` pitch · `M` release mouse · `Esc` quit.
The green lines on the top-left minimap are **portals**; grey lines are solid walls.

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

## The six techniques in this prototype

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

Distance shading (fog) and procedural textures (brick walls, tiled floors) are
thrown in so it reads as a room rather than flat colours.

## Things deliberately left out (good next steps)

- **Sub-dividing walls for partial-height doors / windows.** Right now portals are
  full-width openings.
- **Sprites** (billboarded monsters/items), **sloped floors**, and **"sector over
  sector"** stacking (Build's room-over-room trick via teleporting portals).

## Where to read more

- Ken Silverman's original BUILD source and notes (advsys.net/ken).
- Bisqwit's "Creating a Doom-style 3D engine in C" video + code — the compact
  teaching renderer this prototype's structure follows.
