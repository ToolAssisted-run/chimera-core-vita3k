#!/usr/bin/env python3
"""A licence (a work.bin or a .rif, 512 bytes) written as a zRIF: the text form
NoPayStation lists for a package and a KEY.txt beside one holds.

usage: make-zrif.py <keyflate.c> <work.bin> <out.txt>

A zRIF is the licence compressed with zlib against a fixed dictionary and
written in base64. The dictionary is read from Vita3K's own copy of the code
that reads one (external/psvpfstools/libzrif/src/keyflate.c), so the text made
here is the text that code takes back.
"""
import base64
import re
import sys
import zlib


def dictionary(source):
    text = open(source).read()
    body = text[text.index("g_dict[] ="):]
    body = body[body.index("{") + 1 : body.index("}")]
    data = bytes(int(n) for n in re.findall(r"\d+", body))
    size = int(re.search(r"g_dict_size\s*=\s*(\d+)", text).group(1))
    # the array has one value more than the code uses (g_dict_size)
    return data[:size].ljust(size, b"\0")


def main():
    zdict = dictionary(sys.argv[1])
    licence = open(sys.argv[2], "rb").read()
    if len(licence) != 512:
        sys.exit(f"{sys.argv[2]}: {len(licence)} bytes, and a licence is 512")
    z = zlib.compressobj(9, zlib.DEFLATED, 10, 8, zlib.Z_DEFAULT_STRATEGY, zdict)
    packed = z.compress(licence) + z.flush()
    with open(sys.argv[3], "w") as out:
        out.write(base64.b64encode(packed).decode() + "\n")
    print(f"zRIF of {len(packed)} bytes compressed")


if __name__ == "__main__":
    main()
