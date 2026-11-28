#!/usr/bin/env python3
"""static.psarc with the "Re-ARMed 2026" tag on the title-screen logo, with the standard library alone.

    logo.py <static.psarc> <out.psarc> [--tag tag.png]

What build.sh does, for a machine without Pillow, numpy, etcpak and the Mac's Marker Felt (the Linux setup): the tag
is not drawn here but read from tag.png beside this file (`rearmed.py --tag-only` made it: the tag alone, our own
artwork), put over the two logo textures of the archive (gfxassets/views/logo_elk_final.dds and
logo_graded_elk_final.dds: DXT5, 1024 x 256, no mipmaps), and the archive is written with those two entries
replaced (psarc_pack.py). Only the 4 x 4 blocks the tag touches are decoded, painted and encoded again; every
other block of the textures keeps its bytes. The archive's key is found as psarc_extract.py finds it, and
`openssl` does the AES, as for every tool here.

The encoder is this file's own: for a block's colours the two ends of their principal axis, refined by least
squares over the block's own choice of the four colours; for its alpha the better of the two layouts DXT5 has.
"""
import argparse
import os
import struct
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import psarc_extract  # noqa: E402
import psarc_pack  # noqa: E402
del sys.path[0]

LOGOS = ("gfxassets/views/logo_elk_final.dds", "gfxassets/views/logo_graded_elk_final.dds")
WIDTH, HEIGHT, HEADER = 1024, 256, 128
TAG = os.path.join(HERE, "tag.png")


class Failure(Exception):
    pass


# --- a PNG, read ---------------------------------------------------------------------------------------------

def read_png(path):
    """(width, height, RGBA bytes) of an 8-bit RGBA PNG that is not interlaced (what Pillow writes for tag.png)."""
    with open(path, "rb") as f:
        d = f.read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise Failure("%s is not a PNG" % path)
    at, idat, head = 8, [], None
    while at + 8 <= len(d):
        n, kind = struct.unpack_from(">I4s", d, at)
        body = d[at + 8:at + 8 + n]
        at += 12 + n
        if kind == b"IHDR":
            head = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    if not head or head[2:] != (8, 6, 0, 0, 0):
        raise Failure("%s is not an 8-bit RGBA PNG" % path)
    w, h = head[0], head[1]
    raw = zlib.decompress(b"".join(idat))
    stride = w * 4
    if len(raw) != h * (stride + 1):
        raise Failure("%s is cut short" % path)
    out = bytearray(h * stride)
    prev = bytearray(stride)
    for y in range(h):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if kind == 1:
            for i in range(4, stride):
                line[i] = (line[i] + line[i - 4]) & 255
        elif kind == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif kind == 3:
            for i in range(stride):
                line[i] = (line[i] + (((line[i - 4] if i >= 4 else 0) + prev[i]) >> 1)) & 255
        elif kind == 4:
            for i in range(stride):
                a = line[i - 4] if i >= 4 else 0
                b = prev[i]
                c = prev[i - 4] if i >= 4 else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        elif kind != 0:
            raise Failure("%s has a line filter this does not know" % path)
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, bytes(out)


# --- DXT5 blocks ---------------------------------------------------------------------------------------------

def expand565(c):
    r, g, b = c >> 11, (c >> 5) & 63, c & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def colour_palette(c0, c1):
    """The four colours of a DXT5 colour block: its two ends and the two thirds between them."""
    a, b = expand565(c0), expand565(c1)
    return (a, b, tuple((2 * a[i] + b[i]) // 3 for i in range(3)), tuple((a[i] + 2 * b[i]) // 3 for i in range(3)))


def alpha_palette(a0, a1):
    if a0 > a1:
        return (a0, a1) + tuple(((7 - i) * a0 + i * a1) // 7 for i in range(1, 7))
    return (a0, a1) + tuple(((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)) + (0, 255)


def decode_block(block):
    """The 16 pixels of a DXT5 block, as (r, g, b, a), row by row."""
    alphas = alpha_palette(block[0], block[1])
    abits = int.from_bytes(block[2:8], "little")
    c0, c1, cbits = struct.unpack_from("<HHI", block, 8)
    colours = colour_palette(c0, c1)
    return [colours[(cbits >> (2 * i)) & 3] + (alphas[(abits >> (3 * i)) & 7],) for i in range(16)]


def encode_alpha(alphas):
    """Eight bytes for 16 alphas: the layout with eight steps between the least and the most, or the one with six
    steps and 0 and 255 besides, whichever is closer."""
    lo, hi = min(alphas), max(alphas)
    if lo == hi:
        return bytes((lo, lo)) + b"\0" * 6
    tries = [(hi, lo)]
    inner = [a for a in alphas if 0 < a < 255]
    if len(inner) < 16:
        tries.append((min(inner), max(inner)) if inner and min(inner) < max(inner) else
                     (inner[0], inner[0]) if inner else (0, 255))
    best = None
    for a0, a1 in tries:
        if a0 == a1 and (a0, a1) != tries[0]:
            a1 = min(a0 + 1, 255)  # six steps want a0 <= a1; 0 and 255 are in the table either way
            a0 = a1 - 1
        pal = alpha_palette(a0, a1)
        bits = err = 0
        for i, a in enumerate(alphas):
            k = min(range(8), key=lambda j: abs(pal[j] - a))
            err += (pal[k] - a) ** 2
            bits |= k << (3 * i)
        if best is None or err < best[0]:
            best = (err, bytes((a0, a1)) + bits.to_bytes(6, "little"))
    return best[1]


def to565(rgb):
    r, g, b = (min(max(int(v + 0.5), 0), 255) for v in rgb)
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)


def assign(colours, weights, c0, c1):
    """Each pixel's nearest of the four colours, and the weighted squared error of that."""
    pal = colour_palette(c0, c1)
    picks, err = [], 0.0
    for (r, g, b), w in zip(colours, weights):
        best, bd = 0, None
        for k, (pr, pg, pb) in enumerate(pal):
            d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
            if bd is None or d < bd:
                best, bd = k, d
        picks.append(best)
        err += w * bd
    return picks, err


def encode_colour(colours, weights):
    """Eight bytes for 16 colours (pixels of no weight, the transparent ones, do not count)."""
    total = sum(weights)
    if total <= 0:
        colours, weights, total = colours, [1.0] * 16, 16.0
    mean = [sum(c[i] * w for c, w in zip(colours, weights)) / total for i in range(3)]
    cov = [[sum(w * (c[i] - mean[i]) * (c[j] - mean[j]) for c, w in zip(colours, weights)) for j in range(3)]
           for i in range(3)]
    axis = [cov[0][0] + cov[0][1] + cov[0][2] or 1.0, cov[1][0] + cov[1][1] + cov[1][2] or 1.0,
            cov[2][0] + cov[2][1] + cov[2][2] or 1.0]
    for _ in range(8):  # the principal axis, by power iteration
        nxt = [sum(cov[i][j] * axis[j] for j in range(3)) for i in range(3)]
        size = max(abs(v) for v in nxt)
        if size < 1e-9:
            break
        axis = [v / size for v in nxt]
    dots = [sum((c[i] - mean[i]) * axis[i] for i in range(3)) for c, w in zip(colours, weights) if w > 0] or [0.0]
    norm = sum(v * v for v in axis) or 1.0
    lo, hi = min(dots) / norm, max(dots) / norm
    c0 = to565([mean[i] + axis[i] * hi for i in range(3)])
    c1 = to565([mean[i] + axis[i] * lo for i in range(3)])
    picks, err = assign(colours, weights, c0, c1)
    best = (err, c0, c1, picks)
    # The ends that suit the pixels' own choices best (least squares), a few rounds while it gets better.
    share = ((1.0, 0.0), (0.0, 1.0), (2 / 3, 1 / 3), (1 / 3, 2 / 3))
    for _ in range(3):
        aa = ab = bb = 0.0
        ax, bx = [0.0] * 3, [0.0] * 3
        for c, w, k in zip(colours, weights, best[3]):
            a, b = share[k]
            aa += w * a * a
            ab += w * a * b
            bb += w * b * b
            for i in range(3):
                ax[i] += w * a * c[i]
                bx[i] += w * b * c[i]
        det = aa * bb - ab * ab
        if abs(det) < 1e-9:
            break
        n0 = to565([(ax[i] * bb - bx[i] * ab) / det for i in range(3)])
        n1 = to565([(bx[i] * aa - ax[i] * ab) / det for i in range(3)])
        if (n0, n1) == best[1:3]:
            break
        picks, err = assign(colours, weights, n0, n1)
        if err >= best[0]:
            break
        best = (err, n0, n1, picks)
    _, c0, c1, picks = best
    if c0 < c1:  # the first end the greater, as every decoder reads four colours then
        c0, c1 = c1, c0
        picks = [(1, 0, 3, 2)[k] for k in picks]
    bits = 0
    for i, k in enumerate(picks):
        bits |= k << (2 * i)
    return struct.pack("<HHI", c0, c1, bits)


def encode_block(pixels):
    return encode_alpha([p[3] for p in pixels]) + encode_colour([p[:3] for p in pixels], [p[3] / 255.0 for p in pixels])


def over(top, under):
    """One pixel over another, both with straight alpha."""
    ta = top[3] / 255.0
    if ta >= 1.0:
        return top
    ua = under[3] / 255.0
    oa = ta + ua * (1 - ta)
    if oa <= 0:
        return (0, 0, 0, 0)
    return tuple(min(255, int((top[i] * ta + under[i] * ua * (1 - ta)) / oa + 0.5)) for i in range(3)) + \
        (min(255, int(oa * 255 + 0.5)),)


def paint(dds, tag):
    """The texture DDS (its bytes) with TAG (width, height, RGBA) over it; (the new bytes, blocks painted)."""
    w, h, rgba = tag
    if (w, h) != (WIDTH, HEIGHT):
        raise Failure("the tag is %d x %d, the logo %d x %d" % (w, h, WIDTH, HEIGHT))
    if len(dds) != HEADER + WIDTH * HEIGHT or dds[:4] != b"DDS " or dds[84:88] != b"DXT5":
        raise Failure("the logo texture is not the DXT5 one of %d x %d this was written for" % (WIDTH, HEIGHT))
    if struct.unpack_from("<II", dds, 12) != (HEIGHT, WIDTH):
        raise Failure("the logo texture is not %d x %d" % (WIDTH, HEIGHT))
    out = bytearray(dds)
    painted = 0
    for by in range(HEIGHT // 4):
        for bx in range(WIDTH // 4):
            top = []
            for y in range(by * 4, by * 4 + 4):
                at = (y * WIDTH + bx * 4) * 4
                top += [tuple(rgba[at + 4 * i:at + 4 * i + 4]) for i in range(4)]
            if not any(p[3] for p in top):
                continue
            at = HEADER + (by * (WIDTH // 4) + bx) * 16
            under = decode_block(dds[at:at + 16])
            out[at:at + 16] = encode_block([over(t, u) for t, u in zip(top, under)])
            painted += 1
    return bytes(out), painted


# --- the archive ---------------------------------------------------------------------------------------------

def build(src, dst, tag_path=TAG):
    """DST: the archive SRC with the tag on its two logos. Complete or not there."""
    tag = read_png(tag_path)
    with open(src, "rb") as f:
        if f.read(4) != b"PSAR":
            raise Failure("%s is not a psarc archive" % src)
    f, block_size, entries, zsizes = psarc_extract.read_archive(src)
    try:
        names = psarc_extract.read_entry(f, block_size, zsizes, entries[0]).decode().split("\n")
        found = {n: e for n, e in zip(names, entries[1:]) if n in LOGOS}
        if len(found) != len(LOGOS):
            raise Failure("%s has no title logo (%s)" % (src, ", ".join(n for n in LOGOS if n not in found)))
        textures = {n: psarc_extract.read_entry(f, block_size, zsizes, e) for n, e in found.items()}
    finally:
        f.close()
    with tempfile.TemporaryDirectory(prefix="rs-logo-") as work:
        args = []
        for i, name in enumerate(LOGOS):
            data, _ = paint(textures[name], tag)
            path = os.path.join(work, "%d.dds" % i)
            with open(path, "wb") as o:
                o.write(data)
            args.append("%s=%s" % (name, path))
        part = dst + ".part"
        argv, sys.argv = sys.argv, ["psarc_pack.py", src, part] + args
        try:
            psarc_pack.main()
        finally:
            sys.argv = argv
        os.replace(part, dst)


def main(argv=None):
    ap = argparse.ArgumentParser(description="static.psarc with the Re-ARMed tag on the title-screen logo.")
    ap.add_argument("archive")
    ap.add_argument("out")
    ap.add_argument("--tag", default=TAG)
    a = ap.parse_args(argv)
    try:
        build(a.archive, a.out, a.tag)
    except Failure as e:
        sys.stderr.write("logo: %s\n" % e)
        return 1
    finally:
        if os.path.exists(a.out + ".part"):
            os.unlink(a.out + ".part")
    print("wrote %s" % a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
