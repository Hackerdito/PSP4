#!/usr/bin/env bash
# Builds an OpenOrbis-compatible PS4 SDK from source into ./sdk (Linux or WSL).
#
# Needs: git, clang + lld (14+), make, go (1.17+), python3, dotnet-sdk-8.0
#   Ubuntu/WSL: sudo apt install clang lld make golang python3 dotnet-sdk-8.0
#
# Then:  export OO_PS4_TOOLCHAIN=$PWD/sdk && make
#
# (You can also use an official OpenOrbis release instead; this script exists so the
#  whole build is reproducible from source.)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$ROOT/.sdk-src"
SDK="$ROOT/sdk"
mkdir -p "$WORK" "$SDK/bin/linux" "$SDK/lib"
cd "$WORK"

clone() { [ -d "$2" ] || git clone -q --depth 1 "$1" "$2"; }

echo "== sources"
clone https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain.git toolchain
clone https://github.com/OpenOrbis/musl.git musl
clone https://github.com/OpenOrbis/create-fself.git create-fself
clone https://github.com/OpenOrbis/create-gp4.git create-gp4
clone https://github.com/OpenOrbis/LibOrbisPkg.git LibOrbisPkg
if [ ! -d libdoc ]; then
  git clone -q --depth 1 --filter=blob:none --sparse -b 5.05 https://github.com/idc/ps4libdoc.git libdoc
  (cd libdoc && git sparse-checkout set --no-cone \
     /system/common/lib/libkernel.sprx.json /system/common/lib/libSceVideoOut.sprx.json \
     /system/common/lib/libScePad.sprx.json /system/common/lib/libSceUserService.sprx.json \
     /system/common/lib/libSceSysmodule.sprx.json /system/common/lib/libSceSystemService.sprx.json)
fi
# bin/data (libc.prx, libSceFios2.prx, right.sprx) was removed from the toolchain repo in c946047.
if [ ! -d toolchain-data ]; then
  git clone -q --filter=blob:none --no-checkout https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain.git toolchain-data
  (cd toolchain-data && git checkout -q c946047^ -- bin/data)
fi

echo "== headers + linker script"
cp -r toolchain/include toolchain/link.x "$SDK/"
cp -r toolchain-data/bin/data "$SDK/bin/"

echo "== musl libc"
(cd musl && ./configure --target=x86_64-scei-ps4 --disable-shared CC=clang \
   CFLAGS="-fPIC -DPS4 -D_LIBUNWIND_IS_BAREMETAL=1" --prefix="$WORK/musl-out" >/dev/null && make -j"$(nproc)" >/dev/null && make install >/dev/null)
cp musl-out/lib/*.a musl-out/lib/crt1.o "$SDK/lib/"
cp -rn musl-out/include/* "$SDK/include/" 2>/dev/null || true

echo "== create-fself / create-gp4"
(cd create-fself/cmd/create-fself && go build -modfile=go-linux.mod -o "$SDK/bin/linux/create-fself" .)
(cd create-gp4/cmd/create-gp4 && go build -o "$SDK/bin/linux/create-gp4" .)

echo "== PkgTool.Core"
cat > "$WORK/nuget.config" <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<configuration><packageSources><clear /></packageSources></configuration>
EOF
(cd LibOrbisPkg/PkgTool.Core && dotnet build -c Release --configfile "$WORK/nuget.config" -o "$SDK/bin/linux/pkgtool" >/dev/null)
printf '#!/bin/sh\nexec dotnet "%s/bin/linux/pkgtool/PkgTool.Core.dll" "$@"\n' "$SDK" > "$SDK/bin/linux/PkgTool.Core"
chmod +x "$SDK/bin/linux/PkgTool.Core"

echo "== system library stubs"
python3 "$ROOT/tools/gen-stubs.py" libdoc/system/common/lib "$SDK/lib" \
  libkernel libSceVideoOut libScePad libSceUserService libSceSysmodule libSceSystemService >/dev/null

echo "SDK ready: export OO_PS4_TOOLCHAIN=$SDK"
