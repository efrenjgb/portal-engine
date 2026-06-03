#!/usr/bin/env python3
# Generate the sample PPM (P6) textures used by map.txt. No dependencies.
#   python3 tools/gen_textures.py
import os, math, random

OUT = os.path.join(os.path.dirname(__file__), "..", "textures")
os.makedirs(OUT, exist_ok=True)
S = 64  # texture size

def write_ppm(name, pixels):
    path = os.path.join(OUT, name)
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (S, S))
        f.write(bytes(pixels))
    print("wrote", os.path.relpath(path))

def brick():
    px = bytearray()
    bh, bw = 16, 32                         # brick height / width
    for y in range(S):
        row = y // bh
        off = (bw // 2) if (row & 1) else 0
        for x in range(S):
            xx = (x + off) % bw
            mortar = (y % bh) < 2 or (xx < 2)
            if mortar:
                r, g, b = 60, 58, 55
            else:
                random.seed(row * 97 + ((x + off) // bw) * 131)
                v = 0.85 + 0.15 * random.random()
                r, g, b = int(150*v), int(70*v), int(55*v)
            px += bytes((r, g, b))
    return px

def tiles():
    px = bytearray()
    t = 16
    for y in range(S):
        for x in range(S):
            grid = (x % t) < 2 or (y % t) < 2
            checker = ((x // t) + (y // t)) & 1
            if grid:        r, g, b = 30, 35, 40
            elif checker:   r, g, b = 70, 120, 150
            else:           r, g, b = 45, 80, 105
            px += bytes((r, g, b))
    return px

def panel():
    px = bytearray()
    for y in range(S):
        for x in range(S):
            base = 95 + int(25 * math.sin((x + y) * 0.35))   # brushed diagonal
            r = g = b = base
            # rivets near the corners of each 32-cell
            cx, cy = x % 32, y % 32
            d = math.hypot(cx - 4, cy - 4)
            if d < 3: r, g, b = 160, 160, 170
            px += bytes((max(0,min(255,r)), max(0,min(255,g)), max(0,min(255,b))))
    return px

write_ppm("brick.ppm", brick())
write_ppm("tiles.ppm", tiles())
write_ppm("panel.ppm", panel())
