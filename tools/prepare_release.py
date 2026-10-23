#!/usr/bin/env python3
"""collect client packages and checksums for a release."""

import argparse
import hashlib
from pathlib import Path
import re
import shutil


PACKAGES = (
    ("linux-x64", "tar.gz"),
    ("linux-x64", "deb"),
    ("linux-x64", "rpm"),
    ("linux-x64", "AppImage"),
    ("windows-x64", "zip"),
    ("windows-x64", "exe"),
    ("windows-x86", "zip"),
    ("windows-x86", "exe"),
    ("macos-arm64", "zip"),
    ("macos-x64", "zip"),
)


def prepare(tag, artifacts, output):
    if not re.fullmatch(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", tag) or len(tag) > 40:
        raise ValueError("release tags must use v0.0.0")
    sources = []
    for platform, extension in PACKAGES:
        matches = list((artifacts / f"prisontest-{platform}").glob(f"*.{extension}"))
        if len(matches) != 1 or matches[0].is_symlink() or not matches[0].is_file() or matches[0].stat().st_size == 0:
            raise ValueError(f"expected one complete {platform} .{extension} package")
        sources.append((matches[0], f"prisontest-{tag}-{platform}.{extension}"))
    assets = output / "assets"
    assets.mkdir(parents=True, exist_ok=True)
    if any(assets.iterdir()):
        raise ValueError("release staging directory must be empty")
    checksums = []
    for source, name in sources:
        target = assets / name
        shutil.copyfile(source, target)
        digest = hashlib.sha256()
        with target.open("rb") as package:
            for chunk in iter(lambda: package.read(1048576), b""):
                digest.update(chunk)
        checksums.append(f"{digest.hexdigest()}  {name}")
    (assets / "SHA256SUMS.txt").write_text("\n".join(checksums) + "\n", encoding="utf-8")
    (output / "notes.md").write_text(f"""prisontest {tag}

- windows: choose x64 or x86 (win32) and run the .exe installer. for a portable copy, extract the zip and run `prisontest.cmd`.
- linux: run the AppImage, install the deb/rpm, or extract the tar.gz and run `usr/bin/prisontest`.
- macos: extract the zip and open `prisontest.app`. choose apple silicon or intel for your mac.

the linux tar.gz needs SDL2, OpenGL, FreeType, libpng and libcurl from your
package manager. install the deb with `sudo apt install ./prisontest-{tag}-linux-x64.deb`,
or the rpm with `sudo dnf install ./prisontest-{tag}-linux-x64.rpm`.
for the AppImage, run `chmod +x prisontest-{tag}-linux-x64.AppImage` first.
JsonCpp is bundled in all linux packages. macos builds are ad-hoc signed and not notarized; windows builds
are not signed.

the first launch requires internet. assets download from https://assets.prisontest.com/ and stay cached for offline
play. existing worlds remain in the normal per-user save directory.

SHA256SUMS.txt contains checksums for all ten packages. use the named client
packages above; github's automatic source archives are not playable builds.
""", encoding="utf-8")
    print(f"release: staged {len(sources)} packages and checksums in {assets}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tag")
    parser.add_argument("artifacts", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        prepare(args.tag, args.artifacts, args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, f"release: {error}\n")
