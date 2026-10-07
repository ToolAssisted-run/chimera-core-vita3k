#!/usr/bin/env python3
"""Is the bigger picture DRAWN bigger, or the small one stretched?

    check-resolution.py <1x.tga> <Nx.tga> <N>

ResTest's picture is slopes: lines at eight angles, a disc, a tilted triangle,
in four colours with no antialiasing. A slope on a grid is a staircase whose
steps are the grid's. So, of the picture at N times the resolution:

  - it is N times the size each way;
  - it holds the colours of the 1x picture and no others (a stretch through a
    smoothing filter invents colours);
  - its steps are finer: there are NxN blocks of it that are not one colour (a
    stretch that repeats pixels has none, and that is the control this script
    runs on the 1x picture itself, stretched here);
  - and it is the same picture: where a block IS one colour, it is the colour
    the 1x picture has there, almost everywhere.

Prints one line of figures and exits 0, or says what failed and exits 1.
"""
import struct
import sys


def tga(path):
    d = open(path, "rb").read()
    w, h = struct.unpack("<HH", d[12:16])
    bpp = d[16] // 8
    px = d[18 + d[0]:18 + d[0] + w * h * bpp]
    # rows of (b, g, r) triples, alpha dropped, top row first: a TGA says in
    # its descriptor which end it starts from, and the engine's start from the
    # bottom while the harness's start from the top
    rows = [[px[(y * w + x) * bpp:(y * w + x) * bpp + 3] for x in range(w)] for y in range(h)]
    return w, h, rows if d[17] & 0x20 else rows[::-1]


def blocks(rows, w, h, n):
    """(mixed, uniform-and-right, uniform) over the NxN blocks, against `small`."""
    for y in range(h // n):
        for x in range(w // n):
            first = rows[y * n][x * n]
            yield x, y, first, all(rows[y * n + dy][x * n + dx] == first for dy in range(n) for dx in range(n))


def measure(small, big, w, h, n):
    mixed = right = uniform = 0
    for x, y, colour, one in blocks(big, w * n, h * n, n):
        if not one:
            mixed += 1
            continue
        uniform += 1
        right += colour == small[y][x]
    return mixed, right, uniform


def main():
    if len(sys.argv) != 4:
        sys.exit("usage: check-resolution.py <1x.tga> <Nx.tga> <N>")
    n = int(sys.argv[3])
    w, h, small = tga(sys.argv[1])
    bw, bh, big = tga(sys.argv[2])
    if (bw, bh) != (w * n, h * n):
        sys.exit("FAIL the %dx picture is %dx%d, and %d times %dx%d is %dx%d" % (n, bw, bh, n, w, h, w * n, h * n))
    colours = {p for row in small for p in row}
    if len(colours) < 4:
        sys.exit("FAIL the 1x picture has %d colours: ResTest draws four, so it did not draw" % len(colours))
    extra = {p for row in big for p in row} - colours
    if extra:
        sys.exit("FAIL the %dx picture has %d colours the 1x picture does not: it was smoothed, not drawn" % (n, len(extra)))

    # the control: this is what a stretch that repeats pixels looks like to the test below
    stretched = [[p for p in row for _ in range(n)] for row in small for _ in range(n)]
    if measure(small, stretched, w, h, n)[0] != 0:
        sys.exit("FAIL the control: a 1x picture with every pixel repeated shows finer steps, so the test sees them everywhere")

    mixed, right, uniform = measure(small, big, w, h, n)
    if mixed < 1000:
        sys.exit("FAIL only %d blocks of the %dx picture are more than one colour: its steps are the 1x picture's, stretched" % (mixed, n))
    if right * 1000 < uniform * 995:
        sys.exit("FAIL %d of %d one-colour blocks are not the 1x picture's colour there: it is another picture" % (uniform - right, uniform))
    print("%dx%d against %dx%d: %d blocks with finer steps (0 in the 1x picture stretched), %d of %d others the 1x colour, %d colours"
          % (bw, bh, w, h, mixed, right, uniform, len(colours)))


if __name__ == "__main__":
    main()
