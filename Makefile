# VAULT - PSP game launcher for jailbroken PS4 (GoldHEN).
#
#   make            -> VAULT.pkg   (needs OO_PS4_TOOLCHAIN, see README)
#   make host       -> desktop test build that renders screenshots to build/host/
#   make clean

TITLE       := VAULT
VERSION     := 01.00
TITLE_ID    := VALT00001
CONTENT_ID  := IV0000-VALT00001_00-VAULTPSPLAUNCHER

SRCS        := src/main.c src/ui.c src/gfx.c src/disc.c src/library.c
PS4_SRCS    := $(SRCS) src/platform_ps4.c
HOST_SRCS   := $(SRCS) src/platform_host.c

ASSETS      := $(wildcard assets/fonts/*)
MODULES     := $(wildcard sce_module/*)

TOOLCHAIN   := $(OO_PS4_TOOLCHAIN)
CREATE_FSELF ?= $(TOOLCHAIN)/bin/linux/create-fself
CREATE_GP4   ?= $(TOOLCHAIN)/bin/linux/create-gp4
PKGTOOL      ?= $(TOOLCHAIN)/bin/linux/PkgTool.Core

PS4_CC      ?= clang
LD          := ld.lld
BUILD       := build/ps4
OBJS        := $(patsubst src/%.c,$(BUILD)/%.o,$(PS4_SRCS))

CFLAGS      := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -O2 -Wall -Wno-unused-function \
               -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include -Ithird_party
LDFLAGS     := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr --gc-sections -L$(TOOLCHAIN)/lib \
               -lc -lm -lkernel -lSceVideoOut -lScePad -lSceUserService -lSceSystemService $(TOOLCHAIN)/lib/crt1.o

PKG         := $(CONTENT_ID).pkg

all: VAULT.pkg

VAULT.pkg: $(PKG)
	cp $< $@

$(PKG): pkg.gp4
	$(PKGTOOL) pkg_build $< .

pkg.gp4: eboot.bin sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png sce_sys/pic1.png $(MODULES) $(ASSETS)
	$(CREATE_GP4) -out $@ --content-id=$(CONTENT_ID) --files "$^"

sce_sys/param.sfo: Makefile
	$(PKGTOOL) sfo_new $@
	$(PKGTOOL) sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(PKGTOOL) sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
	$(PKGTOOL) sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
	$(PKGTOOL) sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(PKGTOOL) sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TITLE)'
	$(PKGTOOL) sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(PKGTOOL) sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'

eboot.bin: $(OBJS)
	$(LD) $(OBJS) -o $(BUILD)/vault.elf $(LDFLAGS)
	$(CREATE_FSELF) -in=$(BUILD)/vault.elf -out=$(BUILD)/vault.oelf --eboot "eboot.bin" --paid 0x3800000000000011

$(BUILD)/%.o: src/%.c src/*.h
	@mkdir -p $(BUILD)
	$(PS4_CC) $(CFLAGS) -c -o $@ $<

# ---- desktop test build ----------------------------------------------------
host: build/host/vault-host

build/host/vault-host: $(HOST_SRCS) src/*.h
	@mkdir -p build/host
	cc -O2 -g -Wall -Wno-unused-function -DVAULT_HOST -Ithird_party -o $@ $(HOST_SRCS) -lm

clean:
	rm -rf build eboot.bin pkg.gp4 sce_sys/param.sfo $(PKG) VAULT.pkg

.PHONY: all host clean
