#!/bin/sh
set -eu

if [ "${MSYSTEM:-}" != MINGW32 ] || [ "$(gcc -dumpmachine)" != i686-w64-mingw32 ]; then
    echo 'run this script in an msys2 mingw32 shell with the i686 compiler' >&2
    exit 1
fi

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="$project_dir/build/win32-deps"
install_prefix=$(cygpath -m "$MINGW_PREFIX")
mkdir -p "$build_dir"

# msys2 no longer ships these three libraries for mingw32.
build_library() {
    name=$1
    url=$2
    checksum=$3
    shift 3
    archive="$build_dir/$name.tar"
    if [ ! -f "$archive" ]; then
        curl --fail --location --retry 3 "$url" --output "$archive.part"
        printf '%s  %s\n' "$checksum" "$archive.part" | sha256sum --check
        mv "$archive.part" "$archive"
    fi
    printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check
    mkdir -p "$build_dir/$name"
    tar -xf "$archive" --strip-components=1 -C "$build_dir/$name"
    cmake -S "$build_dir/$name" -B "$build_dir/$name-build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$install_prefix" \
        -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_SHARED_LIBS=ON "$@"
    cmake --build "$build_dir/$name-build" --parallel 2
    cmake --install "$build_dir/$name-build"
}

build_library SDL2-2.32.10 \
    https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz \
    5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165 \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL_TESTS=OFF

build_library libpng-1.6.59 \
    https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.59.tar.gz \
    2540302a1844ad2b2b501977abecfa850f265f97b78f065a712ab4074a89f5b5 \
    -DPNG_SHARED=ON -DPNG_STATIC=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF

build_library freetype-2.15.0 \
    https://download-mirror.savannah.gnu.org/releases/freetype/freetype-2.15.0.tar.xz \
    93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5 \
    -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_BROTLI=ON \
    -DFT_DISABLE_HVF=ON -DFT_REQUIRE_ZLIB=ON -DFT_REQUIRE_PNG=ON

install -Dm644 "$build_dir/SDL2-2.32.10/LICENSE.txt" "$MINGW_PREFIX/share/licenses/SDL2/LICENSE"
install -Dm644 "$build_dir/libpng-1.6.59/LICENSE" "$MINGW_PREFIX/share/licenses/libpng/LICENSE"
install -Dm644 "$build_dir/freetype-2.15.0/docs/FTL.TXT" "$MINGW_PREFIX/share/licenses/freetype/FTL.TXT"
pkg-config --modversion sdl2 freetype2 libpng
