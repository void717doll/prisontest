# game assets

the client loads assets directly from its installation. every build and release
package includes the files listed in `resources.txt`; startup does not download
or cache them.

| directory | contents |
| --- | --- |
| `ui/` | app icon, title and menu artwork |
| `textures/` | blocks, items and mining effects |
| `sounds/` | mining, movement and item sounds |
| `fonts/` | interface fonts |
| `data/` | gameplay tables and help pages |

builds copy these files beside the executable, or into `Contents/Resources` in
the macos app. installed linux and windows packages use `share/prisoncpp` beside
`bin`. paths are resolved from the executable, so launching from another
working directory works too. subdirectories such as `sounds/prison/` are kept.

to add an asset, add its relative filename to `resources.txt` and rebuild.
building or packaging fails if a listed file is missing. only listed runtime
files are copied, along with the resource list, credits and packaged licenses.

run `python3 tools/check_data.py .` after editing gameplay data.
in the shared `client/`, `assets/`, `site/` workspace, run
`python3 tools/sync_workspace.py` to export the same resources and rebuild the
website. `assets/` is a hosting export; edit originals in the client. the export
keeps its own managed file list, removes retired files from that list, and leaves
hosting configuration and unrelated files alone. `--check` reports drift without
writing. the client's installed assets still come from its own repository.

`source_systems.json`, `native_economy.json` and `native_content.json` must carry
`data_revision: 2` together; older or mixed revisions are rejected. data changes
ship with the client package.

`--assets PATH` loads another local asset folder. use `--assets "$PWD"` from the
repository root to preview source files without rebuilding. graphical and
headless launches use the same asset paths.

older asset caches are unused. saves and settings remain in the per-user data
directory. the optional [help API](HELP_API.md) is configured separately and is
disabled unless `PRISON_HELP_URL` is set.
