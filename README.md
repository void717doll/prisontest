# prisontest

a singleplayer prison mining game. mine blocks, sell what you collect, upgrade
your pickaxe and work through ranks and prestiges. collect pets, robots,
crystals and pickaxe parts along the way. your equipped pet follows you around the
mine; its appearance can be switched off in settings without losing its boost.

this is the standalone C++ client, built with SDL2 and OpenGL.
download packages from this repository's
[releases](https://github.com/void717doll/prisontest/releases).

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

active autominer time, sell/token/xp multipliers and temporary enchant effects
have countdown bars above the mine. the autominer countdown accounts for its
current speed; stored time remains in the scoreboard.

open Statistics from the pause menu or use `/stats` for this world's totals:
mining, currency earned, enchant activations, pets, crystals and other actions.
older worlds keep their recorded totals; actions that were never tracked start at zero.

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

when this checkout lives in a workspace with sibling `assets/` and `site/`
folders, sync their contents after changing game data or features:

```sh
python3 tools/sync_workspace.py
python3 tools/sync_workspace.py --check
```

the client owns the resources, version and help text. the sync exports assets
and rebuilds the site's guide, commands and feature summaries from those files.
edit `data/help.json` alongside gameplay changes so additions and removals reach
both the in-game help and website. `run.sh` also syncs this workspace before building.
`python3 tools/sync_workspace.py --install-hooks` installs local pre-commit checks
in the workspace repositories. standalone client checkouts still build normally.

## credits and license

- Pramsing — original concept.
- void717doll — adaptation of the original concept.
- [Luanti](https://www.luanti.org/) — the original game's engine and a reference for this client.
- Minetest Game and its artists — scenery and flower textures.
- Minecraft and Roblox's OP Prison game modes — mining and progression inspiration.
- Codex — help with coding and development.

source is licensed under [GPL-3.0](LICENSE). textures, fonts and sounds have their own
[credits and licenses](ASSET_CREDITS.md).
