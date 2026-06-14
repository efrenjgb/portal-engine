# Roadmap / enhancements

Running list of possible enhancements for the portal engine, so work can resume
across sessions. Done items kept for context; open items grouped by theme.

## Done
- Sector/portal software renderer; textured walls/floors/ceilings; static sky
- Sprites (billboards) with per-pixel depth; image-textured sprites + transparency
- Jump/gravity, collision, portal step-through
- 2D map editor: drag/split/delete vertices, draw new sectors (`B`), geometry-derived
  portals, grid snap (Alt bypass), undo, add/move/delete sprites
- 3D editor: per-surface texture wrap/pan, texture cycle (`N`), texture **picker**
  (`B`) browsing imported tiles with All/Solid/Masked filter, sky toggle (`O`)
- **Doors / lifts + `use` key** (`E`): tagged sectors (`C` in 3D editor) animate a
  surface on use — door drives the ceiling, lift the floor; works in `make play`
- Per-surface **lighting** (per-wall + floor/ceiling), `Y`/`H`
- Height edit by camera pitch / aimed surface (`T`/`G`)
- HUD text (built-in 5x7 font), FPS counter, transient toast messages
- Save/load text maps (overwrite-in-place for `.save`), `set player start`
- `tools/grp_extract.py`: BUILD `.GRP` -> PNG converter (Duke shareware art),
  now also emits `picanm` -> `anim.txt` and keys transparency to magenta
- **Animated textures** (BUILD `picanm`): walls/floors/ceilings cycle frame tiles
  off a clock; frames auto-loaded; works in editor + `make play`; picked tiles
  animate immediately
- **Magenta colour-key transparency**: the index-255 key is exported as opaque
  magenta and read through on *every* path (walls/floors as well as sprites), so
  masked tiles no longer show black holes on surfaces. Picker filter is now
  Solid/Masked (classified by magenta fraction)
- **Sprite sizing**: picking a tile sizes the sprite to the texture's pixel aspect
  (`fitSprite`); `[`/`]` resizes the aimed sprite in 3D (aspect preserved)
- `--novsync` flag; `.clang-format` + PostToolUse format hook
- **CMake** build (Linux/macOS/Windows, GCC/Clang/MSVC/MinGW) + GitHub Actions
  that publish self-contained release zips per platform (bundled SDL2 + assets)
- **Inner-sector cutouts** (columns/pits/platforms/recesses): draw a loop inside a
  sector → portal-bonded inner sector; correct concave/convex rim culling; rim
  texture stays synced across both sides; delete-sector + 3D undo

## Open — editor polish / rough edges
- Duplicate / zero-length vertex **merge** (drag/split onto a neighbour should merge)
- **Self-intersection guard** for the polygon draw tool (reject/warn on bow-ties)
- **Extrude-a-wall** sector creation (hover a wall -> grow a sector outward)
- HUD: centered "big notice" style; wire toasts to non-editor events
- Deferred **rename pass** for function-local / parameter names (render-loop math
  `tx/tz/fx`, parser locals); members/constants/buffers already done

## Open — make it interactive (a *game*)
- Pickups / triggers / switches
- **Lua + sol2 scripting layer** for sprite/sector behavior, hot-reloadable
  without restarting — full design in [Appendix A](#appendix-a--lua--sol2-scripting-design)

## Open — deeper renderer (marquee BUILD features)
- **Sloped floors/ceilings**
- **Sector-over-sector** (room-over-room)
- Lighting nuance: per-surface `pal` tint (coloured light), per-sprite shade

## Open — audio & build
- Sound (SDL_mixer): footsteps, ambient, switch *clunk*

---
Suggested next: **doors/lifts + use key** (interactive) — now the biggest
remaining payoff, and it pairs with the Lua `on_use` step in Appendix A.

---

## Appendix A — Lua + sol2 scripting design

Goal: move per-frame *behavior* (sprite animation/movement, sector doors/lifts,
triggers) out of compiled C++ and into hot-reloadable Lua, so behavior can be
tweaked and reloaded **without restarting the running session** — and eventually
authored by non-C++ users. This is a design note, not yet implemented.

Scope note: the bigger practical win here is live iteration (edit script, reload,
see it move) rather than raw compile time — this codebase is ~2.2k LOC and builds
in seconds. Keep that framing when judging the effort/payoff.

### Why Lua + sol2
- Lua is the de-facto game scripting language: tiny VM, fast, battle-tested.
- sol2 is a single-header modern-C++ binding layer — exposing our plain structs
  (`Sprite`, `Sector`) is nearly one line each, no manual stack juggling.
- Trivial hot-reload: re-run the script file (`lua.script_file(...)`) on a keypress
  or file-change; the VM rebuilds its tables, C++ state (the `Map`) persists.

Cost / caveat: vendoring a ~30k-LOC C library into an otherwise hand-rolled,
read-top-to-bottom project cuts slightly against the repo's ethos. Accept that
trade deliberately. (Wren or a tiny custom DSL remain the lighter-weight
alternatives if that matters more than ecosystem.)

### Module layout
- New `Script.{h,cpp}` — owns the `sol::state`, binding registration, lifecycle
  calls, and hot-reload. Mirrors the role of `Player`/`Texture`: a self-contained
  subsystem `main.cpp` drives.
- **Keep `Renderer` untouched and SDL-free.** Scripting is game/platform logic; it
  lives next to `main.cpp`, never inside the rasterizer.
- `#if EDITOR` is orthogonal — scripts should run in both the editor and `make play`
  builds. Hot-reload key is most useful in the editor build.
- Build: add Lua via `pkg-config lua5.4` (or vendor `lua/` + sol2 single header).
  Append the Lua source/`-llua` to `SRCS`/`LDFLAGS` in the `Makefile`; sol2 is
  header-only so just an `-I`. Guard with a `SCRIPTING` macro (default on) so the
  engine still builds if Lua is absent, like the `EDITOR` toggle in `Vec2.h`.

### Lifecycle / hook model
A script file (default `script.lua`, or per-map `<mapfile>.lua`) defines optional
global functions the host calls:
- `on_load(map)` — once after map + bindings are ready; spawn/register behaviors.
- `on_update(dt)` — every frame from the single update site (`main.cpp:743`, after
  `dt` is clamped, before `renderWorld` at `main.cpp:917`).
- `on_use(ref)` — when the player presses the use key aimed at a wall/sprite/sector
  (ties into the planned doors/lifts + use-key item; `ref` carries kind + indices).
- `on_reload()` — after a hot-reload, so scripts can re-seed transient state.

Behavior attaches by **tag**, not by editing C++: sectors/sprites carry an integer
`tag` (see map-format note); Lua keys its own state tables off that tag. This keeps
the C++ structs dumb and the behavior fully in script.

### Bindings to expose (minimum viable)
- Types: `Vec2{x,y}`, `Sprite{position,z,radius,height,textureId,color}`,
  `Sector{floor,ceiling,floorLight,ceilingLight,floorTextureId,ceilingTextureId}`.
  Expose `wallLight`/wall arrays read/write via accessor functions, not raw vectors,
  to keep the parallel-array invariants (see CLAUDE.md) safe from script.
- Map access: `map.sprites` (indexable), `map.sectors` (indexable), counts.
- Player (read-only to start): `player.position`, `player.sector`, `player.angle`.
- Time/util: `dt` arg, plus a `lerp`/`approach` helper exposed from C++ so easing
  is consistent with engine movement.
- Events out: `play_sound(name)` stub now (wire to SDL_mixer when audio lands).

Safety: scripts get a sandboxed env (no `os`/`io`/`require` by default); wrap every
host call site in `sol::protected_function` + error toast via the existing
`showMessage` queue so a script error never crashes the session.

### Hot-reload mechanism
- Simplest: a key (e.g. `L`) calls `Script::reload()` → re-`script_file`, then
  `on_reload()`. Push a toast on success/parse-error.
- Nicer: poll the script file's mtime each frame (cheap `stat`) and auto-reload on
  change. Optional follow-up.
- C++-side `Map` state persists across reloads; only the Lua tables rebuild. Scripts
  must therefore treat `on_load`/`on_reload` as the place to rebuild their own state.

### Map-format additions
- Add an optional integer `tag` to the `sector` line and `sprite` line (trailing,
  backward-compatible like the existing optional light floats). Loader/saver in
  `Map.cpp` parse/emit it; default `0` = untagged.
- Optionally a top-level `script <file>` line so a map can name its behavior script;
  else fall back to `<mapfile>.lua` then `script.lua`.

### Worked examples (target script surface)
```lua
-- a sprite that bobs up and down
function on_update(dt)
  for i, s in ipairs(map.sprites) do
    if s.tag == 1 then s.z = s.base_z + math.sin(time * 2) * 8 end
  end
end

-- a door: sector tag 2, lower its ceiling to the floor on use, raise on re-use
local doors = {}
function on_use(ref)
  if ref.kind == "sector" and map.sectors[ref.sector].tag == 2 then
    local d = doors[ref.sector] or { open = false }
    d.open = not d.open
    doors[ref.sector] = d           -- target state; on_update lerps toward it
  end
end
```

### Phasing (suggested implementation order, later)
1. Vendor Lua + sol2, `Script.{h,cpp}` skeleton, `on_update(dt)` wired into the loop,
   `L`-key reload, toast on error. Bind `map.sprites` read/write + `dt`. (Prove the
   bob example.)
2. Add `tag` to map format (parse/save/round-trip) + bind `map.sectors`.
3. `on_use(ref)` + use-key targeting (shares the doors/lifts work — pick buffer is
   EDITOR-only, so add a non-pick "sector/sprite in front of player" lookup).
4. Sandbox hardening, per-map `<mapfile>.lua`, optional mtime auto-reload.

### Risks / open questions
- Sprites have no `tag`/`base_z` field today — either add them to the struct or hold
  that state purely in Lua keyed by index (index is unstable if sprites are deleted).
  Leaning: add a small `tag` int to `Sprite`/`Sector`; keep richer state in Lua.
- Parallel-array invariants (`wallLight` vs `vertices`) must not be exposed raw to
  script — accessor functions only.
- Determinism/perf: per-frame Lua over all sprites is fine at this scale; revisit if
  entity counts grow.
