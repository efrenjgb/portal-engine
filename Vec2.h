// Core value types and tiny helpers shared across the engine.
#pragma once
#include <cstdint>
#include <cmath>

// Compile-time switch: 1 includes the in-engine editor + the per-pixel pick
// buffer; 0 (a "play" build, `make play`) strips both. Defaults to on.
#ifndef EDITOR
#define EDITOR 1
#endif

constexpr float PI_F = 3.14159265358979323846f;

// Camera near plane (small). The renderer also clips walls to the view's side
// edges, so projected x stays within [0,W] and texture coords stay precise even
// for grazing walls — that frustum clip, not a large near plane, is what keeps
// things numerically sane, so this can be tiny and the player can walk right up
// to and through portals.
constexpr float NEAR_PLANE = 0.005f;

struct Vec2 { float x = 0, y = 0; };

inline int   clampi(int v, int lo, int hi){ return v < lo ? lo : (v > hi ? hi : v); }
inline float clampf(float v, float lo, float hi){ return v < lo ? lo : (v > hi ? hi : v); }

// Colour is packed 0xAARRGGBB. shade() multiplies RGB by f (kept opaque).
inline uint32_t shade(uint32_t c, float f){
    if(f < 0) f = 0; if(f > 1) f = 1;
    int r = (int)(((c >> 16) & 255) * f);
    int g = (int)(((c >>  8) & 255) * f);
    int b = (int)(( c        & 255) * f);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

constexpr float FOG_DIST = 34.0f;   // distance at which shading is darkest
inline float distFade(float depth){ float f = 1.0f - depth / FOG_DIST; return f < 0.12f ? 0.12f : f; }
