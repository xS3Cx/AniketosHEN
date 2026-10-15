#!/usr/bin/env python3
"""Draws the default Home particle pictures (source/shellui/assets/particles).

Four outlined PlayStation symbols with a glow, and a soft dot. Plain Python,
no imaging library: each pixel's distance to the shape gives its brightness.

  python3 scripts/generate_particle_assets.py
"""
import math, os, struct, zlib

SIZE = 96
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "source",
                   "shellui", "assets", "particles")


def png(path, pixels):
    raw = b"".join(b"\x00" + bytes(v for px in row for v in px) for row in pixels)

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    with open(path, "wb") as out:
        out.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0))
                  + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def segment(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def polygon(points):
    edges = list(zip(points, points[1:] + points[:1]))
    return lambda x, y: min(segment(x, y, a[0], a[1], b[0], b[1]) for a, b in edges)


C = SIZE / 2
SHAPES = {
    "triangle": (polygon([(C, 20), (78, 72), (18, 72)]), (0, 214, 160)),
    "circle": (lambda x, y: abs(math.hypot(x - C, y - C) - 27), (255, 64, 96)),
    "cross": (lambda x, y: min(segment(x, y, 24, 24, 72, 72), segment(x, y, 72, 24, 24, 72)), (80, 144, 255)),
    "square": (polygon([(23, 23), (73, 23), (73, 73), (23, 73)]), (232, 120, 208)),
}


def outlined(distance, colour, line=2.6, glow=9.0):
    rows = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            d = distance(x + 0.5, y + 0.5)
            core = max(0.0, min(1.0, line + 0.5 - d))       # the line itself
            halo = 0.55 * math.exp(-(d / glow) ** 2 * 2.2)  # its glow
            alpha = min(1.0, core + halo)
            white = core * 0.45                              # a hot centre
            row.append(tuple(int(c + (255 - c) * white) for c in colour) + (int(alpha * 255),))
        rows.append(row)
    return rows


def dot():
    rows = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            d = math.hypot(x + 0.5 - C, y + 0.5 - C) / (SIZE * 0.42)
            alpha = max(0.0, 1.0 - d * d) ** 1.6
            row.append((255, 246, 228, int(alpha * 255)))
        rows.append(row)
    return rows


os.makedirs(OUT, exist_ok=True)
for name, (distance, colour) in SHAPES.items():
    png(os.path.join(OUT, name + ".png"), outlined(distance, colour))
png(os.path.join(OUT, "dot.png"), dot())
print("written to", os.path.normpath(OUT))
