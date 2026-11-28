#!/usr/bin/env python3
"""tools/loadscreen/logo.py: the tag put on a logo texture with the standard library alone. On made-up textures
and pictures; no game bytes here. Where Pillow, numpy and the Mac's font are (a Mac with the build's packages),
tag.png is also checked to be what rearmed.py draws.

    python3 tools/loadscreen/test_logo.py
"""
import os
import random
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import logo  # noqa: E402


def png(width, height, rgba, filters=None):
    """An 8-bit RGBA PNG of those pixels; FILTERS: the filter of each line (all five kinds are in the format)."""
    stride = width * 4
    raw = bytearray()
    prev = bytes(stride)
    for y in range(height):
        line = rgba[y * stride:(y + 1) * stride]
        kind = (filters or [0] * height)[y]
        out = bytearray(stride)
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            b = prev[i]
            c = prev[i - 4] if i >= 4 else 0
            p = a + b - c
            paeth = a if abs(p - a) <= abs(p - b) and abs(p - a) <= abs(p - c) else b if abs(p - b) <= abs(p - c) else c
            out[i] = (line[i] - (0, a, b, (a + b) >> 1, paeth)[kind]) & 255
        raw += bytes([kind]) + out
        prev = line

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    z = zlib.compress(bytes(raw))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", z[:7]) + chunk(b"IDAT", z[7:]) + chunk(b"IEND", b""))


def texture(blocks=None):
    """A DXT5 texture of the logo's size: a header as the game's has what logo.py reads, and 16-byte blocks."""
    head = bytearray(logo.HEADER)
    head[:4] = b"DDS "
    struct.pack_into("<II", head, 12, logo.HEIGHT, logo.WIDTH)
    head[84:88] = b"DXT5"
    n = logo.WIDTH * logo.HEIGHT // 16
    return bytes(head) + (blocks if blocks is not None else b"".join(
        logo.encode_block([(40 + (i * 7) % 60, 50, 90 + (i % 3) * 20, 255)] * 16) for i in range(n)))


def error(a, b):
    return sum((x[i] - y[i]) ** 2 for x, y in zip(a, b) for i in range(4)) / (len(a) * 4.0)


class Png(unittest.TestCase):
    def test_every_line_filter_reads_back(self):
        rng = random.Random(1)
        w, h = 13, 10
        rgba = bytes(rng.randrange(256) for _ in range(w * h * 4))
        with tempfile.TemporaryDirectory() as tmp:
            p = os.path.join(tmp, "x.png")
            with open(p, "wb") as f:
                f.write(png(w, h, rgba, [y % 5 for y in range(h)]))
            self.assertEqual(logo.read_png(p), (w, h, rgba))

    def test_what_is_not_such_a_png_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = os.path.join(tmp, "x.png")
            with open(p, "wb") as f:
                f.write(b"not a picture")
            with self.assertRaises(logo.Failure):
                logo.read_png(p)
            with open(p, "wb") as f:
                f.write(png(2, 2, bytes(16))[:60])
            with self.assertRaises((logo.Failure, zlib.error)):
                logo.read_png(p)


class Blocks(unittest.TestCase):
    def test_one_colour_comes_back_as_it_can_be_stored(self):
        for pixel in ((255, 255, 255, 255), (0, 0, 0, 0), (210, 24, 30, 255), (28, 20, 26, 128)):
            got = logo.decode_block(logo.encode_block([pixel] * 16))
            self.assertLess(error(got, [pixel] * 16), 20, pixel)  # 5, 6 and 5 bits of colour
            self.assertEqual({p[3] for p in got}, {pixel[3]})

    def test_two_colours_keep_their_edge(self):
        red, ink = (210, 24, 30, 255), (28, 20, 26, 255)
        pixels = [red if (i % 4) < 2 else ink for i in range(16)]
        got = logo.decode_block(logo.encode_block(pixels))
        self.assertLess(error(got, pixels), 12)
        self.assertLess(error(got[:2], [red] * 2), 12)
        self.assertLess(error(got[2:4], [ink] * 2), 12)

    def test_a_ramp_and_the_alphas_at_both_ends(self):
        pixels = [(i * 16, 255 - i * 16, 100, (0, 255, 60, 200)[i % 4]) for i in range(16)]
        got = logo.decode_block(logo.encode_block(pixels))
        self.assertLess(max(abs(g[3] - p[3]) for g, p in zip(got, pixels)), 16)
        self.assertEqual([g[3] for g, p in zip(got, pixels) if p[3] in (0, 255)], [p[3] for p in pixels if p[3] in (0, 255)])

    def test_the_first_end_is_the_greater(self):
        rng = random.Random(2)
        for _ in range(200):
            pixels = [tuple(rng.randrange(256) for _ in range(4)) for _ in range(16)]
            block = logo.encode_block(pixels)
            c0, c1 = struct.unpack_from("<HH", block, 8)
            self.assertGreaterEqual(c0, c1)
            self.assertEqual(len(block), 16)

    def test_transparent_pixels_do_not_pull_the_colours(self):
        pixels = [(210, 24, 30, 255)] * 8 + [(0, 255, 0, 0)] * 8  # the green is under no paint
        got = logo.decode_block(logo.encode_block(pixels))
        self.assertLess(error(got[:8], pixels[:8]), 12)
        self.assertEqual([p[3] for p in got[8:]], [0] * 8)

    def test_one_pixel_over_another(self):
        self.assertEqual(logo.over((10, 20, 30, 255), (200, 200, 200, 255)), (10, 20, 30, 255))
        self.assertEqual(logo.over((10, 20, 30, 0), (200, 100, 50, 255)), (200, 100, 50, 255))
        self.assertEqual(logo.over((0, 0, 0, 0), (9, 9, 9, 0)), (0, 0, 0, 0))
        self.assertEqual(logo.over((255, 0, 0, 128), (0, 0, 255, 255))[3], 255)
        r, g, b, a = logo.over((255, 0, 0, 128), (0, 0, 255, 0))  # over nothing: the paint's own colour
        self.assertEqual((r, g, b, a), (255, 0, 0, 128))


class Paint(unittest.TestCase):
    def tag(self, box):
        """A red patch with a soft edge in BOX (x0, y0, x1, y1), transparent elsewhere."""
        rgba = bytearray(logo.WIDTH * logo.HEIGHT * 4)
        x0, y0, x1, y1 = box
        for y in range(y0, y1):
            for x in range(x0, x1):
                edge = x in (x0, x1 - 1) or y in (y0, y1 - 1)
                rgba[(y * logo.WIDTH + x) * 4:(y * logo.WIDTH + x) * 4 + 4] = bytes((210, 24, 30, 120 if edge else 255))
        return logo.WIDTH, logo.HEIGHT, bytes(rgba)

    def test_only_the_blocks_under_the_tag_change(self):
        dds = texture()
        out, painted = logo.paint(dds, self.tag((101, 50, 147, 71)))
        self.assertEqual(len(out), len(dds))
        self.assertEqual(out[:logo.HEADER], dds[:logo.HEADER])
        changed = {i for i in range((len(dds) - logo.HEADER) // 16)
                   if out[logo.HEADER + i * 16:logo.HEADER + i * 16 + 16] != dds[logo.HEADER + i * 16:logo.HEADER + i * 16 + 16]}
        under = {by * (logo.WIDTH // 4) + bx for by in range(50 // 4, 70 // 4 + 1) for bx in range(101 // 4, 146 // 4 + 1)}
        self.assertEqual(changed, under)
        self.assertEqual(painted, len(under))
        inside = logo.decode_block(out[logo.HEADER + ((60 // 4) * 256 + 120 // 4) * 16:][:16])
        self.assertLess(error(inside, [(210, 24, 30, 255)] * 16), 20)

    def test_a_tag_or_a_texture_of_another_shape_is_refused(self):
        with self.assertRaises(logo.Failure):
            logo.paint(texture(), (8, 8, bytes(8 * 8 * 4)))
        with self.assertRaises(logo.Failure):
            logo.paint(texture()[:-16], self.tag((0, 0, 4, 4)))
        other = bytearray(texture(b"\0" * (logo.WIDTH * logo.HEIGHT)))
        other[84:88] = b"DXT1"
        with self.assertRaises(logo.Failure):
            logo.paint(bytes(other), self.tag((0, 0, 4, 4)))


class Tag(unittest.TestCase):
    def test_the_tag_is_a_picture_of_the_logos_size_with_paint_in_it(self):
        w, h, rgba = logo.read_png(logo.TAG)
        self.assertEqual((w, h), (logo.WIDTH, logo.HEIGHT))
        painted = sum(1 for i in range(3, len(rgba), 4) if rgba[i])
        self.assertTrue(30000 < painted < 60000, painted)
        self.assertEqual(rgba[3], 0)  # the corner is clear

    def test_it_is_what_rearmed_py_draws(self):
        py = os.environ.get("RS_PY") or os.path.join(HERE, "..", "..", "build", "venv", "bin", "python")
        fonts = ("/System/Library/Fonts/Supplemental/MarkerFelt.ttc", "/System/Library/Fonts/MarkerFelt.ttc")
        if not (os.path.exists(py) and any(os.path.exists(f) for f in fonts)
                and subprocess.run([py, "-c", "import PIL, numpy"], capture_output=True).returncode == 0):
            self.skipTest("no Python with Pillow and numpy, or no Marker Felt, here")
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "tag.png")
            subprocess.run([py, os.path.join(HERE, "rearmed.py"), "--tag-only", out], check=True)
            self.assertEqual(logo.read_png(out), logo.read_png(logo.TAG))


if __name__ == "__main__":
    unittest.main()
