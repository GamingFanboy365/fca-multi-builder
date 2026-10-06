#!/usr/bin/env python3
"""Boot-test the images in mGBA: build compilations with fcabuild, start
each game from the emulator's own menu and check the screen colour.

    emu_tests.py BIN_DIR GBARUN

GBARUN is tests/gbarun.c built against libmgba (make emu-test does this).
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, 'original')  # the files from the old package

# Screen colours of the test programs as mGBA shows them.
NES_RED, NES_GREEN, NES_BLUE, NES_GREY = 'EF5A00', '84EF7B', '526BF7', 'A5A5A5'
PCE_RED, PCE_GREEN, PCE_BLUE = 'FF0000', '00FF00', '0000FF'
FCA_RED = 'DE2900'

failed = 0


def main():
    global failed
    bindir, gbarun = (os.path.abspath(p) for p in sys.argv[1:3])
    work = tempfile.mkdtemp(prefix='fca-emu-')
    os.chdir(work)
    subprocess.run([sys.executable, os.path.join(ROOT, 'tests', 'make-test-roms.py'), 'roms',
                    os.path.join(DATA, 'gbongba-0.4.gba')], check=True)

    def tool(name, *args):
        subprocess.run([os.path.join(bindir, name), *args], check=True,
                       stdout=subprocess.DEVNULL)

    def build(out, *args):
        tool('fcabuild', '-q', '-d', DATA, '--no-ini', '--pad-to', '512k', *args, '-o', out)

    def screen(img, frames, *keys, env=None, flat=38400):
        r = subprocess.run([gbarun, img, str(frames), *keys], check=True, stdout=subprocess.PIPE,
                           env=dict(os.environ, **(env or {})))
        top, count = r.stdout.decode().split()[1:3]
        # a flat screen in one colour; menus and error messages aren't
        return top if int(count) >= flat else 'mixed:' + top

    def expect(what, got, want):
        global failed
        ok = got == want
        failed += not ok
        print('  %-58s %s%s' % (what, 'ok' if ok else 'FAIL', '' if ok else ' (%s, wanted %s)' % (got, want)))

    R = lambda n: os.path.join('roms', n)
    pick = ['100:A', '100:D 160:A', '100:D 160:D 220:A']

    print('PocketNES')
    build('pnes.gba', R('Red Test (U) [!].nes'), R('Green Test (E).nes'), R('Blue Trainer.nes'))
    for n, want in enumerate((NES_RED, NES_GREEN, NES_BLUE)):
        expect('game %d from the menu' % (n + 1), screen('pnes.gba', 400, *pick[n].split()), want)
    build('pnes-splash.gba', R('Red Test (U) [!].nes'), R('Blue Plain.nes'),
          '-s', os.path.join(DATA, 'splash9.raw'))
    expect('splash screen first', screen('pnes-splash.gba', 30, flat=20000), '000000')
    expect('game 2 after the splash', screen('pnes-splash.gba', 600, '250:D', '310:A'), NES_BLUE)

    print('PCEAdvance')
    build('pce.gba', R('PCE Red (J).pce'), R('PCE Green (J).pce'), R('PCE Blue (U).pce'))
    for n, want in enumerate((PCE_RED, PCE_GREEN, PCE_BLUE)):
        expect('game %d from the menu' % (n + 1), screen('pce.gba', 400, *pick[n].split()), want)
    build('pce-scrambled.gba', R('PCE Blue (U).pce'), '--pce-unscramble', 'no')
    expect('scrambled US dump gives a black screen', screen('pce-scrambled.gba', 300), '000000')

    print('GBonGBA')
    build('gb.gba', R('GB Black.gb'), R('GB White.gb'))
    expect('game 1', screen('gb.gba', 600, '160:A', '260:A'), '000000')
    # the Game Boy screen (160x144) sits in a black border
    expect('game 2', screen('gb.gba', 600, '160:D', '220:A', '320:A', flat=160 * 144), 'FFFFFF')

    print('FamicomAdvance')
    fca_keys = ['200:A', '260:D', '320:A', '400:D', '460:A', '540:A']
    for target in ('fca-v02', 'fca-v01'):
        build(target + '.gba', '-t', target, R('Red Test (U) [!].nes'), R('Blue Plain.nes'))
        expect(target + ': format SRAM, pick game 1',
               screen(target + '.gba', 800, *fca_keys, env={'SRAM64': '1'}), FCA_RED)

    print('PocketNES saves made by fcasvedt')
    build('bt.gba', R('Battery Test.nes'), R('Red Test (U) [!].nes'))
    tool('fcasvedt', 'good.sav', 'create', 'pocketnes')
    tool('fcasvedt', 'good.sav', 'add', R('green.sav'), '--rom', R('Battery Test.nes'))
    tool('fcasvedt', 'bad.sav', 'create', 'pocketnes')
    tool('fcasvedt', 'bad.sav', 'add', R('green.sav'), '--checksum', 'DEADBEEF')
    expect('game reads the SRAM from the save',
           screen('bt.gba', 300, '100:A', env={'SRAM64': '1', 'SAVE_IN': 'good.sav'}), NES_GREEN)
    expect('save for another game is ignored',
           screen('bt.gba', 300, '100:A', env={'SRAM64': '1', 'SAVE_IN': 'bad.sav'}), NES_GREY)

    shutil.rmtree(work, ignore_errors=True)
    print('\n%s' % ('all emulator checks passed' if not failed else '%d emulator checks FAILED' % failed))
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
