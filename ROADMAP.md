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
- Per-surface **lighting** (per-wall + floor/ceiling), `Y`/`H`
- Height edit by camera pitch / aimed surface (`T`/`G`)
- HUD text (built-in 5x7 font), FPS counter, transient toast messages
- Save/load text maps (overwrite-in-place for `.save`), `set player start`
- `tools/grp_extract.py`: BUILD `.GRP` -> PNG converter (Duke shareware art)
- `--novsync` flag; `.clang-format` + PostToolUse format hook

## Open — editor polish / rough edges
- Duplicate / zero-length vertex **merge** (drag/split onto a neighbour should merge)
- **Self-intersection guard** for the polygon draw tool (reject/warn on bow-ties)
- **Extrude-a-wall** sector creation (hover a wall -> grow a sector outward)
- HUD: centered "big notice" style; wire toasts to non-editor events
- Deferred **rename pass** for function-local / parameter names (render-loop math
  `tx/tz/fx`, parser locals); members/constants/buffers already done

## Open — make it interactive (a *game*)
- **Doors / lifts + a `use` key** (tagged sectors that animate) — biggest payoff
- Pickups / triggers / switches

## Open — deeper renderer (marquee BUILD features)
- **Animated textures** (use Duke ART `picanm` data we currently skip)
- **Sloped floors/ceilings**
- **Sector-over-sector** (room-over-room)
- Lighting nuance: per-surface `pal` tint (coloured light), per-sprite shade

## Open — audio & build
- Sound (SDL_mixer): footsteps, ambient, switch *clunk*
- **CMake** build for one-command Windows / Linux / macOS (incl. MSVC)

---
Suggested next: **doors/lifts + use key** (interactive), or **animated textures**
(quick high-visual win reusing the imported art).
