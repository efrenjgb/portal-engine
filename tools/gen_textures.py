#!/usr/bin/env python3
# Generate the sample PNG textures used by map.txt. Pure stdlib (zlib), no PIL.
#   python3 tools/gen_textures.py
import os, math, random, zlib, struct

OUT = os.path.join(os.path.dirname(__file__), "..", "textures")
os.makedirs(OUT, exist_ok=True)
S = 64  # texture size

def write_png(name, pixels, w, h):           # pixels: bytes, w*h*3 RGB
    raw = bytearray()
    for y in range(h):                       # each scanline gets a filter byte (0)
        raw.append(0)
        raw += pixels[y*w*3:(y+1)*w*3]
    def chunk(typ, data):
        return (struct.pack(">I", len(data)) + typ + data +
                struct.pack(">I", zlib.crc32(typ + data) & 0xffffffff))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))   # 8-bit RGB
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    path = os.path.join(OUT, name)
    with open(path, "wb") as f:
        f.write(png)
    print("wrote", os.path.relpath(path))

# A wide sky panorama (wraps horizontally): blue gradient + wispy clouds.
def sky(W=256, H=128):
    def lerp(a, b, t): return int(a + (b - a) * t)
    px = bytearray()
    for y in range(H):
        fy = y / (H - 1)
        br, bg, bb = lerp(0x20, 0xbe, fy), lerp(0x34, 0xd4, fy), lerp(0x72, 0xea, fy)
        for x in range(W):
            fx = x / W
            n = 0.5*math.sin(2*math.pi*2*fx) + 0.3*math.sin(2*math.pi*3*fx + 1.3) \
              + 0.2*math.sin(2*math.pi*5*fx + 2.7)
            n = (n + 1.0) * 0.5                                  # 0..1, periodic in x
            band = math.exp(-((fy - 0.38)**2) / (2 * 0.16**2))  # clouds in the upper band
            cloud = max(0.0, min(1.0, (n - 0.55) / 0.45 * band))
            r = lerp(br, 0xff, cloud*0.9); g = lerp(bg, 0xff, cloud*0.9); b = lerp(bb, 0xff, cloud*0.85)
            px += bytes((min(255, r), min(255, g), min(255, b)))
    return px

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

write_png("brick.png", brick(), S, S)
write_png("tiles.png", tiles(), S, S)
write_png("panel.png", panel(), S, S)
write_png("sky.png",   sky(),   256, 128)
