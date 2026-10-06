# fca-multi-builder tools
#
#   make                  native build (Linux, or whatever cc targets) -> build/native
#   make win32 / win64    Windows .exe files via MinGW-w64           -> build/win32, build/win64
#   make all-platforms    all three
#   make test             run the test suite against the native build
#   make emu-test         also boot the images in mGBA (needs libmgba-dev)
#   make dist             release zips in dist/ (static binaries + emulator files)

TOOLS   := fcabuild fcasvedt fca-mkfs gbaraw
VERSION := $(shell sed -n 's/.*FCA_TOOLS_VERSION "\(.*\)"/\1/p' src/common.h)

CC      ?= cc
CFLAGS  ?= -O2
WARN    := -std=c99 -Wall -Wextra -Wpedantic -Wshadow
DEFS    := -D_POSIX_C_SOURCE=200809L

MINGW32 ?= i686-w64-mingw32-gcc
MINGW64 ?= x86_64-w64-mingw32-gcc
# MinGW's own printf (C99 %zu etc. on old msvcrt), fully static so the
# .exe files run on a bare Windows install, Windows XP and later.
WINFLAGS := -O2 -D__USE_MINGW_ANSI_STDIO=1 -D_WIN32_WINNT=0x0501 -static -static-libgcc -s
WINLIBS  := -lshell32

fcabuild_SRC := common.c text.c ini.c gba.c image.c fcabuild.c
fcasvedt_SRC := common.c text.c lzo.c fcasvedt.c
fca-mkfs_SRC := common.c text.c fca-mkfs.c
gbaraw_SRC   := common.c image.c gbaraw.c

HEADERS := $(wildcard src/*.h)

.PHONY: all native win32 win64 all-platforms test emu-test dist clean

all: native

native: $(addprefix build/native/,$(TOOLS))
win32:  $(addprefix build/win32/,$(addsuffix .exe,$(TOOLS)))
win64:  $(addprefix build/win64/,$(addsuffix .exe,$(TOOLS)))
all-platforms: native win32 win64

define tool_rules
build/native/$(1): $$(addprefix src/,$$($(1)_SRC)) $$(HEADERS)
	@mkdir -p $$(@D)
	$$(CC) $$(WARN) $$(DEFS) $$(CFLAGS) $$(addprefix src/,$$($(1)_SRC)) -o $$@ $$(LDFLAGS)

build/win32/$(1).exe: $$(addprefix src/,$$($(1)_SRC)) $$(HEADERS)
	@mkdir -p $$(@D)
	$$(MINGW32) $$(WARN) $$(WINFLAGS) $$(addprefix src/,$$($(1)_SRC)) -o $$@ $$(WINLIBS)

build/win64/$(1).exe: $$(addprefix src/,$$($(1)_SRC)) $$(HEADERS)
	@mkdir -p $$(@D)
	$$(MINGW64) $$(WARN) $$(WINFLAGS) $$(addprefix src/,$$($(1)_SRC)) -o $$@ $$(WINLIBS)
endef
$(foreach t,$(TOOLS),$(eval $(call tool_rules,$(t))))

test: native
	python3 tests/run_tests.py build/native

build/gbarun: tests/gbarun.c src/image.c src/common.c $(HEADERS)
	$(CC) $(CFLAGS) -Isrc tests/gbarun.c src/image.c src/common.c -o $@ -lmgba

emu-test: native build/gbarun
	python3 tests/emu_tests.py build/native build/gbarun

# Files the tools need at run time (emulators, nes2fca.cfg) and the docs,
# copied next to the binaries so each zip works out of the box.
DATA := $(addprefix original/,nes2fca.cfg fca.gba shell.bin emu.bin emuslow.bin font.dat \
        mapper0.bin mapper1.bin mapper2.bin mapper3.bin mapper4.bin \
        pocketnes.gba pceadvance.gba gbongba-0.4.gba Easymb2gba.gba \
        splash9.raw splashlogo.raw gameboyplayer.raw)
DOCS := README.md docs/FORMATS.md

dist: all-platforms
	@rm -rf dist && mkdir -p dist
	$(MAKE) --no-print-directory build/static/fcabuild build/static/fcasvedt build/static/fca-mkfs build/static/gbaraw
	@set -e; for p in linux-x86_64:static: win32:win32:.exe win64:win64:.exe; do \
	    name=$${p%%:*}; rest=$${p#*:}; dir=$${rest%%:*}; ext=$${rest#*:}; \
	    out=dist/fca-multi-builder-$(VERSION)-$$name; mkdir -p $$out; \
	    for t in $(TOOLS); do cp build/$$dir/$$t$$ext $$out/; done; \
	    cp $(DATA) $$out/; mkdir -p $$out/docs; cp $(DOCS) $$out/docs/; \
	    (cd dist && zip -qr fca-multi-builder-$(VERSION)-$$name.zip fca-multi-builder-$(VERSION)-$$name); \
	    echo "dist/fca-multi-builder-$(VERSION)-$$name.zip"; \
	done

# Fully static Linux binaries for the release zip.
build/static/%: build/native/%
	@mkdir -p $(@D)
	$(CC) $(WARN) $(DEFS) $(CFLAGS) $(addprefix src/,$($*_SRC)) -o $@ -static -s

clean:
	rm -rf build dist
