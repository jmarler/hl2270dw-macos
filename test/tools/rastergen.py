#!/usr/bin/env python3
# This file is part of hl2270dw-macos.
#
# Copyright 2026 Jon Marler
#
# hl2270dw-macos is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.

"""Generate synthetic CUPS raster (v3, uncompressed) test pages.

The same black/white test pattern can be written as a 1-bit raster (what
macOS produces for our PPD) or as an 8-bit raster using only the values 0
and 255 (what macOS produces for Brother's PPD). Both encode exactly the
same pixels, which lets the two drivers be compared bit for bit.

Usage:
  rastergen.py OUT.ras [--bits 1|8] [--res 600] [--size Letter]
               [--pages N] [--duplex none|long|short] [--media N]
               [--position N] [--copies N] [--blank-pages 2,4]
               [--pattern test|edge|blank]
"""

import argparse
import struct
import sys

# PPD name -> (width pt, height pt). Matches Brother's HL-2270DW PPD.
PAPER = {
    'Letter': (612, 792), 'A4': (595, 842), 'Legal': (612, 1008),
    'Executive': (522, 756), 'A5': (420, 595), 'A5Rotated': (595, 420),
    'A6': (297, 420), 'ISOB5': (499, 709), 'ISOB6': (354, 499),
    'Env10': (297, 684), 'EnvDL': (312, 624), 'EnvC5': (459, 649),
    'EnvMonarch': (279, 540), '3x5': (216, 360),
    'FanFoldGermanLegal': (612, 936), 'EnvPRC5Rotated': (624, 312),
    'B5': (516, 729), 'Postcard': (284, 419), 'EnvYou4': (298, 666),
    'EnvChou3': (340, 666),
}
MARGINS = (18, 12, 18, 12)  # left, bottom, right, top (points)
HEADER_SIZE = 1796


def cstr(s, n):
    b = s.encode('ascii')[:n - 1]
    return b + b'\0' * (n - len(b))


def build_header(*, width_pt, height_pt, res, bits, size_name, duplex,
                 tumble, media_type, position, copies, ints):
    left, bottom, right, top = MARGINS
    w = round((width_pt - left - right) * res / 72)
    h = round((height_pt - bottom - top) * res / 72)
    bpl = (w * bits + 7) // 8
    hdr = bytearray(HEADER_SIZE)
    hdr[128:192] = cstr('', 64)                       # MediaType
    u = lambda off, val: struct.pack_into('<I', hdr, off, val)
    f = lambda off, val: struct.pack_into('<f', hdr, off, val)
    u(272, 1 if duplex else 0)                        # Duplex
    u(276, res); u(280, res)                          # HWResolution
    for i, v in enumerate((left, bottom, width_pt - right, height_pt - top)):
        u(284 + 4 * i, v)                             # ImagingBoundingBox
    u(312, left); u(316, bottom)                      # Margins
    u(324, position)                                  # MediaPosition
    u(340, copies)                                    # NumCopies
    u(352, width_pt); u(356, height_pt)               # PageSize
    u(368, 1 if tumble else 0)                        # Tumble
    u(372, w); u(376, h)                              # cupsWidth/Height
    u(380, media_type)                                # cupsMediaType
    u(384, bits); u(388, bits)                        # BitsPerColor/Pixel
    u(392, bpl)                                       # cupsBytesPerLine
    u(396, 0)                                         # cupsColorOrder
    u(400, 3)                                         # cupsColorSpace = K
    u(420, 1)                                         # cupsNumColors
    f(424, 1.0)                                       # BorderlessScaling
    f(428, float(width_pt)); f(432, float(height_pt))
    for i, v in enumerate((left, bottom, width_pt - right, height_pt - top)):
        f(436 + 4 * i, float(v))
    for i, v in ints.items():
        u(452 + 4 * i, v)                             # cupsInteger[i]
    hdr[1732:1796] = cstr(size_name, 64)              # cupsPageSizeName
    return bytes(hdr), w, h, bpl


def _row_spec(y, w, res, pattern, page_no):
    """Return (cache key, builder) for row y. builder() -> list of 0/1."""
    if pattern == 'blank':
        return ('blank',), None
    unit = max(1, res // 100)
    if pattern == 'edge':
        if y == 0:
            return ('edge-full',), lambda: [1] * w
        def edge_row():
            row = [0] * w
            for x in range(8):
                row[x] = 1
            row[w - 1] = 1
            return row
        return ('edge-mid',), edge_row
    band = (y // (unit * 40)) % 4
    if band == 0:
        # Fine checkerboard: hard case for the delta encoder.
        par = ((y // unit) + page_no) & 1
        return ('chk', par), lambda: [((x // unit) + par) & 1
                                      for x in range(w)]
    if band == 1:
        # Long runs (repeat records).
        return ('run',), lambda: [1 if w // 8 <= x < w // 2 else 0
                                  for x in range(w)]
    if band == 2:
        # Diagonal stripes: each line differs slightly from the previous.
        period = unit * 18
        phase = y % period
        return ('diag', phase), lambda: [
            1 if ((x + phase) // (unit * 6)) % 3 == 0 else 0
            for x in range(w)]
    if y % (unit * 7) == 0:
        # Sparse dots, mostly blank.
        step = unit * 13
        return ('dots',), lambda: [1 if x % step == 0 else 0
                                   for x in range(w)]
    return ('blank',), None


_EXPAND = [bytes(255 if (b >> (7 - i)) & 1 else 0 for i in range(8))
           for b in range(256)]


def page_rows(w, h, res, bits, bpl, pattern, page_no):
    """Yield packed raster rows for one page."""
    cache = {}
    for y in range(h):
        if pattern == 'edge' and y == h - 1:
            key, build = ('edge-full',), lambda: [1] * w
        else:
            key, build = _row_spec(y, w, res, pattern, page_no)
        out = cache.get(key)
        if out is None:
            packed = bytearray((w + 7) // 8)
            if build is not None:
                for x, v in enumerate(build()):
                    if v:
                        packed[x >> 3] |= 0x80 >> (x & 7)
            if bits == 1:
                out = bytes(packed) + bytes(bpl - len(packed))
            else:
                out = b''.join(_EXPAND[b] for b in packed)[:w]
                out += bytes(bpl - w)
            cache[key] = out
        yield out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--bits', type=int, default=1, choices=(1, 8))
    ap.add_argument('--res', type=int, default=600)
    ap.add_argument('--size', default='Letter')
    ap.add_argument('--custom', help='custom size WxH in points')
    ap.add_argument('--pages', type=int, default=1)
    ap.add_argument('--duplex', default='none',
                    choices=('none', 'long', 'short'))
    ap.add_argument('--media', type=int, default=1)
    ap.add_argument('--position', type=int, default=7)
    ap.add_argument('--copies', type=int, default=1)
    ap.add_argument('--blank-pages', default='')
    ap.add_argument('--pattern', default='test',
                    choices=('test', 'edge', 'blank'))
    ap.add_argument('--int', action='append', default=[],
                    help='cupsIntegerN=value')
    a = ap.parse_args()

    if a.custom:
        wpt, hpt = (int(v) for v in a.custom.split('x'))
        size_name = 'Custom.%dx%d' % (wpt, hpt)
    else:
        wpt, hpt = PAPER[a.size]
        size_name = a.size
    ints = {}
    for spec in a.int:
        k, v = spec.split('=')
        ints[int(k)] = int(v)
    blanks = {int(p) for p in a.blank_pages.split(',') if p}

    with open(a.out, 'wb') as out:
        out.write(b'3SaR')
        for page in range(1, a.pages + 1):
            hdr, w, h, bpl = build_header(
                width_pt=wpt, height_pt=hpt, res=a.res, bits=a.bits,
                size_name=size_name, duplex=a.duplex != 'none',
                tumble=a.duplex == 'short', media_type=a.media,
                position=a.position, copies=a.copies, ints=ints)
            out.write(hdr)
            pattern = 'blank' if page in blanks else a.pattern
            for row in page_rows(w, h, a.res, a.bits, bpl, pattern, page):
                out.write(row)
    return 0


if __name__ == '__main__':
    sys.exit(main())
