#!/usr/bin/env python3
"""sync the local asset export and website with this client."""

import argparse
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

from check_data import check as check_data
from sync_assets import MANIFEST, apply_export, plan_export


HOOK = """#!/bin/sh
# prisontest workspace sync
set -eu
repo=$(git rev-parse --show-toplevel)
if [ -f "$repo/client/tools/sync_workspace.py" ]; then
    client="$repo/client"
elif [ -f "$repo/tools/sync_workspace.py" ]; then
    client="$repo"
else
    client="$repo/../client"
fi
if [ -d "$client/../assets" ] && [ -f "$client/../site/tools/build.py" ]; then
    exec python3 "$client/tools/sync_workspace.py" --check --staged
fi
"""


def load_site_builder(site):
    path = site / "tools/build.py"
    spec = importlib.util.spec_from_file_location("prison_site", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def install_hooks(workspace):
    hooks = []
    for repo in (workspace, workspace / "client", workspace / "site"):
        if not (repo / ".git").exists():
            continue
        path = subprocess.check_output(
            ["git", "-C", str(repo), "rev-parse", "--git-path", "hooks/pre-commit"], text=True
        ).strip()
        hook = Path(path)
        if not hook.is_absolute():
            hook = repo / hook
        if hook.exists() and hook.read_text() != HOOK:
            raise ValueError(f"existing hook left unchanged: {hook}")
        hooks.append(hook)
    for hook in hooks:
        hook.parent.mkdir(parents=True, exist_ok=True)
        hook.write_text(HOOK)
        hook.chmod(0o755)
        print(f"installed {hook}")


def check_staged(workspace, outputs):
    repo = Path(subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"], text=True
    ).strip()).resolve()
    staged = subprocess.check_output(
        ["git", "diff", "--cached", "--name-only", "-z"]
    ).decode().split("\0")
    indexed_paths = set(subprocess.check_output(["git", "ls-files", "-z"]).decode().split("\0"))
    export_root = workspace / "assets" if repo == workspace else repo
    expected_files = {
        path: content for path, content in outputs.items()
        if repo in (workspace, workspace / "site") and path.is_relative_to(export_root)
    }
    for relative in filter(None, staged):
        path = repo / relative
        is_input = (
            repo == workspace / "client" and
            (relative in {"VERSION", "resources.txt", "ASSET_CREDITS.md"} or
             relative.startswith(("data/", "textures/", "ui/", "sounds/", "fonts/", "tools/sync_")))
        ) or (
            repo == workspace / "site" and
            (relative == "config.json" or relative.startswith(("templates/", "tools/")))
        )
        if is_input and path.is_file():
            expected_files[path] = path.read_bytes()
    for path, expected in expected_files.items():
        relative = path.relative_to(repo).as_posix()
        if relative not in indexed_paths:
            raise ValueError(f"missing staged export: {relative}; stage the generated file")
        indexed = subprocess.check_output(["git", "show", f":{relative}"])
        if indexed != expected:
            raise ValueError(f"staged copy is out of sync: {relative}; review and stage the updated file")
    if repo == workspace:
        for revision in ("HEAD", ""):
            previous = subprocess.run(
                ["git", "show", f"{revision}:assets/{MANIFEST}"],
                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            )
            if previous.returncode:
                continue
            manifest = json.loads(previous.stdout)
            if not isinstance(manifest, dict) or not isinstance(manifest.get("files"), list):
                raise ValueError("invalid prior asset sync manifest")
            for name in manifest["files"]:
                if not isinstance(name, str) or Path(name).is_absolute() or ".." in Path(name).parts:
                    raise ValueError("invalid path in prior asset sync manifest")
                path = workspace / "assets" / name
                relative = path.relative_to(repo).as_posix()
                if path not in outputs and relative in indexed_paths:
                    raise ValueError(f"retired asset remains staged: {relative}; stage its removal")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--check", action="store_true", help="report drift without changing files")
    parser.add_argument("--install-hooks", action="store_true", help="install local pre-commit sync checks")
    parser.add_argument("--staged", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.check and args.install_hooks:
        parser.error("use --install-hooks without --check")
    workspace = args.workspace.resolve()
    client, assets, site = (workspace / name for name in ("client", "assets", "site"))
    try:
        for path in (client / "VERSION", client / "resources.txt", site / "tools/build.py"):
            if not path.is_file():
                raise ValueError(f"workspace file missing: {path}")
        if not assets.is_dir():
            raise ValueError(f"asset directory missing: {assets}")
        check_data(client)
        outputs, removed = plan_export(client, assets)
        outputs.update(load_site_builder(site).plan_site(client, site))
        if args.staged:
            check_staged(workspace, outputs)
        changed = apply_export(outputs, removed, check=args.check)
        if args.check and changed:
            for path, content in outputs.items():
                if not path.is_file() or path.read_bytes() != content:
                    print(f"out of sync: {path.relative_to(workspace)}")
            for path in removed:
                print(f"retired: {path.relative_to(workspace)}")
            print("run python3 client/tools/sync_workspace.py from the workspace to update exports", file=sys.stderr)
            return 1
        print("workspace: synced" if changed else "workspace: up to date")
        if args.install_hooks:
            install_hooks(workspace)
        return 0
    except (OSError, ValueError, KeyError, TypeError, AttributeError, ImportError, subprocess.CalledProcessError) as error:
        print(f"sync: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
