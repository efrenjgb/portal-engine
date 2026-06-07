// The camera pose: position, yaw, pitch, with cached sin/cos of the yaw.
#pragma once
#include "Vec2.h"
#include <cmath>

struct Camera {
    float x = 0, y = 0, z = 0;   // eye position (z is up)
    float angle = 0;             // yaw, radians; forward = (cos, sin)
    float pitch = 0;             // look up/down, applied as a vertical shear
    float yawSin = 0, yawCos = 1;    // cached sin/cos of angle

    void update(){ yawSin = std::sin(angle); yawCos = std::cos(angle); }
};
