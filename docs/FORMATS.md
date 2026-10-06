# File formats

This page records the formats the tools read and write. They were worked out by disassembling NES2FCA 0.92 and fcasvedt (studio KAZAMA), cross-checked against `fca-mkfs.c`, `FORMATS.TXT`, and the published PocketNES and PCEAdvance sources, and then confirmed by booting images built by `fcabuild` in mGBA (see `tests/emu_tests.py`). All integers are little-endian.

## Compilation images

Every image starts with the emulator file exactly as shipped. The ROMs follow it, in the layout the emulator expects. NES2FCA always finished an image with an FCA end-of-list record (the 32-bit value `0x41700417` followed by 40 zero bytes), whatever the emulator, and `fcabuild` does the same.

### FamicomAdvance file system

Used by `fca.gba` (FamicomAdvance 0.2), by `shell.bin` plus the system files (FamicomAdvance 0.1), and by NES2FCA's Easy MB2GBA entry. Each file is a 44-byte header followed by its data, padded with zeros to a multiple of 4 bytes.

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 4 | magic `0x04174170` |
| 4 | 32 | name without extension, NUL-terminated, EUC-JP |
| 36 | 4 | extension, at most 3 characters (`nes`, `sav`, `bin`, `dat`) |
| 40 | 4 | file length in bytes |

FamicomAdvance shows the name and extension in its game list, so names can use the kana, kanji and symbols in its font. NES2FCA limited titles to 21 characters and 31 bytes, and `fcabuild` does too. For FamicomAdvance 0.1 the image is `shell.bin` followed by the system files `emu.bin`, `emuslow.bin`, `font.dat` and `mapper0.bin` to `mapper4.bin` as file-system entries (the list comes from `nes2fca.cfg`), then the games. When an `.nes` file's header byte 7 has any of its low four bits set (usually leftover text such as "DiskDude!"), NES2FCA set the byte to zero so the mapper number reads correctly. `fcabuild` repeats this for FamicomAdvance and PocketNES.

### PocketNES

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 32 | title, NUL-terminated ASCII (31 characters at most) |
| 32 | 4 | size of the ROM data that follows |
| 36 | 4 | flags |
| 40 | 4 | "follow" value for the Unscaled (Auto) display mode |
| 44 | 4 | reserved (0) |
| 48 | size | the `.nes` file, iNES header included |

Flag bits: 0 enables the PPU speed hack, 1 disables the CPU speed hack, 2 selects PAL timing, and 5 makes "follow" a memory address instead of a sprite number. An optional 76800-byte splash screen may sit between the emulator and the first header; the emulator notices it because the iNES signature isn't where the first ROM would start. That version does not skip a 512-byte trainer, so `fcabuild` removes trainers for this target and clears iNES flag bit 2. PocketNES finds the next game at header + 48 + size, so sizes are rounded up to a multiple of 4.

### PCEAdvance

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 32 | title, NUL-terminated ASCII |
| 32 | 4 | HuCard size + 16 |
| 36 | 4 | flags |
| 40 | 4 | follow value |
| 44 | 4 | reserved (0) |
| 48 | 16 | `"NES"`, `0x1A`, `"@"`, then 11 spaces |
| 64 | | HuCard data |

PCEAdvance counts the 16 identifier bytes as part of the ROM, so it also finds the next game at header + 48 + size. NES2FCA removed a 512-byte copier header (the size modulo 8 KB) before storing the data. PCEAdvance can also skip such a header itself when bit 9 of the size is set, but `fcabuild` strips it the way NES2FCA did. Flag bits: 0 runs the CPU at 50 %, 1 disables the CPU speed hack, 2 marks a US (TurboGrafx-16) game, and 5 makes "follow" a memory address.

The US flag does not decode US HuCard dumps. TurboGrafx-16 cards have their data lines wired in reverse, so a straight dump of a US card has the bits of every byte reversed, and PCEAdvance shows a black screen for it. Such a dump can be recognised by its reset vector, the last two bytes of the first 8 KB bank: it must point into `$E000-$FFFF`, so its high byte is `$E0` or above. `fcabuild` reverses the bits of every byte when only the bit-reversed value is a valid vector (`--pce-unscramble`). This is the job the old PCEToy tool did.

In mGBA, PCEAdvance images of 256 KB or less behave as if they ran from RAM: only the first two games of the menu start, and the third and later entries come up blank once another game has run. The same image padded past 256 KB works, so this is about how the emulator loads small files, not about the format. Flash carts aren't affected, and `--pad-to 512k` avoids it in emulators.

### GBonGBA

The Game Boy ROMs are stored back to back after `gbongba-0.4.gba`, with no headers. GBonGBA scans memory for the Game Boy logo and lists the games by the titles in their cartridge headers.

## GBA header ("Fix GBA header")

`fca.gba`, `shell.bin` and `gbongba-0.4.gba` do not carry the boot logo a real GBA checks. NES2FCA's "Fix GBA header" option copied the logo to offsets 0x04-0x9F and rewrote 0xA0-0xBF as follows: the 12-byte title, game code `FCA` (with the fourth byte zero), maker code `KK`, 0x96 at offset 0xB2, zeros elsewhere, and the complement check byte at 0xBD, which is minus (the sum of bytes 0xA0-0xBC + 0x19). By default `fcabuild` does this only when the emulator's header is invalid, and keeps the emulator's own title and codes if it has them.

## Splash screens

A splash screen is 240 x 160 pixels, 2 bytes per pixel, row by row from the top left (76800 bytes in total). Each pixel is BGR555: red in bits 0-4, green in bits 5-9, blue in bits 10-14. `gbaraw` converts these files to and from BMP.

## nes2fca.cfg, NES2FCA.ini and list files

`nes2fca.cfg` lists the targets under `[list]` (`num`, `list0`, ...). Each target section has a `shell=` entry, an optional `type=` (`PNES`, `PCEA`, `GBONGBA` or `PGB`; anything else means the FamicomAdvance file system), and, for FamicomAdvance 0.1, `fileN=source,name,ext` system files.

NES2FCA kept per-game settings in `NES2FCA.ini`, keyed by file name. For FamicomAdvance the `[TITLE]` section maps a file name to its title. The `[PocketNES]`, `[PCEAdvance]` and `[GBonGBA]` sections map a file name to `flags|follow|reserved|title`. `fcabuild` reads this file (from its data directory, or from `--ini`) so settings made in NES2FCA carry over.

A NES2FCA list file (`.lst`) has the line `### NES2FCA NES List ###` and then one full path per line. `fcabuild` also accepts `path|flags|follow|reserved|title` lines, blank lines and `#` comments. If a path doesn't exist (for example, an absolute Windows path read on Linux), it looks for a file with the same name next to the list.

## FamicomAdvance save files

A 64 KB SRAM image.

| Offset | Size | Field |
|-------:|-----:|-------|
| 0x0000 | 4 | magic `0xA838861A` |
| 0x0004 | 7 x 40 | directory: magic (`0x04174170` used, `0x41700417` free), name[32] (EUC-JP), ext[4] (`sav`) |
| 0x011C | 4 | XOR of the 71 words at 0x0000-0x011B |
| 0x2000 x (n+1) | 0x2000 | data of slot n (0-6) |

FamicomAdvance matches a save to its game by name, so a save must have the same title as the game in the image. An image without a valid directory makes FamicomAdvance ask whether to format the SRAM.

## PocketNES save files

A 64 KB SRAM image (32 KB carts use the same layout with a 0x6000 limit).

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 4 | `0x57A731D7` (newer versions also use `0x57A731D8`) |
| 4 | | records, back to back |
| 0xE000 | 0x2000 | live SRAM of the game named in the config record |

Each record is a 48-byte header followed by LZO1X-compressed data, padded to a multiple of 4 bytes.

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 2 | record size, header included |
| 2 | 2 | type: 0 save state, 1 SRAM, 2 config |
| 4 | 4 | data size (9.8 writes the compressed length, later versions the uncompressed one; neither reads it back) |
| 8 | 4 | frame count (play time) |
| 12 | 4 | ROM checksum |
| 16 | 32 | title |

A record size of 0 ends the list, followed by the words 0 and `0xFFFFFFFF`, then zeros up to 0xE000. The config record (type 2) holds display settings and, at offset 8, the checksum of the game whose SRAM is currently live at 0xE000. When that game is started, PocketNES uses the live copy. Otherwise it unpacks the game's SRAM record into the live area. The ROM checksum is the sum of 128 little-endian words, 128 bytes apart, starting right after the iNES header of the ROM as stored in the image.
