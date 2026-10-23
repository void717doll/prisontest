# desktop builds

`.github/workflows/prisoncpp.yml` builds this native source on branch pushes,
pull requests, `v*` tags and manual runs. every successful run uploads all five
packages, checksums and release notes as the `prisontest-packages` artifact.
version tags or an explicit `release_tag` also publish a release in this same
repository after every platform succeeds. the workflow does not upload to the
public client repository.

## download packages

1. open this source repo's **Actions → native builds and releases → Run workflow**.
2. choose the branch. leave `release_tag` empty for downloadable artifacts, or
   enter a new version such as `v0.1.1` to also create a release here.
3. when the run succeeds, download `prisontest-packages` from its artifacts or
   summary link. versioned runs also attach each package to this repo's release.
4. extract the artifact and upload the five packages in `assets/` plus
   `SHA256SUMS.txt` to `prisontest/prisontest-client` manually. copy `notes.md`
   into the public release description.

artifacts expire after 14 days; release attachments remain available. each
platform also uploads its own artifact, even if another platform fails later.
the combined bundle and release require all four platform builds to pass.

releases use the built-in `GITHUB_TOKEN`, with `contents: write` granted only
to the release job. no cross-repository token or secret is needed. new tags
created by manual runs point to the exact commit that was built. uploads go to
a draft first, so a failed upload can be retried before publishing. already
published versions stay unchanged; use a new version for new packages.

the public downloads repository does not contain the native source or `VERSION`;
run this workflow here, at the `prisoncpp` repository root.

## platform builds

new runs wait for an active run in the same concurrency group instead of canceling
it. github can still replace an older pending run with a newer one. build jobs
retain their 30-minute limit. the linux/macos installed-client check has its own
90-second process deadline, including the first asset download, and reports
launch failures, exit status or a timeout.
the surrounding step has a two-minute limit. startup stages are printed through
`PRISON_TRACE_STARTUP=1`. a macos timeout captures a five-second `sample` report
before killing the process, then uploads it as `native-check-macos-arm64` or
`native-check-macos-x64`. the log shows the call graph; the artifact keeps the
full report and startup output.

after a startup or workflow fix, start a new **Run workflow** on `main`.
**Re-run jobs** keeps the original commit and workflow, so it cannot pick up
new fixes. see [github reruns](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/re-run-workflows-and-jobs).

macos resolves bundled resources from `_NSGetExecutablePath` and
`Contents/Resources`, so headless launches do not enter SDL's Cocoa bundle lookup.
the diagnostic trace distinguishes resource lookup, data loading, mine setup and
process shutdown. a timeout alone does not establish which stage stalled.

`Error: The operation was canceled` alone does not identify the cause. check the
run summary for a job timeout, explicit cancellation or a superseding run; the
printed `if` line from older workflows only selects the installed executable.
see [github concurrency](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/control-workflow-concurrency).

| target | runner | package |
| --- | --- | --- |
| linux x64 | ubuntu 22.04 | deb and tar.gz with a private JsonCpp library |
| windows x64 | windows 2022, MSYS2 UCRT64 | zip with required runtime DLLs and `prisontest.cmd` |
| macos arm64 | macos 15 | zip containing `prisontest.app` and its libraries |
| macos x64 | macos 15 intel | zip containing `prisontest.app` and its libraries |

each job builds and packages the client, then checks the installed executable
from another directory. that first launch downloads the assets. the job audits
the downloaded json files and uses that same pack for the core suites.
windows also removes
MSYS2 from the test process's PATH, so missing packaged DLLs fail that check.
linux extracts the deb and checks its library metadata and JsonCpp resolution
before launching that packaged client.
macos extracts the zip, verifies the app signature and checks that SDL3 actually
loads from inside the extracted app.
the executable is `prisontest` on linux and macos, and `prisontest.exe` on windows.
linux runs the rendered UI checks under Xvfb with software OpenGL. macos/windows
rendering still needs testing on those systems. the macos bundle is ad-hoc
signed for its relocated libraries; it is not notarized. the windows binary is
not signed.

build jobs use pinned actions with read-only repository permissions. packages
contain the client, required libraries, app icon, resource list, documentation
and licenses. game data, textures, fonts, sounds, saves and caches are excluded.
windows/mac packages also include a CA bundle for asset downloads and the optional HTTPS help API,
so it does not depend on a certificate path from the build machine.

every client build stages the resource list and app icon beside the executable,
or inside the macos app's Resources directory. asset files are not needed to
build or package the client. installed `resources.txt` lists which files the
client fetches from `https://assets.prisontest.com/` on launch. downloads keep
the host's `ui/`, `textures/`, `sounds/`, `fonts/` and `data/` layout.
`--assets PATH` selects a local asset pack and skips downloads.

the resource list, app icon and trusted certificates stay in the application directory.
downloaded game assets, worlds, settings and the help cache use the existing
per-user data directory, in separate files and directories. install permissions can
protect shared game files from accidental edits by other users; this is an offline
game and does not prevent the machine's owner from editing files or saves.

## local dependencies

linux build dependencies are listed in [the native readme](../README.md).
both linux packages carry the matching JsonCpp library under `lib/prisoncpp`;
the executable finds it relative to its own location. this avoids requiring
Ubuntu's `libjsoncpp25` on Debian releases that provide `libjsoncpp26`.
its [upstream license](https://github.com/open-source-parsers/jsoncpp/blob/1.9.6/LICENSE)
is installed under `share/prisoncpp/licenses`.

install a downloaded deb with `sudo apt install ./prisontest-v0.1.0-linux-x64.deb`,
using its actual filename. apt installs the remaining runtime dependencies;
`dpkg -i` alone does not fetch them. the tar.gz needs those system libraries too;
it is not an AppImage. do not symlink different JsonCpp sonames to each other.

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

on windows, use an MSYS2 UCRT64 shell:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-freetype mingw-w64-ucrt-x86_64-libpng mingw-w64-ucrt-x86_64-jsoncpp mingw-w64-ucrt-x86_64-curl
```

then, on any target:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
cpack --config build/CPackConfig.cmake -B build/packages
```

to run the checks, point `PRISON_TEST_ASSETS` at a complete downloaded pack:

```sh
cmake -S . -B build -DPRISON_TEST_ASSETS=/path/to/asset-pack
cmake --build build --parallel 2
python3 tools/check_data.py /path/to/asset-pack
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
must run on a mac; windows packages use the native UCRT64 compiler, not MSYS's
POSIX compatibility compiler.

references: [github runner images](https://github.com/actions/runner-images),
[MSYS2 action](https://github.com/msys2/setup-msys2),
[CMake runtime dependency installation](https://cmake.org/cmake/help/latest/command/install.html),
[macos bundle relocation](https://cmake.org/cmake/help/latest/module/BundleUtilities.html).
