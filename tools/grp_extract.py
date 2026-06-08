#!/usr/bin/env python3
"""Extract textures from a BUILD-engine .GRP (e.g. Duke Nukem 3D shareware) to PNGs.

A .GRP is a trivial archive; the art lives in TILES000.ART..TILESxxx.ART (8-bit
palette indices, stored column-major) and the colours in PALETTE.DAT (256 RGB
triples at 6 bits/channel). This reads all three and writes one PNG per non-empty
tile.  Pure stdlib (zlib for PNG), no PIL.

    python3 tools/grp_extract.py <file.grp> [out_dir] [first] [last]

  out_dir   where PNGs go              (default: textures/duke)
  first,last optional inclusive tile-number range to limit the dump

Legal note: art from the *full* game is copyrighted — only redistribute tiles
extracted from the freely-distributable *shareware* GRP.
"""
import os, sys, struct, zlib


def write_png(path, rgba, w, h):
    raw = bytearray()
    for y in range(h):                       # one filter byte (0 = none) per scanline
        raw.append(0)
        raw += rgba[y*w*4:(y+1)*w*4]
    def chunk(typ, data):
        return (struct.pack(">I", len(data)) + typ + data +
                struct.pack(">I", zlib.crc32(typ + data) & 0xffffffff))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))   # 8-bit RGBA
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def read_grp(path):
    """Return {filename: bytes} for every entry in a KenSilverman .GRP."""
    with open(path, "rb") as f:
        blob = f.read()
    if blob[:12] != b"KenSilverman":
        sys.exit("not a KenSilverman .GRP (bad magic)")
    (count,) = struct.unpack_from("<i", blob, 12)
    files, off = [], 16
    for _ in range(count):                   # table: 12-byte name + 4-byte size
        name = blob[off:off+12].split(b"\x00")[0].decode("ascii", "replace").strip()
        (size,) = struct.unpack_from("<i", blob, off+12)
        files.append((name, size))
        off += 16
    out = {}
    for name, size in files:                 # payloads follow the table, in order
        out[name] = blob[off:off+size]
        off += size
    return out


def load_palette(dat):
    """First 768 bytes of PALETTE.DAT: 256 RGB triples, 6-bit channels -> 8-bit."""
    pal = []
    for i in range(256):
        r, g, b = dat[i*3], dat[i*3+1], dat[i*3+2]
        pal.append((r*255//63, g*255//63, b*255//63))
    return pal


def decode_art(art, palette, out_dir, lo, hi):
    ver, numtiles, start, end = struct.unpack_from("<iiii", art, 0)
    n = end - start + 1
    if n <= 0:
        return 0
    o = 16
    sizx = struct.unpack_from("<%dh" % n, art, o); o += 2*n
    sizy = struct.unpack_from("<%dh" % n, art, o); o += 2*n
    o += 4*n                                 # skip picanm[]
    written = 0
    for i in range(n):
        w, h = sizx[i], sizy[i]
        tile = start + i
        if w <= 0 or h <= 0:
            continue
        data = art[o:o + w*h]; o += w*h
        if tile < lo or tile > hi:
            continue
        rgba = bytearray(w*h*4)
        for x in range(w):                   # source is column-major
            col = x*h
            for y in range(h):
                idx = data[col + y]
                j = (y*w + x)*4
                if idx == 255:               # BUILD's transparency key
                    rgba[j:j+4] = b"\x00\x00\x00\x00"
                else:
                    r, g, b = palette[idx]
                    rgba[j], rgba[j+1], rgba[j+2], rgba[j+3] = r, g, b, 255
        write_png(os.path.join(out_dir, "tile%04d.png" % tile), bytes(rgba), w, h)
        written += 1
    return written


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    grp_path = sys.argv[1]
    out_dir  = sys.argv[2] if len(sys.argv) > 2 else "textures/duke"
    lo = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    hi = int(sys.argv[4]) if len(sys.argv) > 4 else 1 << 30
    os.makedirs(out_dir, exist_ok=True)

    grp = read_grp(grp_path)
    if "PALETTE.DAT" not in grp:
        sys.exit("PALETTE.DAT not found in GRP")
    palette = load_palette(grp["PALETTE.DAT"])
    arts = sorted(k for k in grp if k.upper().startswith("TILES") and k.upper().endswith(".ART"))
    if not arts:
        sys.exit("no TILESxxx.ART files in GRP")

    total = 0
    for name in arts:
        c = decode_art(grp[name], palette, out_dir, lo, hi)
        total += c
        print("  %-14s -> %d tiles" % (name, c))
    print("wrote %d PNGs to %s/" % (total, out_dir))


if __name__ == "__main__":
    main()
