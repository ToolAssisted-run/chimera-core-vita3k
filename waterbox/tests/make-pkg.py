#!/usr/bin/env python3
"""Packs a NoNpDRM dump back into the form the PlayStation Store sends a game
in: a .pkg, and beside it the dump's licence (its work.bin).

    make-pkg.py <dump.zip> <vita3k's packages/pkg.h> <out.pkg> <out work.bin>

The gate has no package of its own to install - a real one is a game somebody
bought - and it does have dumps (build/content). A dump is what a package
unpacks to, still encrypted the way the console keeps it, plus the licence. So
the package is put back together around the same files: the container Vita3K's
install_pkg reads (an outer header, three info blocks, an item table, names,
data, the last three under AES-128-CTR), with the licence left out, because a
package does not carry one. What the core then has to do with it is exactly
what it has to do with a bought package: find the licence in its slot, unpack,
decrypt, start. The leg compares the machine it starts with the one the dump
itself starts.

The container's key is Vita3K's own constant, read out of its header here
rather than written down a second time. openssl does the two ciphers.

This is NOT a tool for making packages a console would take: nothing is
signed, and the digests are zero. It makes what install_pkg reads.
"""
import io
import re
import struct
import subprocess
import sys
import tempfile
import zipfile


def die(msg):
    sys.exit("make-pkg: " + msg)


def openssl(args, data):
    return subprocess.run(["openssl", "enc"] + args, input=data, stdout=subprocess.PIPE, check=True).stdout


def sfo_string(sfo, key):
    magic, _version, key_table, data_table, count = struct.unpack_from("<4sIIII", sfo, 0)
    if magic != b"\0PSF":
        die("the dump's param.sfo is not one")
    for i in range(count):
        key_off, _fmt, length, _max, data_off = struct.unpack_from("<HHIII", sfo, 20 + i * 16)
        name = sfo[key_table + key_off:sfo.index(b"\0", key_table + key_off)].decode()
        if name == key:
            return sfo[data_table + data_off:data_table + data_off + length].split(b"\0")[0].decode()
    return ""


def main():
    if len(sys.argv) != 5:
        die("usage: make-pkg.py <dump.zip> <pkg.h> <out.pkg> <out work.bin>")
    dump, header, out_pkg, out_licence = sys.argv[1:]

    m = re.search(r"pkg_vita_2\[\]\s*=\s*\{([^}]*)\}", open(header).read())
    if not m:
        die("no pkg_vita_2 in " + header)
    vita_key = bytes(int(x, 16) for x in m.group(1).replace(" ", "").split(",") if x)
    if len(vita_key) != 16:
        die("pkg_vita_2 is not sixteen bytes")

    z = zipfile.ZipFile(dump)
    names = [n for n in z.namelist()]
    sfo_name = next((n for n in names if n.endswith("sce_sys/param.sfo")), None)
    if not sfo_name:
        die("no sce_sys/param.sfo in the dump")
    root = sfo_name[:-len("sce_sys/param.sfo")]
    licence_name = root + "sce_sys/package/work.bin"
    if licence_name not in names:
        die("no sce_sys/package/work.bin in the dump: it is not a NoNpDRM one")
    sfo = z.read(sfo_name)
    content_id = sfo_string(sfo, "CONTENT_ID")
    if len(content_id) < 16:
        die("the dump's param.sfo names no content id")
    open(out_licence, "wb").write(z.read(licence_name))

    # what goes in: every directory and file under the game's folder, by the
    # path the console knows it under, without the licence
    dirs, files = [], []
    for n in sorted(names):
        if not n.startswith(root) or n == root or n == licence_name:
            continue
        rel = n[len(root):]
        if n.endswith("/"):
            dirs.append(rel.rstrip("/"))
        else:
            files.append((rel, n))
    for rel, _ in files:  # a folder the archive never listed on its own
        parts = rel.split("/")[:-1]
        for i in range(1, len(parts) + 1):
            d = "/".join(parts[:i])
            if d not in dirs:
                dirs.append(d)
    dirs.sort()
    items = [(d, None) for d in dirs] + files

    def pad16(n):
        return (n + 15) & ~15

    # the data area, as install_pkg walks it: the item table, the names, the
    # files, each on a sixteen-byte boundary because each is deciphered from
    # the counter its own offset gives
    table_size = len(items) * 32
    name_off, offsets = pad16(table_size), []
    for rel, _ in items:
        offsets.append(name_off)
        name_off = pad16(name_off + len(rel.encode()))
    data_off = name_off

    iv = bytes(range(0x40, 0x50))
    main_key = openssl(["-aes-128-ecb", "-nopad", "-K", vita_key.hex()], iv)[:16]

    with tempfile.TemporaryFile() as plain:
        table = io.BytesIO()
        cursor = data_off
        for (rel, src), noff in zip(items, offsets):
            size = 0 if src is None else z.getinfo(src).file_size
            # type 4 is a folder, 3 a file; flags above the low byte are not read
            table.write(struct.pack(">IIQQII", noff, len(rel.encode()), cursor if src else 0, size, 4 if src is None else 3, 0))
            if src:
                cursor = pad16(cursor + size)
        plain.write(table.getvalue())
        for (rel, _), noff in zip(items, offsets):
            plain.seek(noff)
            plain.write(rel.encode())
        cursor = data_off
        for rel, src in items:
            if src is None:
                continue
            plain.seek(cursor)
            with z.open(src) as f:
                while True:
                    chunk = f.read(1 << 20)
                    if not chunk:
                        break
                    plain.write(chunk)
            cursor = pad16(cursor + z.getinfo(src).file_size)
        if plain.seek(0, 2) < cursor:  # the last file's padding
            plain.seek(cursor - 1)
            plain.write(b"\0")
        data_size = cursor
        plain.seek(0)

        # the outer part, in the clear: the two headers, the info blocks, the
        # param.sfo (install_pkg reads the game's name from here, before
        # anything is deciphered)
        HEADER, EXT = 0xC0, 0x40
        info_off = HEADER + EXT
        info = b""
        info += struct.pack(">III", 2, 4, 0x15)  # content type: a Vita application
        info += struct.pack(">IIII", 13, 8, 0, table_size)  # the item table
        sfo_at = info_off + len(info) + 16
        info += struct.pack(">IIII", 14, 8, sfo_at, len(sfo))  # the param.sfo
        data_at = pad16(sfo_at + len(sfo))
        total = data_at + data_size

        head = struct.pack(">IHHIIIIQQQ", 0x7F504B47, 0x8000, 2, info_off, 3, data_at, len(items), total, data_at, data_size)
        head += content_id.encode().ljust(0x30, b"\0") + bytes(0x10) + iv + bytes(0x40)
        assert len(head) == HEADER
        ext = struct.pack(">IIIIIIQIIIIQQ", 0x7F657874, 1, EXT, 0, 0, 0, data_size, 0, 2, 0, 0, 0, 0)  # data_type2 2: the key above
        assert len(ext) == EXT

        with open(out_pkg, "wb") as out:
            out.write(head + ext + info + sfo)
            out.write(bytes(data_at - out.tell()))
            out.flush()
            subprocess.run(["openssl", "enc", "-aes-128-ctr", "-nopad", "-K", main_key.hex(), "-iv", iv.hex()],
                           stdin=plain, stdout=out, check=True)
    print("%s: %s, %d folders and %d files, %d bytes" % (out_pkg, content_id, len(dirs), len(files), total))


if __name__ == "__main__":
    main()
