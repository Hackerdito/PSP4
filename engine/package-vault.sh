#!/usr/bin/env bash
# Packages SAMPSP4 (VAULT's screen + its built-in PSP engine) (PPSSPP, from M33's PS4 port) into one PKG.
#
#   engine/package-vault.sh <version XX.YY>
#
# Expects the engine built by engine/build-engine.sh in $PSB (default ~/m33-build) and the
# M33 checkout (its assets and make-pkg.sh) in $M33 (default ~/m33).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
PSB="${PSB:-$HOME/m33-build}"
M33="${M33:-$HOME/m33}"
VERSION="${1:-01.00}"
EBOOT="$PSB/build/ppsspp/PPSSPPPS4_eboot/eboot.bin"
SRC="$PSB/src/ppsspp"
[ -f "$EBOOT" ] || { echo "missing $EBOOT: build the engine first" >&2; exit 1; }

STAGE="$PSB/build/vault-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/sce_sys" "$STAGE/assets" "$STAGE/vault/fonts"
cp "$EBOOT" "$STAGE/eboot.bin"
cp "$REPO/sce_sys/icon0.png" "$REPO/sce_sys/pic1.png" "$STAGE/sce_sys/"
echo "$VERSION" > "$STAGE/version.txt"
# The engine's assets: PPSSPP's, plus the PS4 port's theme, sounds and CA bundle.
tar -C "$SRC/assets" --exclude=debugger --exclude=upload --exclude=.git -cf - . | tar -C "$STAGE/assets" -xf -
cp -r "$M33/assets/." "$STAGE/assets/"
python3 "$M33/scripts/brand-stage.py" "$STAGE/assets" "$M33"
# The port's texts say "M33" where PPSSPP said "PPSSPP": in this app, that's SAMPSP4.
sed -i 's/\bM33\b/SAMPSP4/g' "$STAGE"/assets/lang/*.ini
# VAULT's own screen.
cp "$REPO/assets/fonts/"* "$STAGE/vault/fonts/"
find "$STAGE" -type d -empty -delete

PKG="$(SFO_STYLE=plain NO_RIGHT_SPRX=1 DOLPHIN_PS4_MODULES_DIR=/nonexistent \
    "$M33/scripts/make-pkg.sh" "$STAGE" VALT00001 "SAMPSP4" "$VERSION" VAULTPSPLAUNCHER "$PSB/out" | tail -1)"
mkdir -p "$REPO/out"
cp "$PKG" "$REPO/out/SAMPSP4-v$VERSION.pkg"
ls -la "$REPO/out/SAMPSP4-v$VERSION.pkg"
