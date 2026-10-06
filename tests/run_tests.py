#!/usr/bin/env python3
"""Test suite for the fca-multi-builder tools.

    run_tests.py BIN_DIR [--runner CMD]

BIN_DIR holds fcabuild, fcasvedt, fca-mkfs and gbaraw (or the .exe
versions).  --runner runs them through another program, e.g. "wine".
Only the Python standard library is needed.
"""
import hashlib
import os
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, 'original')  # the files from the old package
FAILED = []
PASSED = 0


def check(cond, what):
    global PASSED
    if cond:
        PASSED += 1
    else:
        FAILED.append(what)
        print('  FAIL:', what)


class Tools:
    def __init__(self, bindir, runner):
        self.bindir = os.path.abspath(bindir)
        self.runner = shlex.split(runner) if runner else []

    def path(self, tool):
        for name in (tool, tool + '.exe'):
            p = os.path.join(self.bindir, name)
            if os.path.exists(p):
                return p
        sys.exit('missing %s in %s' % (tool, self.bindir))

    def run(self, tool, *args, ok=True):
        cmd = self.runner + [self.path(tool)] + list(args)
        env = dict(os.environ, WINEDEBUG='-all')
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        out = r.stdout.decode('utf-8', 'replace') + r.stderr.decode('utf-8', 'replace')
        if ok and r.returncode != 0:
            print(out)
            raise AssertionError('%s %s failed (%d)' % (tool, ' '.join(args), r.returncode))
        return r.returncode, out


def read(p):
    with open(p, 'rb') as f:
        return f.read()


def u32(d, o):
    return struct.unpack_from('<I', d, o)[0]


def entries_pnes(img, start):
    """Walk PocketNES/PCEAdvance headers: (title, size, flags, follow, reserved, offset)."""
    out = []
    o = start
    while o + 48 <= len(img) and u32(img, o) != 0x41700417:
        title = img[o:o + 32].split(b'\0')[0].decode('latin-1')
        size, flags, follow, res = struct.unpack_from('<4I', img, o + 32)
        out.append((title, size, flags, follow, res, o))
        o += 48 + size
    return out, o


def entries_fca(img, start):
    out = []
    o = start
    while u32(img, o) == 0x04174170:
        name = img[o + 4:o + 36].split(b'\0')[0]
        ext = img[o + 36:o + 40].split(b'\0')[0].decode()
        length = u32(img, o + 40)
        out.append((name, ext, length, o + 44))
        o += 44 + ((length + 3) & ~3)
    return out, o


def gba_header_ok(img):
    s = sum(img[0xA0:0xBD])
    return img[0xBD] == (-(s + 0x19)) & 0xFF and img[0xB2] == 0x96


def main():
    args = sys.argv[1:]
    runner = None
    if '--runner' in args:
        i = args.index('--runner')
        runner = args[i + 1]
        del args[i:i + 2]
    if not args:
        sys.exit(__doc__)
    t = Tools(args[0], runner)
    work = tempfile.mkdtemp(prefix='fca-tests-')
    os.chdir(work)
    print('Working in', work)
    subprocess.run([sys.executable, os.path.join(ROOT, 'tests', 'make-test-roms.py'), 'roms',
                    os.path.join(DATA, 'gbongba-0.4.gba')], check=True)
    R = lambda n: os.path.join('roms', n)
    D = ['-d', DATA, '--no-ini', '-q']
    pnes_len = len(read(os.path.join(DATA, 'pocketnes.gba')))
    pcea_len = len(read(os.path.join(DATA, 'pceadvance.gba')))
    fca_len = len(read(os.path.join(DATA, 'fca.gba')))

    print('PocketNES image')
    t.run('fcabuild', *D, R('Red Test (U) [!].nes'), R('Green Test (E).nes'),
          R('Blue Trainer.nes'), '--clean-titles', '-o', 'pnes.gba')
    img = read('pnes.gba')
    check(img[:pnes_len] == read(os.path.join(DATA, 'pocketnes.gba')), 'emulator copied unchanged')
    ents, end = entries_pnes(img, pnes_len)
    check([e[0] for e in ents] == ['Red Test', 'Green Test', 'Blue Trainer'], 'titles cleaned: %r' % [e[0] for e in ents])
    check(ents[0][2] == 0 and ents[1][2] == 4, 'PAL flag only for the (E) game')
    green = img[ents[1][5] + 48:ents[1][5] + 48 + ents[1][1]]
    check(green[:4] == b'NES\x1a' and green[7] == 0, 'DiskDude junk in byte 7 cleared')
    check(green[16:] == read(R('Green Test (E).nes'))[16:], 'ROM data unchanged')
    blue = img[ents[2][5] + 48:ents[2][5] + 48 + ents[2][1]]
    check(blue == read(R('Blue Plain.nes')), 'trainer removed for PocketNES')
    check(u32(img, end) == 0x41700417 and len(img) == end + 44, 'NES2FCA end record')
    check(all(e[1] % 4 == 0 for e in ents), 'entries word aligned')

    print('Flags, list files and NES2FCA.ini')
    # Old NES2FCA lists hold absolute Windows paths; fcabuild then looks for
    # the file next to the list.
    with open(R('games.lst'), 'w', encoding='utf-8') as f:
        f.write('### NES2FCA NES List ###\n')
        f.write('Red Test (U) [!].nes|3|7|0|Custom Title\n')
        f.write('# comment\n')
        f.write('C:\\old\\windows\\path\\Blue Plain.nes\n')
    with open('NES2FCA.ini', 'w', encoding='utf-8') as f:
        f.write('[PocketNES]\nBlue Plain.nes=32|5|0|From Ini\n')
    t.run('fcabuild', '-d', DATA, '-q', '-l', R('games.lst'), '--ini', 'NES2FCA.ini', '-o', 'list.gba')
    ents, _ = entries_pnes(read('list.gba'), pnes_len)
    check([(e[0], e[2], e[3]) for e in ents] == [('Custom Title', 3, 7), ('From Ini', 32, 5)],
          'list file fields and ini settings: %r' % [(e[0], e[2], e[3]) for e in ents])
    t.run('fcabuild', *D, R('Red Test (U) [!].nes'), '--pal', '--ppu-hack', '--follow', '9',
          '--number', '-o', 'flags.gba')
    ents, _ = entries_pnes(read('flags.gba'), pnes_len)
    check((ents[0][0], ents[0][2], ents[0][3]) == ('01 Red Test (U) [!]', 5, 9), 'named flags: %r' % (ents[0],))
    t.run('fcabuild', *D, '-l', R('games.lst'), '--save-list', 'saved.lst', '-n')
    saved = read('saved.lst').decode('utf-8')
    check('|3|7|0|Custom Title' in saved, 'save-list keeps settings')
    code, out = t.run('fcabuild', *D, R('not-a-rom.nes'), R('Red Test (U) [!].nes'), '-o', 'skip.gba', ok=False)
    check(code == 1 and 'not an NES file' in out and len(entries_pnes(read('skip.gba'), pnes_len)[0]) == 1,
          'non-ROM files are skipped with a warning')

    print('PCEAdvance image')
    t.run('fcabuild', *D, R('PCE Red (J).pce'), R('PCE Green (J).pce'), R('PCE Blue (U).pce'), '-o', 'pce.gba')
    img = read('pce.gba')
    ents, end = entries_pnes(img, pcea_len)
    check(len(ents) == 3, 'three PCE entries')
    for e in ents:
        check(img[e[5] + 48:e[5] + 64] == b'NES\x1a@           ', 'PCE identifier for ' + e[0])
        check(e[1] == 0x8000 + 16, 'PCE size field for ' + e[0])
    check(img[ents[1][5] + 64:ents[1][5] + 64 + 0x8000] == read(R('PCE Green (J).pce'))[512:],
          'copier header stripped')
    check(img[ents[2][5] + 64:ents[2][5] + 64 + 0x8000] == read(R('PCE Blue Plain.pce')),
          'US HuCard unscrambled')
    check(ents[2][2] == 4 and ents[0][2] == 0, 'US flag for (U) game only')
    t.run('fcabuild', *D, R('PCE Blue (U).pce'), '--pce-unscramble', 'no', '-o', 'pce2.gba')
    img2 = read('pce2.gba')
    check(img2[pcea_len + 64:pcea_len + 64 + 0x8000] == read(R('PCE Blue (U).pce')), '--pce-unscramble no')

    print('GBonGBA image')
    t.run('fcabuild', *D, R('GB Black.gb'), R('GB White.gb'), '-o', 'gb.gba')
    img = read('gb.gba')
    gb_len = len(read(os.path.join(DATA, 'gbongba-0.4.gba')))
    check(img[gb_len:gb_len + 65536] == read(R('GB Black.gb')) + read(R('GB White.gb')), 'GB ROMs appended')
    check(gba_header_ok(img) and img[4:8] == bytes([0x24, 0xFF, 0xAE, 0x51]), 'invalid header fixed automatically')

    print('FamicomAdvance images')
    t.run('fcabuild', *D, '-t', 'fca', '--no-fix-header', R('Red Test (U) [!].nes'),
          R('Blue Plain.nes'), R('green.sav'), '-o', 'fca.gba')
    t.run('fca-mkfs', '-b', os.path.join(DATA, 'fca.gba'), 'mkfs.gba',
          R('Red Test (U) [!].nes'), R('Blue Plain.nes'), R('green.sav'))
    check(read('fca.gba') == read('mkfs.gba'), 'fcabuild -t fca matches fca-mkfs')
    ents, end = entries_fca(read('fca.gba'), fca_len)
    check([(e[0], e[1]) for e in ents] == [(b'Red Test (U) [!]', 'nes'), (b'Blue Plain', 'nes'), (b'green', 'sav')],
          'FCA directory: %r' % [(e[0], e[1]) for e in ents])
    jp = os.path.join('roms', 'ドラゴンクエスト ｶﾞﾝﾀﾞﾑ.nes')
    shutil.copy(R('Red Test (U) [!].nes'), jp)
    t.run('fcabuild', *D, '-t', 'fca', jp, '-o', 'jp.gba')
    img = read('jp.gba')
    ents, _ = entries_fca(img, fca_len)
    check(ents[0][0] == 'ドラゴンクエスト ガンダム'.encode('euc_jp'), 'UTF-8 title to EUC-JP: %r' % ents[0][0])
    check(gba_header_ok(img) and img[0xAC:0xB2] == b'FCA\0KK', 'NES2FCA header defaults')
    t.run('fcabuild', *D, '-t', 'fca-v01', R('Red Test (U) [!].nes'), '-o', 'v01.gba')
    ents, _ = entries_fca(read('v01.gba'), len(read(os.path.join(DATA, 'shell.bin'))))
    check([e[0] for e in ents][:3] == [b'emu', b'emuslow', b'font'] and ents[-1][0] == b'Red Test (U) [!]',
          'fca-v01 system files')

    print('Cart size and splitting')
    roms = [R('Red Test (U) [!].nes'), R('Green Test (E).nes'), R('Blue Plain.nes')]
    code, out = t.run('fcabuild', *D, *roms, '--cart-size', '1m', '-o', 'big.gba', ok=False)
    check(code != 0 and 'cart size' in out, 'over-size image refused')
    t.run('fcabuild', *D, *roms, '--cart-size', '1m', '--split', '-o', 'split.gba')
    parts = [p for p in ('split-1.gba', 'split-2.gba', 'split-3.gba') if os.path.exists(p)]
    check(len(parts) == 2 and all(len(read(p)) <= 131072 for p in parts), 'split into 1 Mbit parts: %r' % parts)
    t.run('fcabuild', *D, roms[0], '--pad-to', '512k', '-o', 'pad.gba')
    check(len(read('pad.gba')) == 524288 and read('pad.gba').endswith(b'\xff' * 16), '--pad-to')

    print('Splash screens')
    splash = os.path.join(DATA, 'splash9.raw')
    t.run('gbaraw', splash, 'splash.bmp')
    t.run('gbaraw', 'splash.bmp', 'splash.raw')
    a, b = read(splash), read('splash.raw')
    check(all((a[i] | a[i + 1] << 8) & 0x7FFF == (b[i] | b[i + 1] << 8) for i in range(0, len(a), 2)),
          'raw -> bmp -> raw is lossless')
    t.run('fcabuild', *D, roms[0], '-s', 'splash.bmp', '-o', 'sp1.gba')
    t.run('fcabuild', *D, roms[0], '-s', 'splash.raw', '-o', 'sp2.gba')
    check(read('sp1.gba') == read('sp2.gba') and read('sp1.gba')[pnes_len:pnes_len + 76800] == b,
          'splash from BMP and RAW')

    print('fcasvedt: FamicomAdvance saves')
    t.run('fcasvedt', 'fca.sav', 'create', 'fca')
    d = read('fca.sav')
    check(len(d) == 65536 and u32(d, 0) == 0xA838861A, 'new FCA save')
    t.run('fcasvedt', 'fca.sav', 'add', R('green.sav'), '-t', 'ドラゴンクエスト')
    t.run('fcasvedt', 'fca.sav', 'add', R('green.sav'), '-t', 'Second')
    _, out = t.run('fcasvedt', 'fca.sav', 'list')
    check('ドラゴンクエスト.sav' in out and 'Second.sav' in out and '2 of 7' in out, 'FCA list')
    d = read('fca.sav')
    x = 0
    for i in range(0x47):
        x ^= u32(d, i * 4)
    check(u32(d, 0x11C) == x, 'FCA directory check word')
    check(d[0x2000:0x4000] == read(R('green.sav')), 'slot 1 data at 0x2000')
    t.run('fcasvedt', 'fca.sav', 'extract', '1', '-o', 'out1.sav')
    check(read('out1.sav') == read(R('green.sav')), 'FCA extract')
    t.run('fcasvedt', 'fca.sav', 'rename', '2', 'Renamed')
    t.run('fcasvedt', 'fca.sav', 'delete', '1')
    _, out = t.run('fcasvedt', 'fca.sav')
    check('Renamed.sav' in out and '1 of 7' in out, 'FCA rename/delete')

    print('fcasvedt: PocketNES saves')
    t.run('fcasvedt', 'pn.sav', 'create', 'pocketnes')
    t.run('fcasvedt', 'pn.sav', 'add', R('green.sav'), '--rom', R('Battery Test.nes'))
    t.run('fcasvedt', 'pn.sav', 'add', R('green.sav'), '--rom', R('Red Test (U) [!].nes'), '-t', 'Other')
    sram2 = bytearray(read(R('green.sav')))
    sram2[0] = 0x16
    with open('red.sav', 'wb') as f:
        f.write(sram2)
    t.run('fcasvedt', 'pn.sav', 'add', 'red.sav', '--rom', R('Battery Test.nes'), '-t', 'Battery Test')
    _, out = t.run('fcasvedt', 'pn.sav')
    check(out.count('SRAM') == 2 and 'Battery Test' in out and 'Other' in out, 'one SRAM save per game')
    d = read('pn.sav')
    check(u32(d, 0) == 0x57A731D7, 'PocketNES state id')
    # checksum: sum of 128 words, 128 bytes apart, after the iNES header
    rom = read(R('Battery Test.nes'))[16:]
    want = sum(struct.unpack_from('<I', rom, i * 128)[0] for i in range(128)) & 0xFFFFFFFF
    check(u32(d, 4 + 12) == want, 'ROM checksum')
    t.run('fcasvedt', 'pn.sav', 'extract', '1', '-o', 'x.sav')
    check(read('x.sav') == bytes(sram2), 'PocketNES extract (LZO round trip)')
    t.run('fcasvedt', 'pn.sav', 'delete', '2')
    _, out = t.run('fcasvedt', 'pn.sav')
    check('Other' not in out and '1 record' in out, 'PocketNES delete')
    with open('junk.sav', 'wb') as f:
        f.write(b'\x55' * 65536)
    code, out = t.run('fcasvedt', 'junk.sav', ok=False)
    check(code != 0 and 'not a FamicomAdvance or PocketNES' in out, 'rejects unknown files')

    print()
    print('%d checks passed, %d failed' % (PASSED, len(FAILED)))
    shutil.rmtree(work, ignore_errors=True)
    sys.exit(1 if FAILED else 0)


if __name__ == '__main__':
    main()
