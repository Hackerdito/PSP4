#!/usr/bin/env bash
# Builds VAULT's built-in PSP engine: PPSSPP with M33's PS4 port (github.com/iHaiDeeZ/M33) and
# VAULT's own patch on top, linked together with VAULT's library screen into one eboot.
#
# Ubuntu 24.04 / WSL. Needs: clang-20 lld-20 llvm-20 cmake ninja-build meson nasm python3-venv
# glslang-tools libdrm-dev git, plus .NET 8 (PkgTool) and Go (create-fself) for packaging.
#
#   engine/build-engine.sh            # everything, in order (several hours on 2 cores)
#   engine/build-engine.sh <step>     # one step: toolchain | deps | mesa | ppsspp
#
# Layout (same as M33's scripts expect): ~/m33 = M33 checkout, ~/m33-build = build tree,
# /opt/pacbrew/ps4/openorbis = OpenOrbis tree (PacBrew layout, built from source here).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
export PSB="${PSB:-$HOME/m33-build}"
M33="${M33:-$HOME/m33}"
OO=/opt/pacbrew/ps4/openorbis
M33_COMMIT=c8865c81426009531be3876ac279fe3de894a164      # M33 1.03
PPSSPP_BASE=5f2d4afbdced51ac056af25a05fd182a1175b558
LLVM_TAG=llvmorg-21.1.6
step() { echo; echo "=== $1 ($(date +%T))"; }

clone_at() {  # clone_at <url> <dir> <commit>
    [ -d "$2/.git" ] && return
    git init -q "$2"; git -C "$2" remote add origin "$1"
    git -C "$2" fetch -q --depth 1 origin "$3"; git -C "$2" checkout -q FETCH_HEAD
}

toolchain() {
    step "clang-21 names for clang-20 (M33's scripts call clang-21)"
    for t in clang clang++ ld.lld llvm-ar llvm-ranlib llvm-strip llvm-objcopy llvm-nm llvm-readelf llvm-objdump; do
        [ -e "/usr/bin/$t-20" ] && sudo ln -sf "/usr/bin/$t-20" "/usr/local/bin/$t-21"
    done
    step "M33 checkout"
    clone_at https://github.com/iHaiDeeZ/M33.git "$M33" "$M33_COMMIT"
    chmod +x "$M33"/toolchain/*.sh "$M33"/scripts/*.sh "$M33"/toolchain/bin/*

    step "OpenOrbis tree in $OO (PacBrew layout)"
    sudo mkdir -p "$OO" && sudo chown "$(id -u)" "$OO"
    mkdir -p "$OO"/{bin/linux,lib,cmake,usr/include,usr/lib,samples/piglet/sce_module}
    local W="$PSB/src/toolchain"; mkdir -p "$W"
    [ -d "$W/oo" ] || git clone -q --depth 1 https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain.git "$W/oo"
    cp -r "$W/oo/include" "$W/oo/link.x" "$OO/"
    [ -d "$W/pb" ] || git clone -q --depth 1 https://github.com/PacBrew/ps4-openorbis.git "$W/pb"
    cp "$W/pb/vars/ps4.cmake" "$OO/cmake/"; cp "$W/pb/vars/ps4vars.sh" "$OO/"
    # libc.prx / libSceFios2.prx (removed from the toolchain repo in c946047)
    [ -d "$W/oo-data" ] || { git clone -q --filter=blob:none --no-checkout https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain.git "$W/oo-data"
                             git -C "$W/oo-data" checkout -q c946047^ -- bin/data; }
    cp "$W/oo-data/bin/data/modules/"*.prx "$OO/samples/piglet/sce_module/"
    for t in clang clang++; do ln -sf "/usr/bin/$t-20" "$OO/bin/$t"; done
    for t in ld.lld llvm-ar llvm-ranlib llvm-nm llvm-objcopy llvm-strip; do ln -sf "/usr/bin/$t-20" "$OO/bin/$t"; done

    step "musl (PacBrew's PS4 fork)"
    clone_at https://github.com/PacBrew/ps4-openorbis-musl.git "$W/musl" 140a653299ffcb1457bc63a5ab1f507d7bb37c54
    (cd "$W/musl" && ./configure --target=x86_64-scei-ps4 --prefix="$OO" --disable-shared CC=clang-20 \
        CFLAGS="-fPIC -DPS4 -D_LIBUNWIND_IS_BAREMETAL=1 -DPS4_FLEXIBLE_MEMORY=1" >/dev/null && make -j"$(nproc)" >/dev/null && make install >/dev/null)

    step "LLVM $LLVM_TAG runtimes: compiler-rt builtins, libunwind (then libc++ via M33's script)"
    local L="$PSB/src/llvm-project"
    if [ ! -d "$L" ]; then
        git clone -q --depth 1 --branch "$LLVM_TAG" --filter=blob:none --sparse https://github.com/llvm/llvm-project.git "$L"
        git -C "$L" sparse-checkout set runtimes libcxx libcxxabi libunwind libc compiler-rt cmake llvm/cmake llvm/utils/lit llvm/utils/llvm-lit third-party
    fi
    local T=(-DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 -DCMAKE_AR=/usr/bin/llvm-ar-20
             -DCMAKE_RANLIB=/usr/bin/llvm-ranlib-20 -DCMAKE_C_COMPILER_TARGET=x86_64-pc-freebsd12-elf
             -DCMAKE_CXX_COMPILER_TARGET=x86_64-pc-freebsd12-elf -DCMAKE_ASM_COMPILER_TARGET=x86_64-pc-freebsd12-elf
             -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY)
    cmake -G Ninja -S "$L/compiler-rt/lib/builtins" -B "$PSB/build/builtins" "${T[@]}" -DCMAKE_SYSTEM_NAME=Generic \
        -DCMAKE_C_FLAGS="-fPIC -D__PS4__ -isystem $OO/include" -DCMAKE_ASM_FLAGS=-fPIC \
        -DCOMPILER_RT_BAREMETAL_BUILD=ON -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON -DLLVM_CMAKE_DIR="$L/llvm/cmake/modules" >/dev/null
    ninja -C "$PSB/build/builtins" >/dev/null
    cp "$PSB/build/builtins/lib/generic/libclang_rt.builtins-x86_64.a" "$OO/lib/"
    cmake -G Ninja -S "$L/runtimes" -B "$PSB/build/unwind" "${T[@]}" -DCMAKE_SYSTEM_NAME=FreeBSD -DLLVM_ENABLE_RUNTIMES=libunwind \
        -DCMAKE_C_FLAGS="-fPIC -D__PS4__ -D_LIBUNWIND_IS_BAREMETAL=1 -isysroot $OO -isystem $OO/include" \
        -DCMAKE_CXX_FLAGS="-fPIC -D__PS4__ -D_LIBUNWIND_IS_BAREMETAL=1 -isysroot $OO -isystem $OO/include -nostdinc++" \
        -DLIBUNWIND_ENABLE_STATIC=ON -DLIBUNWIND_ENABLE_SHARED=OFF -DLIBUNWIND_USE_COMPILER_RT=ON -DLIBUNWIND_ENABLE_THREADS=OFF \
        -DLIBUNWIND_IS_BAREMETAL=ON -DLIBUNWIND_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_TESTS=OFF >/dev/null
    ninja -C "$PSB/build/unwind" unwind_static >/dev/null
    cp "$PSB/build/unwind/lib/libunwind.a" "$OO/lib/"

    step "system library stubs (ps4libdoc 9.00)"
    local D="$PSB/src/ps4libdoc"
    [ -d "$D" ] || { git clone -q --depth 1 --filter=blob:none --sparse -b 9.00 https://github.com/idc/ps4libdoc.git "$D"
                     git -C "$D" sparse-checkout set --no-cone '/system/common/lib/*.json'; }
    local sprx; sprx=$(cd "$D/system/common/lib" && ls | grep -E '^(libSce|libkernel)[^ ]*\.sprx\.json$' | sed 's/\.sprx\.json$//')
    # shellcheck disable=SC2086
    python3 "$REPO/tools/gen-stubs.py" "$D/system/common/lib" "$OO/lib" $sprx >/dev/null
    cp "$W/pb/orbis-lib-gen/libSceLibcInternal.so" "$OO/lib/"   # PacBrew's (malloc stays musl's)

    step "libc++ 21 + runtime (M33's scripts)"
    HOME="$(dirname "$M33")" LLVM_LOCAL_SRC="$L" bash "$M33/toolchain/build-libcxx.sh"
    bash "$M33/toolchain/build-runtime.sh"
    bash "$M33/toolchain/m33-overlay.sh"
}

deps() {
    step "zlib, OpenSSL, curl, libsmb2, libnfs, libpng, zstd, libzip, FFmpeg"
    bash "$HERE/build-deps.sh"
}

mesa() {
    step "Mesa 26.0.8 RADV with the PS4 GNM back end (Crema's patches)"
    local R="$PSB/src/libdrm"; mkdir -p "$R/include/drm" "$R/amdgpu"
    cp /usr/include/libdrm/*.h "$R/include/drm/"; cp /usr/include/libdrm/amdgpu.h "$R/amdgpu/"
    if [ ! -d "$PSB/src/mesa" ]; then
        git clone -q --depth 1 --branch mesa-26.0.8 https://github.com/chaotic-cx/mesa-mirror.git "$PSB/src/mesa"
        local C="$PSB/src/crema"; [ -d "$C" ] || git clone -q --depth 1 https://github.com/iHaiDeeZ/Crema.git "$C"
        git -C "$PSB/src/mesa" apply "$C/patches/mesa-ps4.patch" "$C/patches/mesa-ps4-aligned-vertex-fetch.patch"
        cp "$C/scripts/build-mesa.sh" "$M33/scripts/build-mesa.sh"
    fi
    [ -d "$PSB/mesa-venv" ] || { python3 -m venv "$PSB/mesa-venv"; "$PSB/mesa-venv/bin/pip" install -q meson mako pyyaml packaging; }
    mkdir -p "$PSB/host-tools/bin"; ln -sf "$(command -v glslangValidator)" "$PSB/host-tools/bin/"
    HOME="$(dirname "$M33")" bash "$M33/scripts/build-mesa.sh" setup
}

ppsspp() {
    step "PPSSPP $PPSSPP_BASE + M33's PS4 patch + VAULT's patch"
    local S="$PSB/src/ppsspp"
    if [ ! -d "$S" ]; then
        clone_at https://github.com/hrydgard/ppsspp.git "$S" "$PPSSPP_BASE"
        git -C "$S" submodule update --init --recursive --depth 1
        git -C "$S" apply "$M33/patches/ppsspp-ps4.patch"
        git -C "$S" apply "$HERE/vault-engine.patch"
        git clone -q --depth 1 --branch 10.1.0 https://github.com/harfbuzz/harfbuzz.git "$PSB/src/harfbuzz"
        mkdir -p "$S/ext/harfbuzz"; cp -r "$PSB/src/harfbuzz/src" "$PSB/src/harfbuzz/COPYING" "$S/ext/harfbuzz/"
        git -C "$M33" apply "$HERE/vault-port.patch"
    fi
    # The PS1 engine first: ps4.cmake links its object into the eboot.
    M33="$M33" PSB="$PSB" bash "$HERE/build-ps1.sh"
    HOME="$(dirname "$M33")" bash "$M33/scripts/configure-ppsspp.sh" -DVAULT_DIR="$REPO"
    HOME="$(dirname "$M33")" JOBS="$(nproc)" bash "$M33/scripts/build-ppsspp.sh"
}

case "${1:-all}" in
    toolchain) toolchain ;; deps) deps ;; mesa) mesa ;; ppsspp) ppsspp ;;
    all) toolchain; deps; mesa; ppsspp ;;
    *) echo "usage: $0 [toolchain|deps|mesa|ppsspp]" >&2; exit 1 ;;
esac
