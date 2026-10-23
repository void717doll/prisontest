# prisontest

a singleplayer prison mining game. mine blocks, sell what you collect, upgrade
your pickaxe and work through ranks and prestiges. pets, robots, crystals and
bosses give you more to work towards.

this is the standalone C++ client, built with SDL2 and OpenGL.
[downloads](https://github.com/prisontest/prisontest-client/releases) are kept
in the public client repository.

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

textures, sounds, fonts and game data come from
`https://assets.prisontest.com/`. only the app icon is kept in this repository
and release packages. the first launch needs an internet connection; downloaded
assets are cached for later launches and offline play. `resources.txt` lists
the files the client downloads.

on linux, saves and the asset cache live under `~/.local/share/prisoncpp/`,
or `$XDG_DATA_HOME/prisoncpp/` when set. windows uses
`%LOCALAPPDATA%/prisoncpp/`; macos uses `~/Library/Application Support/prisoncpp/`.
`--save-dir PATH` changes the save directory and puts the cache beside it.

## playing

use WASD to move, left click to mine and right click to use an item.
E opens your inventory, M opens your pickaxe, and T or / opens chat.
type `/help` for commands and game systems. new worlds start with a tutorial
that can be skipped.

## development

the core checks use a downloaded asset pack:

```sh
cmake -S . -B build -DPRISON_TEST_ASSETS=/path/to/asset-pack
cmake --build build --parallel 2
python3 tools/check_data.py /path/to/asset-pack
ctest --test-dir build --output-on-failure
```

`--assets /path/to/asset-pack` runs the client with local files and skips
downloads. [asset hosting](docs/ASSETS.md) covers the file layout and cache;
[game data](data/README.md) describes the gameplay tables.

## credits and license

prisontest started as a game for [Luanti](https://www.luanti.org/). that version
is the gameplay reference for this client.

source is licensed under [GPL-3.0](LICENSE). fonts and sounds have their own
[credits and licenses](ASSET_CREDITS.md).
