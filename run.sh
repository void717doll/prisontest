#!/bin/sh
set -eu
port_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cmake -S "$port_dir" -B "$port_dir/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$port_dir/build" -j "${PRISON_BUILD_JOBS:-2}"
if [ "$(uname -s)" = Darwin ]; then
    exec "$port_dir/build/prisontest.app/Contents/MacOS/prisontest" "$@"
fi
exec "$port_dir/build/prisontest" "$@"
