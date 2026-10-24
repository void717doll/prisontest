# prisontest

a singleplayer prison mining game. mine blocks, sell what you collect, upgrade
your pickaxe and work through ranks and prestiges. pets, robots, crystals and
bosses give you more to work towards.

this is the standalone C++ client, built with SDL2 and OpenGL.
[downloads](https://github.com/prisontest/prisontest-client/releases) are kept
in the public client repository.

packages: linux deb, rpm, AppImage and tar.gz; windows x64 and win32 installers and zips;
macos apple silicon and intel apps.

## build

requires a C++17 compiler, CMake 3.21 or newer, SDL2, OpenGL, FreeType, libpng,
JsonCpp and libcurl 7.62 or newer.

on debian or ubuntu:

```sh
sudo apt install build-essential cmake pkg-config libsdl2-dev libgl-dev \
  libfreetype-dev libpng-dev libjsoncpp-dev libcurl4-openssl-dev
./run.sh
```

`run.sh` builds and starts the client. to build without launching it:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
```

the linux executable is `build/prisontest`. see [desktop builds](docs/PACKAGING.md)
for macos and windows dependencies, packages and the release workflow.

## assets and saves

the asset files are kept in `ui/`, `textures/`, `sounds/`, `fonts/` and `data/`
at the repository root. every build and release package includes the files
listed in `resources.txt`. the client loads them directly from its installation,
so the first launch works offline without an asset download or cache.

on linux, saves and settings live under `~/.local/share/prisoncpp/`,
or `$XDG_DATA_HOME/prisoncpp/` when set. windows uses
`%LOCALAPPDATA%/prisoncpp/`; macos uses `~/Library/Application Support/prisoncpp/`.
`--save-dir PATH` changes the save directory and puts settings beside it.

## playing

use WASD to move, left click to mine and right click to use an item.
E opens your inventory, M opens your pickaxe, and T or / opens chat.
type `/help` for commands and game systems. new worlds start with a tutorial
that can be skipped.

## development

the core checks can use the repository's asset files:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DPRISON_TEST_ASSETS="$PWD"
cmake --build build --parallel 2
python3 tools/check_data.py .
ctest --test-dir build --output-on-failure
```

`--assets "$PWD"` runs the client with the repository's files instead of the
build's copy. [game assets](docs/ASSETS.md) covers the file layout and packaging;
[game data](data/README.md) describes the gameplay tables.

## credits and license

- Pramsing — original concept.
- r0t — adaptation of the original concept.
- [Luanti](https://www.luanti.org/) — the original game's engine and a reference for this client.
- Minetest Game and its artists — scenery and flower textures.
- Minecraft and Roblox's OP Prison game modes — mining and progression inspiration.
- Codex — help with coding and development.

source is licensed under [GPL-3.0](LICENSE). textures, fonts and sounds have their own
[credits and licenses](ASSET_CREDITS.md).
