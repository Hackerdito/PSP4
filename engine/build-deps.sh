#!/usr/bin/env bash
# zlib, OpenSSL, curl, libsmb2, libnfs for the PS4 sysroot (recipes from Crema/Resonance),
# fetched with git (release tarballs aren't reachable from this build machine).
set -euo pipefail
TC="${M33:-$HOME/m33}/toolchain"
export PSB="${PSB:-$HOME/m33-build}"
source "$TC/env.sh"
SRC="$PSB/src/deps"; BLD="$PSB/build/deps"; mkdir -p "$SRC" "$BLD"
JOBS="${JOBS:-$(nproc)}"
export PKG_CONFIG_LIBDIR="$PS4_SYSROOT/lib/pkgconfig:$PS4_SYSROOT/share/pkgconfig"
unset PKG_CONFIG_PATH CFLAGS CXXFLAGS LDFLAGS CPPFLAGS
gfetch() { [ -d "$SRC/$1" ] || git clone -q --depth 1 --branch "$3" "$2" "$SRC/$1"; }
cmake_build() {
    local name="$1" src="$2"; shift 2
    rm -rf "$BLD/$name"
    cmake -G Ninja -S "$src" -B "$BLD/$name" -DCMAKE_TOOLCHAIN_FILE="$TC/ps4-deps.cmake" -DPS4_TRY_LINK=ON \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PS4_SYSROOT" -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_PREFIX_PATH="$PS4_SYSROOT" -DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 "$@"
    ninja -C "$BLD/$name" -j"$JOBS"; ninja -C "$BLD/$name" install
}
MUSL_LINUX_ONLY=(-DHAVE_ACCEPT4=0 -DHAVE_EVENTFD=0 -DHAVE_SYS_EVENTFD_H=0 -DHAVE_PIPE2=0
    -DHAVE_SENDMMSG=0 -DHAVE_FSETXATTR=0 -DHAVE_FSETXATTR_5=0 -DHAVE_GETIFADDRS=0
    -DHAVE_CLOCK_GETTIME_MONOTONIC_RAW=0 -DHAVE_STROPTS_H=0)
ZLIB_ARGS=(-DZLIB_LIBRARY="$PS4_SYSROOT/lib/libz.a" -DZLIB_INCLUDE_DIR="$PS4_SYSROOT/include")

step() { echo "=== $1 $(date +%T)"; }


step zlib
gfetch zlib https://github.com/madler/zlib.git v1.3.1
o="$BLD/zlib"; rm -rf "$o"; mkdir -p "$o"
for f in adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast inflate inftrees trees uncompr zutil; do
    "$TC/bin/ps4-cc" -O2 -march=btver2 -DHAVE_UNISTD_H -DHAVE_STDARG_H -c "$SRC/zlib/$f.c" -o "$o/$f.o"
done
rm -f "$PS4_SYSROOT/lib/libz.a"; "$PS4_AR" rcs "$PS4_SYSROOT/lib/libz.a" "$o"/*.o
mkdir -p "$PS4_SYSROOT/include" "$PS4_SYSROOT/lib/pkgconfig"; cp "$SRC/zlib/zlib.h" "$SRC/zlib/zconf.h" "$PS4_SYSROOT/include/"
printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\nName: zlib\nDescription: zlib\nVersion: 1.3.1\nLibs: -L${libdir} -lz\nCflags: -I${includedir}\n' "$PS4_SYSROOT" > "$PS4_SYSROOT/lib/pkgconfig/zlib.pc"


step openssl
gfetch openssl https://github.com/openssl/openssl.git openssl-3.5.0
sed -i 's/^#elif defined(_GNU_SOURCE)$/#elif defined(_GNU_SOURCE) \&\& !defined(__PS4__)/' "$SRC/openssl/crypto/o_str.c"
# musl's in6_addr (OpenOrbis) under a FreeBSD triple: keep musl's own s6_addr32.
sed -i 's/^# if defined(__FreeBSD__) && defined(IN6_IS_ADDR_V4MAPPED)$/# if defined(__FreeBSD__) \&\& defined(IN6_IS_ADDR_V4MAPPED) \&\& !defined(__PS4__)/' "$SRC/openssl/crypto/bio/bss_dgram.c"
o="$BLD/openssl"; rm -rf "$o"; mkdir -p "$o"
(cd "$o" && CC="$TC/bin/ps4-cc" AR="$PS4_AR" RANLIB="$PS4_RANLIB" "$SRC/openssl/Configure" linux-x86_64 -march=btver2 \
    --prefix="$PS4_SYSROOT" --libdir=lib --openssldir=/app0/ssl --with-rand-seed=devrandom \
    no-shared no-module no-dso no-engine no-async no-afalgeng no-ktls no-secure-memory no-tests no-docs no-apps no-uplink)
make -C "$o" -j"$JOBS" build_libs >/dev/null
make -C "$o" install_dev >/dev/null
sed -i 's/ -ldl -pthread//; s/-ldl -pthread//' "$PS4_SYSROOT/lib/cmake/OpenSSL/OpenSSLConfig.cmake" "$PS4_SYSROOT/lib/pkgconfig/libcrypto.pc" || true


step curl
gfetch curl https://github.com/curl/curl.git curl-8_14_1
sed -i "s/\bSOL_IP\b/IPPROTO_IP/g" "$SRC/curl/lib/cf-socket.c"
cmake_build curl "$SRC/curl" "${ZLIB_ARGS[@]}" \
    -DBUILD_CURL_EXE=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_TESTING=OFF -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF \
    -DCURL_USE_OPENSSL=ON -DOPENSSL_ROOT_DIR="$PS4_SYSROOT" -DOPENSSL_USE_STATIC_LIBS=ON \
    -DUSE_NGHTTP2=OFF -DCURL_USE_LIBPSL=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DUSE_LIBIDN2=OFF -DCURL_USE_LIBSSH2=OFF \
    -DCURL_USE_LIBSSH=OFF -DCURL_USE_GSSAPI=OFF -DCURL_DISABLE_LDAP=ON -DCURL_DISABLE_LDAPS=ON -DENABLE_THREADED_RESOLVER=ON \
    -DCURL_CA_BUNDLE=none -DCURL_CA_PATH=none -DCURL_CA_FALLBACK=OFF "${MUSL_LINUX_ONLY[@]}"

step libsmb2
gfetch libsmb2 https://github.com/sahlberg/libsmb2.git v6.0.0
sed -i '/elseif(PS4)/,/elseif(EE)/{/CMAKE_C_FLAGS\|CMAKE_CXX_FLAGS\|add_compile_options/d}' "$SRC/libsmb2/CMakeLists.txt"
sed -i 's/^#ifndef __bswap\(16\|32\|64\)$/#if !defined(__bswap\1) \&\& !defined(PS4_PLATFORM)/' "$SRC/libsmb2/include/portable-endian.h"
cmake_build libsmb2 "$SRC/libsmb2" -DENABLE_EXAMPLES=OFF -DENABLE_LIBKRB5=OFF -DENABLE_GSSAPI=OFF "${MUSL_LINUX_ONLY[@]}"

step libnfs
gfetch libnfs https://github.com/sahlberg/libnfs.git libnfs-6.0.2
# OpenOrbis' sockaddr has sa_len but sockaddr_in6 has no sin6_len.
python3 - "$SRC/libnfs/lib/socket.c" <<'PY'
import sys
p = sys.argv[1]; L = open(p).read().split("\n")
for i, l in enumerate(L):
    if "sin6_len" in l:
        for j in range(i - 1, max(i - 4, 0), -1):
            if L[j].strip() == "#ifdef HAVE_SOCKADDR_LEN":
                L[j] = "#if 0 /* PS4: no sin6_len */"; break
open(p, "w").write("\n".join(L))
PY
cmake_build libnfs "$SRC/libnfs" -DENABLE_TESTS=OFF -DENABLE_EXAMPLES=OFF -DENABLE_UTILS=OFF -DENABLE_DOCUMENTATION=OFF "${MUSL_LINUX_ONLY[@]}"
step libpng
gfetch libpng https://github.com/pnggroup/libpng.git v1.6.47
cmake_build libpng "$SRC/libpng" "${ZLIB_ARGS[@]}" -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DPNG_FRAMEWORK=OFF
step zstd
gfetch zstd https://github.com/facebook/zstd.git v1.5.7
cmake_build zstd "$SRC/zstd/build/cmake" -DZSTD_BUILD_SHARED=OFF -DZSTD_BUILD_STATIC=ON -DZSTD_BUILD_PROGRAMS=OFF \
    -DZSTD_BUILD_TESTS=OFF -DZSTD_MULTITHREAD_SUPPORT=ON -DZSTD_LEGACY_SUPPORT=OFF -DZSTD_BUILD_DICTBUILDER=OFF \
    "-DCMAKE_C_FLAGS_RELEASE=-O3 -DNDEBUG -DZSTD_TRACE=0"
step libzip
gfetch libzip https://github.com/nih-at/libzip.git v1.11.4
cmake_build libzip "$SRC/libzip" "${ZLIB_ARGS[@]}" -DENABLE_COMMONCRYPTO=OFF -DENABLE_GNUTLS=OFF -DENABLE_MBEDTLS=OFF \
    -DENABLE_OPENSSL=OFF -DENABLE_WINDOWS_CRYPTO=OFF -DENABLE_BZIP2=OFF -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF \
    -DBUILD_TOOLS=OFF -DBUILD_REGRESS=OFF -DBUILD_OSSFUZZ=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF
step ffmpeg
gfetch ffmpeg https://github.com/FFmpeg/FFmpeg.git n7.1.1
o="$BLD/ffmpeg"; rm -rf "$o"; mkdir -p "$o"
(cd "$o" && "$SRC/ffmpeg/configure" --prefix="$PS4_SYSROOT" --enable-cross-compile --arch=x86_64 --target-os=linux \
    --cc="$TC/bin/ps4-cc" --cxx="$TC/bin/ps4-c++" --ar="$(command -v $PS4_AR)" --ranlib="$(command -v $PS4_RANLIB)" \
    --nm="$(command -v llvm-nm-21)" --strip="$(command -v llvm-strip-21)" \
    --extra-cflags="-march=btver2" --enable-pic --disable-shared --enable-static \
    --disable-everything --disable-autodetect --disable-programs --disable-doc --disable-network \
    --disable-avdevice --disable-avfilter --disable-postproc --disable-debug \
    --enable-avcodec --enable-avformat --enable-avutil --enable-swresample --enable-swscale \
    --enable-decoder=h264,mpeg4,h263,h263p,mpeg2video,mjpeg,mjpegb,aac,aac_latm,atrac3,atrac3al,atrac3p,atrac3pal,mp3,pcm_s16le,pcm_s8 \
    --enable-demuxer=h264,h263,m4v,mpegps,mpegvideo,avi,mp3,aac,pmp,oma,pcm_s16le,pcm_s8,wav,mov \
    --enable-parser=h264,mpeg4video,mpegvideo,aac,aac_latm,mpegaudio \
    --enable-encoder=ffv1,huffyuv,mpeg4,pcm_s16le --enable-muxer=avi --enable-protocol=file)
make -C "$o" -j"$JOBS" >/dev/null
make -C "$o" install >/dev/null
step done
ls -la "$PS4_SYSROOT"/lib/lib{png16,zstd,zip,avcodec,avformat,avutil,swresample,swscale}.a
