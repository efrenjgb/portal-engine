// The player: a camera pose plus movement, collision and sector picking.
#pragma once
#include "Map.h"
#include "Camera.h"

constexpr float EYE      = 1.62f;   // eye height above the floor
constexpr float MAX_STEP = 0.51f;   // tallest step you can walk up
constexpr float BODY     = 1.70f;   // min ceiling clearance to fit through a portal
constexpr float PLAYER_R = 0.35f;   // collision radius off walls
constexpr float MOVE_SPD = 5.1f;    // metres / second
constexpr float GRAVITY  = 20.0f;   // metres / second^2
constexpr float JUMP_VEL = 5.5f;    // initial jump speed (~0.75 m peak)

class Player {
public:
    Camera cam;
    int    sector   = 0;
    float  zvel     = 0.0f;          // vertical velocity (only while airborne)
    bool   onGround = true;

    void move(const Map& map, float dx, float dy);   // walls + portal transitions
    void keepInside(const Map& map);                 // standoff from walls
    void collideSprites(const Map& map);             // bump into sprites
    int  pickSector(const Map& map) const;           // sector under the crosshair
    void jump();                                     // hop, if standing
    void settleEyeHeight(const Map& map, float dt);  // gravity + floor follow
};
