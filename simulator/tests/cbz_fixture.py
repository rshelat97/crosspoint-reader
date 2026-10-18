#!/usr/bin/env python3
"""Build the comics fixture used by the simulator smoke test.

Usage: cbz_fixture.py <repo-root> <out.cbz>

Pages (in natural order once sorted): a 16-step gradient exactly on the 16
brightness levels (page_1.png), a small ramp that gets letterboxed
(page_2.png), two JPEGs and a PNG from the EPUB test fixtures, plus entries
the reader must ignore (__MACOSX, a dotfile, a text file). Deterministic:
entry timestamps are fixed, so the archive bytes never change.
"""
import os
import struct
import sys
import zipfile
import zlib


def png_gray(width, height, rows):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def main():
    root, out = sys.argv[1], sys.argv[2]
    gradient = png_gray(480, 800, [[(x * 16 // 480) * 17 for x in range(480)] for _ in range(800)])
    ramp = png_gray(300, 200, [[(x * 255) // 299 for x in range(300)] for _ in range(200)])
    jpegs = zipfile.ZipFile(os.path.join(root, "test/epubs/test_jpeg_images.epub"))
    pngs = zipfile.ZipFile(os.path.join(root, "test/epubs/test_png_images.epub"))
    with zipfile.ZipFile(out, "w", zipfile.ZIP_STORED) as z:
        def add(name, data):
            z.writestr(zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0)), data)

        add("page_10.jpg", jpegs.read("OEBPS/images/jpeg_format.jpg"))
        add("page_2.png", ramp)
        add("__MACOSX/._page_1.png", b"junk")
        add("notes.txt", b"not a page")
        add("page_1.png", gradient)
        add("page_3.jpg", jpegs.read("OEBPS/images/grayscale_test.jpg"))
        add(".hidden.png", b"junk")
        add("page_4.png", pngs.read("OEBPS/images/png_format.png"))
    print("wrote", out, os.path.getsize(out), "bytes")


if __name__ == "__main__":
    main()
