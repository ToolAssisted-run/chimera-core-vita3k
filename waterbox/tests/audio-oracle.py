#!/usr/bin/env python3
"""The audio oracle: AudioTest's sound (tests/apps/AudioTest), measured.

  audio-oracle.py RAW FRAMES [--drop-one]

RAW is the run's sound (run-*'s --audio-out: S16 stereo at 48 kHz). It must be
800 pairs a frame, and over frames 61..120 the left channel must carry the
main port's 1000 Hz tone at 8000 and the BGM port's 250 Hz tone at 4000, the
right only the 250 Hz tone at half volume (2000), each within 0.1%, and
nothing else (the RMS within 0.2%). --drop-one takes one pair out of the
middle of that window first: the negative control, since a single lost
sample must fail it. SPDX-License-Identifier: MIT
"""
import math
import struct
import sys

RATE = 48000


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    data = open(sys.argv[1], "rb").read()
    frames = int(sys.argv[2])
    pairs = len(data) // 4
    if pairs != frames * 800:
        print("%d pairs for %d frames, not 800 a frame" % (pairs, frames))
        return 1
    s = struct.unpack("<%dh" % (pairs * 2), data)
    left, right = list(s[0::2]), list(s[1::2])
    a, n = 60 * 800, RATE
    if "--drop-one" in sys.argv:
        del left[a + n // 2]
        del right[a + n // 2]
    if len(left) < a + n:
        print("too short: frames 61..120 are needed")
        return 1

    def amp(x, f):
        re = im = 0.0
        w = 2 * math.pi * f / RATE
        for k in range(n):
            v = x[a + k]
            re += v * math.cos(w * k)
            im += v * math.sin(w * k)
        return 2 * math.hypot(re, im) / n

    def rms(x):
        return math.sqrt(sum(v * v for v in x[a:a + n]) / n)

    got = {"L1000": amp(left, 1000), "L250": amp(left, 250), "R1000": amp(right, 1000), "R250": amp(right, 250),
           "Lrms": rms(left), "Rrms": rms(right)}
    want = {"L1000": (8000, 0.001), "L250": (4000, 0.001), "R250": (2000, 0.001),
            "Lrms": (math.sqrt((8000 ** 2 + 4000 ** 2) / 2), 0.002), "Rrms": (2000 / math.sqrt(2), 0.002)}
    bad = [k for k, (v, tol) in want.items() if abs(got[k] - v) > v * tol] + (["R1000"] if got["R1000"] > 2 else [])
    print(" ".join("%s=%.1f" % (k, v) for k, v in got.items()) + ("" if not bad else "  OFF: " + ",".join(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
