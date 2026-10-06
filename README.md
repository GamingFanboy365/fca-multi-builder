# fca-multi-builder

Modern cross-platform ROM builder toolchain for FamicomAdvance (FCA), PocketNES, PCEAdvance and other classic GBA emulators.

The original 2001–2003 Windows tools in this package (NES2FCA, fcasvedt, the PocketNES/PCEAdvance ROM Builder and `fca-mkfs.c`) no longer run on modern systems. Their behaviour has been rewritten as four small command-line programs that run natively on Linux and on 32-bit and 64-bit Windows. They produce the same image layouts as the originals, and the tests boot the results in a GBA emulator.

| Tool | Replaces | What it does |
|------|----------|--------------|
| `fcabuild` | NES2FCA 0.92, PocketNES/PCEAdvance ROM Builder | Builds a GBA image from an emulator and a set of NES, PC Engine or Game Boy ROMs |
| `fcasvedt` | fcasvedt 0.31/0.5, ROM Builder SRAM manager | Lists, extracts, adds, renames and deletes saves in FamicomAdvance and PocketNES save files |
| `fca-mkfs` | `fca-mkfs.c` | The original FamicomAdvance file-system tool, fixed for 64-bit systems and Windows |
| `gbaraw` | the ROM Builder's splash converter | Converts splash screens between BMP and the raw 240×160 GBA format |

## Getting the tools

Ready-to-use zips (one each for Linux x86-64, Windows 32-bit and Windows 64-bit) are built by the GitHub Actions workflow on every push. They appear as artifacts of the "build" workflow run, and as a release for tags named `v*`. Each zip holds the four programs plus the emulator files and `nes2fca.cfg`, so `fcabuild` works straight out of the folder. The Windows programs are self-contained `.exe` files that need no extra DLLs. They only use functions Windows has had since XP, but they have only been tested on current Windows and under Wine.

To build from source you need a C99 compiler and `make`. Cross-compiling for Windows needs MinGW-w64 (`apt install mingw-w64` on Debian/Ubuntu).

```sh
make                 # native tools in build/native/
make win32 win64     # Windows tools in build/win32/ and build/win64/
make test            # test suite (needs Python 3)
make emu-test        # also boot the images in mGBA (needs libmgba-dev)
make dist            # release zips in dist/
```

## Building a compilation with fcabuild

`fcabuild` takes ROM files and writes a `.gba` image. It picks the emulator from the file extensions (`.nes` for PocketNES, `.pce` for PCEAdvance, `.gb`/`.gbc` for GBonGBA), or you can choose one with `-t`. It looks for the emulator files next to the program, then in the current directory; `-d DIR` points it elsewhere.

```sh
fcabuild *.nes -o nes-games.gba
fcabuild -t fca "ドラゴンクエスト.nes" "Final Fantasy III (J).nes" -o fca.gba
fcabuild *.pce --clean-titles -s splash9.raw -o pce-games.gba
fcabuild -l my-old-nes2fca-list.lst --cart-size 32m --split -o cart.gba
```

The targets come from `nes2fca.cfg`, just as in NES2FCA. `fcabuild --list-targets` shows them.

| Target | Aliases | Emulator file | Games |
|--------|---------|---------------|-------|
| `fca-v02` | `fca` | `fca.gba` (FamicomAdvance 0.2) | `.nes`, `.sav` |
| `fca-v01` | `fca1` | `shell.bin` + system files (FamicomAdvance 0.1) | `.nes`, `.sav` |
| `PocketNES` | `pnes` | `pocketnes.gba` (PocketNES 9.8) | `.nes` |
| `PCEAdvance` | `pce` | `pceadvance.gba` (PCEAdvance 4.8) | `.pce` |
| `GBonGBA` | `gb` | `gbongba-0.4.gba` | `.gb`, `.gbc` |
| `Easymb2gba` | | `Easymb2gba.gba` | as configured in NES2FCA |

Per-game options live in the emulators' ROM headers. They can be set for all games on the command line, or per game in a list file or in an `NES2FCA.ini` carried over from NES2FCA.

| Option | PocketNES | PCEAdvance |
|--------|-----------|------------|
| `--ppu-hack` / `--cpu-50` | PPU speed hack | run the CPU at 50 % |
| `--no-cpu-hack` | disable the CPU speed hack | disable the CPU speed hack |
| `--pal` / `--us-rom` | PAL timing | US (TurboGrafx-16) game |
| `--follow-memory`, `--follow N` | Unscaled (Auto) follows an address instead of a sprite | same |
| `--flags N` | raw flag value | raw flag value |

By default, NES games tagged (E) get PAL timing and PC Engine games tagged (U) or (USA) get the US flag; `--no-auto-flags` turns that off. A list file holds one game per line, either as a path or as `path|flags|follow|reserved|title`. Lists saved by NES2FCA load directly: absolute Windows paths that don't exist are looked up next to the list. `--save-list` writes the final list back out.

Titles default to the file names. `--clean-titles` drops GoodTools tags such as `(U) [!]`, and `--underscores`, `--title-case`, `--number` and `--mark-multiboot` tidy them further, as the old ROM Builder did. FamicomAdvance titles may be Japanese: they're converted to EUC-JP for its font, including half-width katakana with voicing marks (`ｶﾞﾝﾀﾞﾑ` becomes `ガンダム`). PocketNES and PCEAdvance only show ASCII, so accented letters are folded to their plain form.

`--cart-size` (for example `32m` for a 32 Mbit cart, or `4mb`) makes `fcabuild` refuse an image that won't fit, and `--split` instead spreads the games over `NAME-1.gba`, `NAME-2.gba` and so on. FamicomAdvance and GBonGBA don't carry the boot logo a real GBA checks, so `fcabuild` repairs the header automatically when an image needs it, as NES2FCA's "Fix GBA header" box did (`--fix-header`, `--no-fix-header`, `--gba-title`).

`fcabuild` also handles three problems the old tools didn't. US TurboGrafx-16 HuCards are wired with their data lines reversed, so many US dumps are "scrambled" and show only a black screen in PCEAdvance. `fcabuild` detects these from the reset vector and unscrambles them (what PCEToy used to do; `--pce-unscramble yes|no|auto`). PocketNES 9.8 can't skip a 512-byte trainer, so trainers are removed for that target. And mGBA runs PCEAdvance images of 256 KB or less in a way that stops the third and later games from starting; real carts are fine, and `--pad-to 512k` avoids it.

## Editing saves with fcasvedt

`fcasvedt` works on the 64 KB save file of a FamicomAdvance or PocketNES cart (an SRAM dump, or an emulator's `.sav`). It tells the two apart by itself.

```sh
fcasvedt cart.sav                          # list the saves
fcasvedt cart.sav extract 2                # write save 2 to a file
fcasvedt cart.sav extract-all -o saves/
fcasvedt cart.sav add zelda.sav --rom "Legend of Zelda.nes"   # PocketNES
fcasvedt cart.sav add dq3.sav -t "ドラゴンクエスト3"            # FamicomAdvance
fcasvedt cart.sav rename 1 "New title"
fcasvedt cart.sav delete 3
fcasvedt new.sav create pocketnes          # or: create fca
```

FamicomAdvance has seven 8 KB slots and finds a game's save by its title, so a save must carry the same title the game has in the image. PocketNES keeps compressed SRAM saves and save states and ties each to its game through a checksum of the ROM. That's why adding a PocketNES save needs the `.nes` file (or `--checksum`). For the game that last ran, PocketNES keeps the newest SRAM in a separate live area; `extract` uses it automatically, and `--stored` picks the stored copy instead.

## The other tools

`fca-mkfs` keeps the command line of the original: `fca-mkfs -b fca.gba out.gba game1.nes game2.nes`. The original source stored its header fields as `long`, which is 8 bytes on 64-bit Linux and produces broken images. This version writes 32-bit fields and produces exactly the bytes the original did on a 32-bit system.

`gbaraw splash.raw splash.bmp` turns a splash screen into a picture you can view or edit, and `gbaraw picture.bmp splash.raw` goes the other way. Pictures that aren't 240×160 are centred, or shrunk to fit with `--scale`. `fcabuild -s` accepts either format directly.

## Testing

`make test` builds compilations for every target from small generated test ROMs and checks them byte by byte. It also checks the list file, `NES2FCA.ini`, splitting and splash handling, and edits both kinds of save file. The suite runs unchanged against the Windows builds, natively or under Wine (`python3 tests/run_tests.py build/win64 --runner wine`).

`make emu-test` goes further. It boots each image in mGBA (through libmgba), starts the games from the emulator's own menus and checks what appears on screen. It also loads saves written by `fcasvedt` into PocketNES and checks that the game sees them.

The exact image and save layouts are documented in [docs/FORMATS.md](docs/FORMATS.md).

## The original files

The files from the original `PCE48FC98GBA.zip` package are kept unchanged in the top folder (the zip itself is in `zip/`). The emulator files `fcabuild` uses at run time are `pocketnes.gba`, `pceadvance.gba`, `fca.gba`, `shell.bin` with its system files (`emu.bin`, `emuslow.bin`, `font.dat`, `mapper*.bin`), `gbongba-0.4.gba` and `Easymb2gba.gba`. `SMS.GBA` (DrSMS 3.00 beta 2) came with the package but none of the original tools supported it. The Windows programs `NES2FCA.exe`, `fcasvedt.exe` and `PCEAdvance_ROM_BUILDER.EXE` remain for reference. `splash9.raw`, `splashlogo.raw` and `gameboyplayer.raw` are ready-made splash screens.

## Credits

FamicomAdvance and `fca-mkfs` are by KIKU. NES2FCA and fcasvedt are by studio KAZAMA. PocketNES is by Loopy and FluBBa, PCEAdvance by FluBBa, and the PocketNES ROM Builder by Hoe. GBonGBA, DrSMS (Reesy) and Easy MB2GBA are by their respective authors. The new tools are a clean reimplementation; the LZO1X code is an independent implementation of the format.
