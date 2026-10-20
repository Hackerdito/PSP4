#!/usr/bin/env bash
# Builds the PS1 engine (PCSX-ReARMed's libretro core, GPLv2) for the PS4 and packs it into one
# object for SAMPSP4's eboot: $PSB/build/ps1/ps1core.o.
#
# The core is linked next to PPSSPP, which has its own libchdr, zstd, lzma and zlib-like code, so
# every symbol except the libretro API is made local, and that API is renamed retro_* -> ps1core_*
# (src/ps1.c calls it by those names).
#
#   engine/build-ps1.sh            (then rebuild the engine: ps4.cmake links the object when present)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
TC="${M33:-$HOME/m33}/toolchain"
export PSB="${PSB:-$HOME/m33-build}"
source "$TC/env.sh"
JOBS="${JOBS:-$(nproc)}"
PCSX_COMMIT="${PCSX_COMMIT:-c881679}"
SRC="$PSB/src/pcsx_rearmed"
OUT="$PSB/build/ps1"
mkdir -p "$PSB/src" "$OUT"

if [ ! -d "$SRC" ]; then
    git clone -q https://github.com/libretro/pcsx_rearmed.git "$SRC"
    git -C "$SRC" checkout -q "$PCSX_COMMIT"
    git -C "$SRC" apply "$HERE/pcsx-ps4.patch"
fi

make -C "$SRC" -f Makefile.libretro platform=ps4 -j"$JOBS" \
    CC="$TC/bin/ps4-cc" CXX="$TC/bin/ps4-c++" AR="$PS4_AR" >/dev/null

ld.lld -r --whole-archive "$SRC/pcsx_rearmed_libretro_ps4.a" -o "$OUT/pcsx_all.o"
args=()
for s in $(llvm-nm --defined-only -g "$OUT/pcsx_all.o" | awk '$3 ~ /^retro_/ {print $3}'); do
    args+=(--redefine-sym="$s=ps1core_$s")
done
llvm-objcopy "${args[@]}" "$OUT/pcsx_all.o" "$OUT/renamed.o"
llvm-objcopy --wildcard --keep-global-symbol='ps1core_*' "$OUT/renamed.o" "$OUT/ps1core.o"
rm -f "$OUT/pcsx_all.o" "$OUT/renamed.o"
echo "$OUT/ps1core.o: $(llvm-nm -g --defined-only "$OUT/ps1core.o" | wc -l) exported, $(llvm-nm -u "$OUT/ps1core.o" | wc -l) imported"
