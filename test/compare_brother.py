#!/usr/bin/env python3
# This file is part of hl2270dw-macos.
#
# Copyright 2026 Jon Marler
#
# hl2270dw-macos is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.

"""Compare rastertohl2270dw with Brother's own HL-2270DW filter.

Each test case renders the same black and white pixels twice: as an 8-bit
raster for Brother's filter (what macOS produces for Brother's PPD) and as
a 1-bit raster for ours. Both filters run with equivalent options, and the
results are decoded and compared: PJL lines, PCL commands, and the decoded
bitmap of every page. The compressed bytes differ (the two encoders make
different but equally valid choices), so they are not compared.

Brother's driver must be installed, and on Apple silicon it runs under
Rosetta. This is a development tool; it is not part of `make check`.

Usage:
  compare_brother.py BUILD_DIR [--keep DIR] [--only NAME]
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'tools'))
import prndecode  # noqa: E402

BROTHER_FILTER = ('/Library/Printers/Brother/Filter/rastertobrother2130.bundle/'
                  'Contents/MacOS/rastertobrother2130')
BROTHER_PPD = ('/Library/Printers/PPDs/Contents/Resources/'
               'Brother HL-2270DW series CUPS.gz')

ALL_SIZES = ['Letter', 'A4', 'Legal', 'Executive', 'A5', 'A5Rotated', 'A6',
             'ISOB5', 'ISOB6', 'Env10', 'EnvDL', 'EnvC5', 'EnvMonarch', '3x5',
             'FanFoldGermanLegal', 'EnvPRC5Rotated', 'B5', 'Postcard',
             'EnvYou4', 'EnvChou3']


def case(name, raster, brother='', ours=None, user='jmarler', title='Test Job',
         sanitized_names=False):
    """raster: rastergen arguments; brother/ours: option strings.

    sanitized_names: the title contains characters that our driver replaces
    (Brother's driver writes them into the PJL unchanged, producing a
    malformed command).
    """
    return dict(name=name, raster=raster, brother=brother,
                ours=brother if ours is None else ours,
                user=user, title=title, sanitized_names=sanitized_names)


def build_cases():
    cases = [
        case('letter-600-default', '--res 600 --size Letter --pages 2'),
        case('letter-300', '--res 300 --size Letter'),
        case('letter-1200-hq', '--res 1200 --size Letter'),
        case('a4-toner-save', '--res 600 --size A4',
             'TonerSaveMode=ON'),
        case('legal-duplex-long', '--res 600 --size Legal --pages 3 '
             '--duplex long', 'Duplex=DuplexNoTumble'),
        case('letter-duplex-short', '--res 300 --size Letter --pages 2 '
             '--duplex short', 'Duplex=DuplexTumble'),
        case('less-paper-curl', '--res 300 --size A4',
             'BRImproveOutput=BRLessPaperCurl'),
        case('fix-intensity-ghost', '--res 300 --size A4',
             'BRImproveOutput=BRFixIntensity BRReduceGhost=ON'),
        case('sleep-15', '--res 300 --size Letter',
             'BRSleepTime=Custom.15 '
             'com.brother.print.PrintSettings.sleepmode=Manual '
             'com.brother.print.PrintSettings.sleep=15',
             'BRSleepTime=15'),
        case('tray1', '--res 300 --size Letter --pages 2',
             'BRInputSlot=Tray1', 'InputSlot=Tray1'),
        case('manual-feed', '--res 300 --size Letter --pages 2',
             'BRInputSlot=Manual', 'InputSlot=Manual'),
        case('first-page-manual', '--res 300 --size Letter --pages 3',
             'BRInputSlot=Tray1 AP_FIRSTPAGE_InputSlot=Manual',
             'InputSlot=Tray1 AP_FIRSTPAGE_InputSlot=Manual'),
        case('skip-blank', '--res 300 --size Letter --pages 4 '
             '--blank-pages 2,4', 'BRSkipBlank=ON'),
        case('blank-not-skipped', '--res 300 --size Letter --pages 2 '
             '--blank-pages 2'),
        case('copies', '--res 300 --size Letter --copies 3'),
        case('postcard', '--res 300 --size Postcard --media 12',
             'PageSize=Postcard media=Postcard', 'PageSize=Postcard'),
        case('custom-400x700', '--res 600 --custom 400x700 --pattern edge',
             'PageSize=Custom.400x700'),
        case('custom-612x1152', '--res 300 --custom 612x1152 --pattern edge',
             'PageSize=Custom.612x1152'),
        case('long-names', '--res 300 --size Letter',
             user='a-very-long-user-name-' * 5,
             title='A "quoted" title that is longer than eighty characters '
                   'and keeps going on and on and on',
             sanitized_names=True),
    ]
    for media in range(2, 13):
        if media != 6:  # 6 (transparency) is not offered for this model
            cases.append(case('media-%d' % media,
                              '--res 300 --size A6 --media %d' % media))
    for size in ALL_SIZES:
        # Brother's driver selects the thick paper path for postcards only
        # when the job also has media=Postcard; ours always does.
        extra = ' media=Postcard' if size == 'Postcard' else ''
        cases.append(case('size-%s' % size,
                          '--res 300 --size %s --pattern edge' % size,
                          'PageSize=%s%s' % (size, extra),
                          'PageSize=%s' % size))
    return cases


def run(cmd, env=None, stdout=None):
    subprocess.run(cmd, check=True, env=env, stdout=stdout,
                   stderr=subprocess.DEVNULL)


def sanitize_quoted_value(line):
    """Apply our driver's character replacement to a NAME="..." value."""
    start = line.find('"')
    end = line.rfind('"')
    if start < 0 or end <= start:
        return line
    value = ''.join(c if 32 <= ord(c) < 127 and c not in '"\\' else '_'
                    for c in line[start + 1:end])
    return line[:start + 1] + value + line[end:]


def normalize_pjl(lines, sanitize=False):
    out = []
    for l in lines:
        if l.startswith('@PJL PRINTLOG ITEM = 2,'):
            # The job log timestamp differs between the two runs.
            l = '@PJL PRINTLOG ITEM = 2,<time>'
        elif sanitize and ('NAME="' in l or 'USER' in l):
            l = sanitize_quoted_value(l)
        out.append(l)
    return out


def compare(c, build_dir, work):
    gen = [sys.executable, '-I', os.path.join(HERE, 'tools', 'rastergen.py')]
    args = c['raster'].split()
    r8 = os.path.join(work, c['name'] + '-8.ras')
    r1 = os.path.join(work, c['name'] + '-1.ras')
    run(gen + [r8, '--bits', '8'] + args)
    run(gen + [r1, '--bits', '1'] + args)

    b_out = os.path.join(work, c['name'] + '-brother.prn')
    o_out = os.path.join(work, c['name'] + '-ours.prn')
    with open(b_out, 'wb') as f:
        run([BROTHER_FILTER, '1', c['user'], c['title'], '1', c['brother'],
             r8], env=dict(os.environ, PPD=BROTHER_PPD), stdout=f)
    with open(o_out, 'wb') as f:
        run([os.path.join(build_dir, 'rastertohl2270dw'), '1', c['user'],
             c['title'], '1', c['ours'], r1],
            env=dict(os.environ,
                     PPD=os.path.join(build_dir, 'ppd', 'hl2270dw.ppd')),
            stdout=f)

    b = prndecode.decode(open(b_out, 'rb').read())
    o = prndecode.decode(open(o_out, 'rb').read())
    problems = []
    bp = normalize_pjl(b.pjl, c['sanitized_names'])
    op = normalize_pjl(o.pjl)
    if bp != op:
        for i in range(max(len(bp), len(op))):
            bl = bp[i] if i < len(bp) else '<none>'
            ol = op[i] if i < len(op) else '<none>'
            if bl != ol:
                problems.append('PJL line %d: brother %r, ours %r' % (i, bl, ol))
                break
    if b.commands != o.commands:
        problems.append('PCL commands: brother %s, ours %s'
                        % (' '.join(b.commands), ' '.join(o.commands)))
    if len(b.pages) != len(o.pages):
        problems.append('pages: brother %d, ours %d'
                        % (len(b.pages), len(o.pages)))
    for i, (bpage, opage) in enumerate(zip(b.pages, o.pages), 1):
        bl, ol = bpage.normalized(), opage.normalized()
        if len(bl) != len(ol):
            problems.append('page %d: brother %d lines, ours %d'
                            % (i, len(bl), len(ol)))
            continue
        diff = [y for y, (x1, x2) in enumerate(zip(bl, ol)) if x1 != x2]
        if diff:
            problems.append('page %d: %d of %d lines differ (first: %d)'
                            % (i, len(diff), len(bl), diff[0]))
    return problems, len(b.pages)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    build_dir = os.path.abspath(sys.argv[1])
    for path in (BROTHER_FILTER, BROTHER_PPD):
        if not os.path.exists(path):
            print('SKIP: Brother driver not installed (%s missing)' % path)
            return 0
    keep = sys.argv[sys.argv.index('--keep') + 1] if '--keep' in sys.argv else None
    only = sys.argv[sys.argv.index('--only') + 1] if '--only' in sys.argv else None

    work = keep or tempfile.mkdtemp(prefix='hl2270dw-compare-')
    os.makedirs(work, exist_ok=True)
    failures = 0
    cases = [c for c in build_cases() if not only or only in c['name']]
    for c in cases:
        try:
            problems, pages = compare(c, build_dir, work)
        except Exception as e:  # report and keep going
            problems, pages = ['error: %s' % e], 0
        status = 'ok  ' if not problems else 'FAIL'
        print('%s %-28s %d page(s)' % (status, c['name'], pages))
        for p in problems:
            print('       ' + p)
        failures += bool(problems)
    print('%d of %d cases match Brother\'s driver' % (len(cases) - failures,
                                                      len(cases)))
    if not keep:
        subprocess.run(['rm', '-rf', work])
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
