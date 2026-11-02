#!/usr/bin/env python3
"""Decode an 8-bit RGBA PNG to raw pixels: ShellUI has no PNG reader of its own."""
import struct
import sys
import zlib


def main(source: str, target: str) -> None:
    data = open(source, "rb").read()
    at, idat = 8, b""
    width = height = 0
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at : at + 8])
        body = data[at + 8 : at + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", body[:10])
            if (depth, colour, body[12]) != (8, 6, 0):
                sys.exit(f"{source}: needs 8-bit RGBA, not interlaced")
        elif kind == b"IDAT":
            idat += body
        at += length + 12
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    above = bytes(stride)
    for row in range(height):
        start = row * (stride + 1)
        kind = raw[start]
        line = bytearray(raw[start + 1 : start + 1 + stride])
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            b = above[i]
            c = above[i - 4] if i >= 4 else 0
            if kind == 1:
                line[i] = (line[i] + a) & 255
            elif kind == 2:
                line[i] = (line[i] + b) & 255
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                best = a if pa <= pb and pa <= pc else b if pb <= pc else c
                line[i] = (line[i] + best) & 255
        out += line
        above = bytes(line)
    open(target, "wb").write(out)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
