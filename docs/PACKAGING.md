# desktop builds

`.github/workflows/prisoncpp.yml` builds this native source on branch pushes,
pull requests, `v*` tags and manual runs. every successful run uploads all ten
packages, checksums and release notes as the `prisontest-packages` artifact.
pushes to the default branch, version tags and manual runs also publish a release
in this same repository after every platform succeeds. pull requests and pushes
to other branches only upload artifacts. the workflow does not upload to the
public client repository.

## download packages

1. open this source repo's **Actions → native builds and releases → Run workflow**.
2. choose the branch. leave `release_tag` empty to release the version in `VERSION`,
   or enter a new version such as `v0.1.5` to override it.
3. when the run succeeds, download `prisontest-packages` from its artifacts or
   summary link. manual runs also attach each package to this repo's release.
4. extract the artifact and upload the ten packages in `assets/` plus
   `SHA256SUMS.txt` to `prisontest/prisontest-client` manually. copy `notes.md`
   into the public release description.

artifacts expire after 14 days; release attachments remain available. each
platform also uploads its own artifact, even if another platform fails later.
the combined bundle and release require all five platform builds to pass.

releases use the built-in `GITHUB_TOKEN`, with `contents: write` granted only
to the release job. no cross-repository token or secret is needed. new tags
created by manual runs point to the exact commit that was built. uploads go to
a draft first, so a failed upload can be retried before publishing. already
published versions stay unchanged; use a new version for new packages.

the public downloads repository does not contain the native source or `VERSION`;
run this workflow here, at the `prisoncpp` repository root.

## platform builds

| target | runner | packages |
| --- | --- | --- |
| linux x64 | ubuntu 22.04 | deb, rpm, AppImage and tar.gz |
| windows x64 | windows 2022, MSYS2 UCRT64 | .exe installer and portable zip |
| windows x86 (win32) | windows 2022, MSYS2 MINGW32 | 32-bit .exe installer and portable zip |
| macos arm64 | macos 15 | zip containing `prisontest.app` and its libraries |
| macos x64 | macos 15 intel | zip containing `prisontest.app` and its libraries |

the workflow only builds and packages. it configures `BUILD_TESTING=OFF`, does
not run the client or tests. game assets are packaged from the checkout.

new runs wait for an active run in the same concurrency group. build jobs have
a 30-minute limit. after changing the workflow, start a new **Run workflow**;
rerunning an old job keeps its original commit.

the executable is `prisontest` on linux and macos, and `prisontest.exe` on windows.
macos apps are ad-hoc signed for their relocated libraries and are not notarized.
windows executables are not signed.

build jobs use pinned actions with read-only repository permissions. packages
contain the client, required libraries, game assets, resource list, documentation
and licenses. saves and caches are excluded.
windows/mac packages also include a CA bundle for the optional HTTPS help API,
so it does not depend on a certificate path from the build machine.

every client build stages the files in `resources.txt` beside the executable,
or inside the macos app's Resources directory. packages install the same files
under `share/prisoncpp` on linux/windows and `Contents/Resources` on macos,
keeping the `ui/`, `textures/`, `sounds/`, `fonts/` and `data/` layout.
the first launch works offline. `--assets PATH` selects another local asset folder.

assets stay in the application directory. worlds, settings and the optional
help cache use the existing per-user data directory. install permissions can
protect shared game files from accidental edits by other users; this is an offline
game and does not prevent the machine's owner from editing files or saves.

## local dependencies

linux build dependencies are listed in [the native readme](../README.md).
the linux packages carry the matching JsonCpp library under `lib/prisoncpp`;
the executable finds it relative to its own location. this avoids requiring
Ubuntu's `libjsoncpp25` on Debian releases that provide `libjsoncpp26`.
its [upstream license](https://github.com/open-source-parsers/jsoncpp/blob/1.9.6/LICENSE)
is installed under `share/prisoncpp/licenses`.

install a downloaded deb with `sudo apt install ./prisontest-v0.1.0-linux-x64.deb`,
using its actual filename. apt installs the remaining runtime dependencies;
`dpkg -i` alone does not fetch them. the tar.gz needs those system libraries too;
use the AppImage to bundle those dependencies. do not symlink different
JsonCpp sonames to each other.

install the rpm with `sudo dnf install ./prisontest-v0.1.0-linux-x64.rpm`, using
its actual filename. rpm dependencies are generated from the linked libraries;
the private JsonCpp library is excluded from system provides and requirements.

make the AppImage executable with `chmod +x prisontest-v0.1.0-linux-x64.AppImage`,
then run it. game assets and runtime libraries are bundled.
[linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) builds
this package with a pinned release and checksum.

on macos:

```sh
brew install cmake ninja pkg-config sdl2-compat freetype libpng jsoncpp curl
export PKG_CONFIG_PATH="$(brew --prefix curl)/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
```

Homebrew's `sdl2-compat` loads SDL3 dynamically, which ordinary dependency
scanning misses. macos packages include `Contents/Frameworks/libSDL3.dylib`
beside SDL2, where the compatibility library looks for it. both libraries'
licenses are included under `Contents/Resources/licenses`. builds using the
original SDL2 do not need SDL3.

on windows x64, use an MSYS2 UCRT64 shell:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-freetype mingw-w64-ucrt-x86_64-libpng mingw-w64-ucrt-x86_64-jsoncpp mingw-w64-ucrt-x86_64-curl
```

for win32, use a MINGW32 shell and replace `mingw-w64-ucrt-x86_64-` with
`mingw-w64-i686-` in those package names. [MSYS2 environments](https://www.msys2.org/docs/environments/)
list the target architectures.

windows installers use [NSIS](https://nsis.sourceforge.io/Download), available
with `choco install nsis --yes`. each installer includes the runtime DLLs and
adds start-menu and desktop shortcuts. saves and settings stay in the
user's data directory when the client is uninstalled.

then, on any target:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --parallel 2
cpack --config build/CPackConfig.cmake -B build/packages
```

on linux, install `rpm` and `dpkg-dev` for the rpm and deb generators. the
AppImage also needs `curl`, `file` and `desktop-file-utils`:

```sh
sh tools/package_appimage.sh build
```

to run checks locally, enable testing with the repository's asset files:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DPRISON_TEST_ASSETS="$PWD"
cmake --build build --parallel 2
python3 tools/check_data.py .
ctest --test-dir build --output-on-failure
```

`VERSION` supplies the development package version. `-DPRISON_VERSION=0.1.0`
overrides it; the workflow supplies the version from a `vMAJOR.MINOR.PATCH` tag.
release files use fixed platform names, such as
`prisontest-v0.1.0-linux-x64.tar.gz`, with a shared `SHA256SUMS.txt`.

the macos/windows configure step finds the package manager's certificate bundle.
use `-DPRISON_CA_BUNDLE=/path/to/cacert.pem` to supply another bundle location.

Python 3 is needed for the static data audit, not to run the game. CMake 3.21 or
newer is required. packages appear in `build/packages/`. macos builds
must run on a mac; windows packages use the native UCRT64 or MINGW32 compiler.

references: [github runner images](https://github.com/actions/runner-images),
[MSYS2 action](https://github.com/msys2/setup-msys2),
[CMake runtime dependency installation](https://cmake.org/cmake/help/latest/command/install.html),
[macos bundle relocation](https://cmake.org/cmake/help/latest/module/BundleUtilities.html).
