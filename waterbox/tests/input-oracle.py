#!/usr/bin/env python3
"""The input oracle: what InputTest (tests/apps/InputTest) must have read,
predicted from the input script alone, with the Vita's own units worked out
here and not taken from the core.

  input-oracle.py gen SCRIPT FRAMES [SHIFT]   write a script (every control,
                                              extremes included; SHIFT moves
                                              every line that many frames later)
  input-oracle.py check SCRIPT INPUT_TXT      compare InputTest's lines with
                                              the prediction; exit 1 on any
                                              difference

A line tagged v<n> was read in the middle of frame n+1. SPDX-License-Identifier: MIT
"""
import math
import sys

AXES = 14
# the declared controls: buttons, then axes (see vita3k_driver.h)
PAD = [0x10, 0x40, 0x80, 0x20, 0x4000, 0x2000, 0x8000, 0x1000, 0x100, 0x200, 0x8, 0x1]
PAD_EXT = PAD[:8] + [0x400, 0x800] + PAD[10:]
RANGES = [(-128, 127)] * 4 + [(0, 65535)] * 4 + [(-4000, 4000)] * 3 + [(-18000, 18000)] * 3
NEUTRAL = [0, 0, 0, 0, 32768, 32768, 32768, 32768, 0, 0, -1000, 0, 0, 0]


def gen(path, frames, shift):
    seed = 0x5eed
    def rnd(n):
        nonlocal seed
        seed = (seed * 6364136223846793005 + 1442695040888963407) & (2**64 - 1)
        return (seed >> 33) % n
    lines = ["# InputTest's script: segments of 1..9 frames\n",
             "1 0x0 " + " ".join(map(str, NEUTRAL)) + "\n"]
    f = 4
    while f <= frames:
        mask = rnd(1 << 14)
        axes = []
        for lo, hi in RANGES:
            pick = rnd(8)
            axes.append(lo if pick == 0 else hi if pick == 1 else lo + rnd(hi - lo + 1))
        lines.append("%d 0x%x %s\n" % (f + shift, mask, " ".join(map(str, axes))))
        f += 1 + rnd(9)
    open(path, "w").writelines(lines)


def read_script(path):
    script = []
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        script.append((int(parts[0]), int(parts[1], 0), [int(x) for x in parts[2:2 + AXES]]))
    return script


def at(script, frame):
    cur = (0, [0] * 0)
    mask, axes = 0, NEUTRAL
    for start, m, a in script:
        if start <= frame:
            mask, axes = m, a
    return mask, axes


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def expect(script, last_frame):
    """frame -> the line InputTest writes, for every frame up to last_frame"""
    out = {}
    ids = [0, 0]
    down_before = [False, False]
    for f in range(1, last_frame + 1):
        mask, a = at(script, f)
        buttons = sum(b for i, b in enumerate(PAD) if mask >> i & 1)
        ext = sum(b for i, b in enumerate(PAD_EXT) if mask >> i & 1)
        lx, ly = clamp(128 + a[0], 0, 255), clamp(128 - a[1], 0, 255)
        rx, ry = clamp(128 + a[2], 0, 255), clamp(128 - a[3], 0, 255)
        touch = []
        for panel, (bit, xa, ya, y0, ysize) in enumerate(((12, 4, 5, 0, 1088), (13, 6, 7, 108, 782))):
            down = bool(mask >> bit & 1)
            if down and not down_before[panel]:
                ids[panel] = (ids[panel] + 1) % 128
            down_before[panel] = down
            if down:
                touch.append("%d:%d,%d,%d" % (1, a[xa] * 1920 // 65536, y0 + a[ya] * ysize // 65536, ids[panel]))
            else:
                touch.append("0")
        accel = a[8:11]
        gyro = [round(clamp(v, -18000, 18000) / 10 * math.pi / 180 * 1000) for v in a[11:14]]
        out[f] = ("b%08x e%08x l%d,%d r%d,%d f%s k%s" % (buttons, ext, lx, ly, rx, ry, touch[0], touch[1]), accel, gyro)
    return out


def check(script_path, input_path):
    script = read_script(script_path)
    got = []
    for line in open(input_path):
        if line.startswith("v"):
            tag, rest = line.split(" ", 1)
            got.append((int(tag[1:]) + 1, rest.strip()))
    if not got:
        print("no lines read")
        return 1
    want = expect(script, max(f for f, _ in got))
    bad = 0
    for f, rest in got:
        parts = rest.split(" ")
        head = " ".join(parts[:6])
        accel = [int(x) for x in parts[6][1:].split(",")]
        gyro = [int(x) for x in parts[7][1:].split(",")]
        w_head, w_accel, w_gyro = want[f]
        # the sensors are floats on the way: one unit either side
        if head != w_head or any(abs(x - y) > 1 for x, y in zip(accel + gyro, w_accel + w_gyro)):
            bad += 1
            if bad <= 3:
                print("frame %d: read %s, predicted %s a%s g%s" % (f, rest, w_head, w_accel, w_gyro))
    presses = sum(1 for _, m, _ in script if m >> 12 & 1)
    print("%d frames read, %d differ from the prediction" % (len(got), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "gen":
        gen(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]) if len(sys.argv) > 4 else 0)
    elif len(sys.argv) == 4 and sys.argv[1] == "check":
        sys.exit(check(sys.argv[2], sys.argv[3]))
    else:
        print(__doc__)
        sys.exit(2)
