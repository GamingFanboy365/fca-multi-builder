#!/usr/bin/env python3
"""Generate tiny, freely distributable test ROMs for the test suite.

Each program fills the screen with one colour, so the emulator tests can
tell which game started.  Usage: make-test-roms.py OUTDIR [GBONGBA.gba]
(the Game Boy logo the GBonGBA scanner looks for is copied from the
emulator file when given, so this script contains no Nintendo data).
"""
import os
import struct
import sys


def nes(color, prg_banks=1, junk7=False, trainer=False, battery=False):
    prg = bytearray(b'\xff' * (16384 * prg_banks))
    code = bytes([
        0x78, 0xD8, 0xA2, 0x40, 0x8E, 0x17, 0x40, 0xA2, 0xFF, 0x9A, 0xE8,
        0x8E, 0x00, 0x20, 0x8E, 0x01, 0x20, 0x8E, 0x10, 0x40,
        0x2C, 0x02, 0x20, 0x10, 0xFB,                    # wait for vblank
        0x2C, 0x02, 0x20, 0x10, 0xFB,                    # twice
        0xA9, 0x3F, 0x8D, 0x06, 0x20, 0xA9, 0x00, 0x8D, 0x06, 0x20,
    ] + ([0xAD, 0x00, 0x60] if battery else [0xA9, color]) + [  # colour
        0x8D, 0x07, 0x20,
        0xA9, 0x00, 0x8D, 0x06, 0x20, 0x8D, 0x06, 0x20, 0x8D, 0x05, 0x20, 0x8D, 0x05, 0x20,
        0xA9, 0x0A, 0x8D, 0x01, 0x20,                    # background on
        0x4C, 0, 0])                                     # loop
    off = len(prg) - 16384
    prg[off:off + len(code)] = code
    prg[off + len(code) - 2:off + len(code)] = struct.pack('<H', 0xC000 + len(code) - 3)
    prg[off + 0x100] = 0x40                              # rti
    # PocketNES identifies games by sampling 4 bytes every 128; make sure
    # the test ROMs differ there.
    prg[off + 0x200] = color ^ (0x80 if battery else 0) ^ (prg_banks << 4)
    prg[-6:] = struct.pack('<HHH', 0xC100, 0xC000, 0xC100)
    if prg_banks > 1:                                    # mirror for 32 KB
        for b in range(prg_banks - 1):
            prg[b * 16384:(b + 1) * 16384] = prg[off:off + 16384]
    flags6 = (0x04 if trainer else 0) | (0x02 if battery else 0)
    hdr = bytearray(b'NES\x1a' + bytes([prg_banks, 1, flags6, 0]) + bytes(8))
    if junk7:
        hdr[7:16] = b'DiskDude!'
    return bytes(hdr) + (bytes(512) if trainer else b'') + bytes(prg) + bytes(8192)


def pce(grb, size=0x8000):
    rom = bytearray(b'\xff' * size)
    lo, hi = grb & 0xFF, (grb >> 8) & 1
    code = bytes([0x78, 0xD4, 0xD8, 0xA9, 0xFF, 0x53, 0x01, 0xA9, 0xF8, 0x53, 0x02,
                  0xA2, 0xFF, 0x9A, 0x9C, 0x00, 0x04, 0x9C, 0x02, 0x04, 0x9C, 0x03, 0x04,
                  0xA9, lo, 0x8D, 0x04, 0x04, 0xA9, hi, 0x8D, 0x05, 0x04,
                  0x03, 0x05, 0x13, 0x80, 0x23, 0x00, 0x80, 0xFE])
    rom[:len(code)] = code
    rom[0x100] = 0x40
    rom[0x1FF6:0x2000] = struct.pack('<5H', 0xE100, 0xE100, 0xE100, 0xE100, 0xE000)
    return bytes(rom)


def bitrev(b):
    return int('{:08b}'.format(b)[::-1], 2)


def gb(bgp, title, logo):
    rom = bytearray(32768)
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])
    rom[0x104:0x134] = logo
    t = title.encode()[:15]
    rom[0x134:0x134 + len(t)] = t
    rom[0x150:0x157] = bytes([0x3E, bgp, 0xE0, 0x47, 0x18, 0xFE, 0])
    x = 0
    for i in range(0x134, 0x14D):
        x = (x - rom[i] - 1) & 0xFF
    rom[0x14D] = x
    s = sum(rom) - rom[0x14E] - rom[0x14F]                 # global checksum
    rom[0x14E], rom[0x14F] = (s >> 8) & 0xFF, s & 0xFF
    return bytes(rom)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    logo = bytes([0xCE, 0xED]) + bytes(46)
    if len(sys.argv) > 2:
        data = open(sys.argv[2], 'rb').read()
        i = data.find(bytes([0xCE, 0xED, 0x66, 0x66]))
        if i >= 0:
            logo = data[i:i + 48]

    def w(name, data):
        with open(os.path.join(out, name), 'wb') as f:
            f.write(data)

    w('Red Test (U) [!].nes', nes(0x16))
    w('Green Test (E).nes', nes(0x2A, prg_banks=2, junk7=True))
    w('Blue Trainer.nes', nes(0x12, trainer=True))
    w('Blue Plain.nes', nes(0x12))
    w('Battery Test.nes', nes(0, prg_banks=2, battery=True))
    # PC Engine colours are GRB333
    w('PCE Red (J).pce', pce(0b000111000))
    w('PCE Green (J).pce', bytes(512) + pce(0b111000000))           # copier header
    w('PCE Blue (U).pce', bytes(bitrev(b) for b in pce(0b000000111)))  # US dump
    w('PCE Blue Plain.pce', pce(0b000000111))
    w('GB Black.gb', gb(0xFF, 'BLACK', logo))
    w('GB White.gb', gb(0x00, 'WHITE', logo))
    sram = bytearray(8192)
    sram[0] = 0x2A
    w('green.sav', bytes(sram))
    w('not-a-rom.nes', b'hello world, not a rom' * 100)


if __name__ == '__main__':
    main()
