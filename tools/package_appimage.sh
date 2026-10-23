#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(CDPATH= cd -- "${1:-build}" && pwd)
version=${2:-$(cat "$project_dir/VERSION")}
tool="$build_dir/appimage-tools/linuxdeploy-x86_64.AppImage"
app_dir="$build_dir/AppDir"

mkdir -p "$build_dir/appimage-tools" "$build_dir/packages"
if [ ! -f "$tool" ]; then
    curl --fail --location --retry 3 \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
        --output "$tool.part"
    mv "$tool.part" "$tool"
fi
printf '%s  %s\n' c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d "$tool" | sha256sum --check
chmod +x "$tool"

cmake -E rm -rf "$app_dir"
cmake --install "$build_dir" --prefix "$app_dir/usr"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 \
    LINUXDEPLOY_OUTPUT_VERSION="$version" LDAI_OUTPUT="$build_dir/packages/prisontest-$version-linux-x64.AppImage" \
    "$tool" --appimage-extract-and-run --appdir "$app_dir" \
    --executable "$app_dir/usr/bin/prisontest" \
    --desktop-file "$app_dir/usr/share/applications/prisontest.desktop" \
    --icon-file "$app_dir/usr/share/icons/hicolor/16x16/apps/prisontest.png" \
    --output appimage
