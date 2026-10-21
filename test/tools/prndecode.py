#!/usr/bin/env python3
# This file is part of hl2270dw-macos.
#
# Copyright 2026 Jon Marler
#
# hl2270dw-macos is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.

"""Decode a Brother PJL/PCL "1030" print stream.

Returns the PJL lines, the PCL commands, and every page as a list of
raster lines, so two print streams can be compared by what the printer
would render rather than by their exact compressed bytes.

Usage:
  prndecode.py FILE.prn            print a summary
  prndecode.py FILE.prn --pbm DIR  also write each page as a PBM image
"""

import os
import sys

UEL = b'\x1b%-12345X'


class DecodeError(Exception):
    pass


class Page:
    def __init__(self):
        self.lines = []        # list of bytes, one per raster line
        self.commands = []     # PCL commands seen before this page's data

    def normalized(self, shift=0):
        """Lines with trailing zero bytes removed, optionally shifted left
        by `shift` bytes (used to cancel a known margin offset)."""
        out = []
        for line in self.lines:
            line = line[shift:] if shift else line
            out.append(line.rstrip(b'\0'))
        return out

    def left_margin_bytes(self):
        """Smallest number of leading zero bytes over all non-blank lines."""
        best = None
        for line in self.lines:
            stripped = line.lstrip(b'\0')
            if stripped:
                lead = len(line) - len(stripped)
                best = lead if best is None else min(best, lead)
        return best


class Stream:
    def __init__(self):
        self.pjl = []          # PJL lines, in order (without trailing EOL)
        self.commands = []     # every PCL command, in order
        self.pages = []
        self.uel_count = 0
        self.leading_nuls = 0


def _read_overflow(data, pos):
    total = 0
    while True:
        if pos >= len(data):
            raise DecodeError('overflow runs past end of block')
        b = data[pos]
        pos += 1
        total += b
        if b != 255:
            return total, pos


def _decode_line(data, pos, prev):
    """Decode one encoded line starting at data[pos]; return (line, pos)."""
    num_edits = data[pos]
    pos += 1
    if num_edits == 0xFF:
        return b'', pos
    line = bytearray(prev)
    cursor = 0
    for _ in range(num_edits):
        cmd = data[pos]
        pos += 1
        if cmd & 0x80:
            offset = (cmd >> 5) & 3
            if offset == 3:
                extra, pos = _read_overflow(data, pos)
                offset += extra
            count = cmd & 31
            if count == 31:
                extra, pos = _read_overflow(data, pos)
                count += extra
            count += 2
            value = data[pos]
            pos += 1
            cursor += offset
            end = cursor + count
            if end > len(line):
                line.extend(bytes(end - len(line)))
            line[cursor:end] = bytes([value]) * count
            cursor = end
        else:
            offset = (cmd >> 3) & 15
            if offset == 15:
                extra, pos = _read_overflow(data, pos)
                offset += extra
            count = cmd & 7
            if count == 7:
                extra, pos = _read_overflow(data, pos)
                count += extra
            count += 1
            cursor += offset
            end = cursor + count
            if end > len(line):
                line.extend(bytes(end - len(line)))
            line[cursor:end] = data[pos:pos + count]
            pos += count
            cursor = end
    return bytes(line), pos


def _decode_block(block, page):
    if len(block) < 2:
        raise DecodeError('short raster block')
    nlines = (block[0] << 8) | block[1]
    pos = 2
    prev = page.lines[-1] if page.lines else b''
    for _ in range(nlines):
        line, pos = _decode_line(block, pos, prev)
        page.lines.append(line)
        prev = line
    if pos != len(block):
        raise DecodeError('raster block has %d trailing bytes'
                          % (len(block) - pos))


def decode(data):
    s = Stream()
    pos = 0
    while pos < len(data) and data[pos] == 0:
        pos += 1
    s.leading_nuls = pos
    page = None
    while pos < len(data):
        if data.startswith(UEL, pos):
            s.uel_count += 1
            pos += len(UEL)
            # PJL lines follow until ENTER LANGUAGE or the next escape.
            while pos < len(data) and data.startswith(b'@PJL', pos):
                end = data.index(b'\n', pos)
                line = data[pos:end].rstrip(b'\r').decode('latin1')
                s.pjl.append(line)
                pos = end + 1
                if line.replace(' ', '').startswith('@PJLENTERLANGUAGE'):
                    break
            continue
        b = data[pos]
        if b in (0x0A, 0x0D):
            pos += 1
            continue
        if b == 0x0C:
            if page is None:
                raise DecodeError('form feed outside a page')
            s.pages.append(page)
            page = None
            pos += 1
            continue
        if b != 0x1B:
            raise DecodeError('unexpected byte 0x%02x at %d' % (b, pos))
        pos += 1
        c1 = chr(data[pos])
        pos += 1
        if c1 == 'E':
            s.commands.append('E')
            continue
        c2 = chr(data[pos])
        pos += 1
        # Parameterized sequence: value + parameter char, repeated while
        # the parameter char is lowercase.
        while True:
            start = pos
            while chr(data[pos]) in '+-.0123456789':
                pos += 1
            value = data[start:pos].decode('ascii')
            param = chr(data[pos])
            pos += 1
            cmd = c1 + c2 + value + param
            if param.lower() == 'w':
                n = int(value)
                block = data[pos:pos + n]
                pos += n
                if page is None:
                    page = Page()
                _decode_block(block, page)
                cmd = c1 + c2 + 'w'
            else:
                s.commands.append(cmd)
                if page is None and cmd != c1 + c2 + value + 'M':
                    pass
                if cmd.lower() == '*b1030m' and page is None:
                    page = Page()
                if page is not None and param.lower() != 'w':
                    page.commands.append(cmd)
            if param.isupper():
                break
            # Lowercase: the sequence continues with another parameter.
            if pos < len(data) and not (chr(data[pos]) in '+-.0123456789'):
                break
    if page is not None and page.lines:
        raise DecodeError('stream ended inside a page')
    return s


def write_pbm(page, path):
    width = max((len(l) for l in page.lines), default=0)
    with open(path, 'wb') as f:
        f.write(b'P4 %d %d\n' % (width * 8, len(page.lines)))
        for line in page.lines:
            f.write(line + bytes(width - len(line)))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    data = open(sys.argv[1], 'rb').read()
    s = decode(data)
    print('leading NULs: %d, UELs: %d' % (s.leading_nuls, s.uel_count))
    for line in s.pjl:
        print('PJL  ' + line)
    print('PCL  ' + ' '.join(s.commands))
    for i, page in enumerate(s.pages, 1):
        width = max((len(l) for l in page.lines), default=0)
        print('page %d: %d lines, %d bytes wide, left margin %s bytes, '
              'commands %s' % (i, len(page.lines), width,
                               page.left_margin_bytes(),
                               ' '.join(page.commands)))
    if '--pbm' in sys.argv:
        outdir = sys.argv[sys.argv.index('--pbm') + 1]
        os.makedirs(outdir, exist_ok=True)
        for i, page in enumerate(s.pages, 1):
            write_pbm(page, os.path.join(outdir, 'page-%d.pbm' % i))
    return 0


if __name__ == '__main__':
    sys.exit(main())
