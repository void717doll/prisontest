#!/usr/bin/env python3
"""export bundled client assets without changing how the game loads them."""

import argparse
import json
import os
from pathlib import Path, PureWindowsPath
import tempfile


MANIFEST = ".sync-manifest.json"
SUPPORT_FILES = {
    "ASSET_CREDITS.md", "resources.txt", "fonts/LICENSE-DejaVu.txt",
    "sounds/README.md", "sounds/prison/README.md", "data/README.md", "VERSION",
}


def safe_path(root, relative):
    if not isinstance(relative, str) or not relative:
        raise ValueError("asset paths must be nonempty strings")
    path = Path(relative)
    if (path.is_absolute() or PureWindowsPath(relative).drive or "\\" in relative
            or ".." in path.parts or path == Path(".") or path.as_posix() != relative):
        raise ValueError(f"invalid asset path: {relative}")
    target = root / path
    if target.resolve() != target:
        raise ValueError(f"symlinks are not supported in asset paths: {target}")
    if target.exists() and not target.is_file():
        raise ValueError(f"expected an asset file: {target}")
    return target


def plan_export(client: Path, destination: Path) -> tuple[dict[Path, bytes], list[Path]]:
    """read and validate every source before returning absolute writes and removals."""
    client = client.resolve()
    destination = Path(os.path.abspath(destination))
    if destination.resolve() != destination:
        raise ValueError("the asset destination must not contain symlinks")
    if client.is_relative_to(destination) or destination.is_relative_to(client):
        raise ValueError("client and asset directories must not overlap")
    resource_list = safe_path(client, "resources.txt").read_text(encoding="utf-8")
    files = {line.strip() for line in resource_list.splitlines() if line.strip()}
    files.update(SUPPORT_FILES)
    if MANIFEST in files or "_headers" in files:
        raise ValueError("the resource list contains a reserved export file")

    manifest_path = safe_path(destination, MANIFEST)
    previous = set()
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if not isinstance(manifest, dict) or not isinstance(manifest.get("files"), list):
            raise ValueError("invalid asset sync manifest")
        for relative in manifest["files"]:
            safe_path(destination, relative)
            if relative in (MANIFEST, "_headers"):
                raise ValueError("the sync manifest contains a reserved export file")
            previous.add(relative)

    outputs = {}
    for relative in sorted(files):
        source = safe_path(client, relative)
        target = safe_path(destination, relative)
        outputs[target] = source.read_bytes()
    removed = []
    for relative in sorted(previous - files):
        path = safe_path(destination, relative)
        if path.exists():
            removed.append(path)
    inventory = json.dumps({"files": sorted(files)}, indent=2) + "\n"
    outputs[manifest_path] = inventory.encode("utf-8")
    return outputs, removed


def apply_export(outputs: dict[Path, bytes], removed: list[Path], check=False) -> bool:
    """apply a plan and return whether files differed; check only reports drift."""
    changed = {}
    for path, content in outputs.items():
        if path.resolve() != path or (path.exists() and not path.is_file()):
            raise ValueError(f"invalid export target: {path}")
        if not path.exists() or path.read_bytes() != content:
            changed[path] = content
    for path in removed:
        if path.resolve() != path or (path.exists() and not path.is_file()):
            raise ValueError(f"invalid removal target: {path}")
    removed = [path for path in removed if path.exists()]
    if check:
        return bool(changed or removed)

    # retain the old inventory until its obsolete files have been removed.
    ordered = sorted(changed, key=lambda path: path.name == MANIFEST)
    for path in ordered:
        if path.name == MANIFEST:
            for obsolete in removed:
                obsolete.unlink()
            removed = []
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as output:
                temporary = Path(output.name)
                output.write(changed[path])
            mode = path.stat().st_mode & 0o777 if path.exists() else 0o644
            temporary.chmod(mode)
            os.replace(temporary, path)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    for obsolete in removed:
        obsolete.unlink()
    return bool(changed or removed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--assets", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    destination = args.assets if args.assets is not None else args.client.resolve().parent / "assets"
    try:
        outputs, removed = plan_export(args.client, destination)
        changed = apply_export(outputs, removed, check=args.check)
    except (OSError, ValueError) as error:
        parser.exit(1, f"assets: {error}\n")
    if args.check:
        print("assets: out of sync" if changed else "assets: up to date")
        return int(changed)
    print("assets: synced" if changed else "assets: up to date")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
