# asset hosting

the native client checks `https://assets.prisontest.com/` on each normal launch.
publish the existing filenames under these directories:

| cache folder | path on the host |
| --- | --- |
| `ui/` | `/ui/` |
| `textures/` | `/textures/` |
| `sounds/` | `/sounds/` |
| `fonts/` | `/fonts/` |
| `data/` | `/data/` |

the renderer, audio loader, cache and downloads use these same relative paths, including
`https://assets.prisontest.com/fonts/DejaVuSans-Bold.ttf` and
`https://assets.prisontest.com/ui/prisontest_header.png`. download errors show
the requested URL. assets are excluded from git and release packages except for
`ui/prisontest_icon.png`, which is kept for the app and desktop launcher.

keep subdirectories, including `sounds/prison/`. the host needs HTTPS and direct
file responses. no API, directory listing, zip archive or server-side manifest
is required. the repository's `resources.txt` lists the files known to that build;
the client fetches `.png`, `.ttf`, `.wav`, `.ogg` and `.json` files from the
directories above. `.import` files and artwork notes are not fetched.
adding a filename requires updating `resources.txt` and rebuilding the client.

serve ETag or Last-Modified headers to avoid transferring unchanged files.
the client sends conditional GET requests and reuses a file on HTTP 304.
HTTP 200 replaces it only after the full asset set has downloaded and passed
file checks. publish complete sets together, especially related json tables.

run `python3 tools/check_data.py /path/to/asset-pack` before publishing data changes.
publish the `data/` files together. `source_systems.json`,
`native_economy.json` and `native_content.json` must carry `data_revision: 2`.
older or mixed revisions are rejected both in cached packs and new downloads.
the previous complete cache remains available if an update fails.

the runtime checks sizes, json parsing and root types, PNG decoding, font
loading, WAV decoding and Ogg signatures. it also loads the assembled data
tables before selecting them. this does not replace the full balance/data audit.
limits are 16 MiB per download, 4 MiB per json file and 128 MiB per asset set.
connections time out after three seconds; each file gets at most ten seconds.
the complete launch check is bounded to sixty seconds for a first download or
twenty seconds with a usable cache. an error stops the update and keeps the
previous usable copy. HTTPS certificates are verified against system trust or
the application's bundled CA file; redirects are not followed.

if the system resolver cannot find the asset host, the client retries once using
Cloudflare DNS over HTTPS at `https://1.1.1.1/dns-query`. the retry stays within
the original file timeout, and subsequent requests use that resolver for the
rest of the update. both the DNS request and asset connection keep certificate
verification enabled. HTTP errors and proxy lookup failures do not trigger this
retry. this uses [libcurl's built-in resolver](https://curl.se/libcurl/c/CURLOPT_DOH_URL.html)
and requires libcurl 7.62 or newer.

## local files

the asset cache lives beside the save directory, normally
`~/.local/share/prisoncpp/asset-cache/` on linux. windows and macos use the same
parent directory as their native saves. `--save-dir` relocates this cache too.
cached files use the five root folders above. `current.json` selects a complete
pack with an atomic file replacement. published packs stay unchanged so another open client can keep
reading its selected version; failed downloads are discarded.

the last complete cache in this layout works offline. the first launch requires
a complete download; without one, startup exits with the download error.
old packs remain available until the cache is cleared;
with the game closed, deleting only `asset-cache/` makes the next launch download
again. caches from the earlier nested layout are replaced by a fresh download.
saves, settings and the separate help cache are unaffected.

`--assets PATH` selects local files and disables asset downloads. headless launches
use the same downloader and cache as the graphical client unless `--assets` is set.
the separate `PRISON_HELP_URL` setting still controls text-only
help updates; its trusted certificates always come from the application bundle.

