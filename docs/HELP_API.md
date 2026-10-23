# help API

`/help`, `/wiki`, `/info` and `/mods` share the guide. normal asset updates supply
`data/help.json`; an optional API can replace its articles and command descriptions.
set `PRISON_HELP_URL=https://your-host/api` before launching to enable it.
`https://prisontest.com/api` is an example, not a configured service.

serve a public, read-only GET returning status 200 and `Content-Type: application/json`.
a static json response is enough:

```json
{
  "version": 1,
  "title": "Help",
  "sections": [
    {
      "id": "items",
      "title": "Items",
      "pages": [
        {
          "id": "rename",
          "title": "Rename Tokens",
          "body": ["Hold your pickaxe or pouch and use /rename."],
          "keywords": ["names", "colors"]
        }
      ]
    }
  ],
  "commands": [
    {"command": "rename", "description": "Rename the held item with one token."}
  ]
}
```

## limits and local behavior

- version must be 1. `sections` and `commands` must be arrays.
- at most 32 sections, 256 pages and 256 command descriptions.
- section titles allow 80 bytes. page ids are unique, nonempty and at most 64
  bytes; titles allow 100 bytes. strings use UTF-8.
- each page has a body string or up to 128 paragraphs, totaling at most 8,192
  bytes. optional keywords allow 32 strings of up to 80 bytes each.
- command names allow 64 bytes and descriptions allow 2,048 bytes. duplicate
  command descriptions are rejected. use canonical command names.
- control characters other than tabs, carriage returns and newlines are rejected.
- local command categories, syntax, aliases and developer visibility are retained.
  unknown command overrides are ignored; the API cannot add executable commands.

the client validates the complete response before applying it. requests have a
128 KiB limit, a three-second connection timeout, a ten-second total timeout and
a fifteen-minute refresh interval. redirects are not followed; HTTPS certificates
are verified. failures keep the current document.

valid responses are cached in `help-cache.json` beside the native saves directory.
the cache records its endpoint and is loaded only for that same endpoint. with
`PRISON_HELP_URL` unset, help comes from the selected asset pack.

for local preview, serve only the data directory:

```sh
python3 -m http.server 8765 --bind 127.0.0.1 --directory data
```

then launch with `PRISON_HELP_URL=http://127.0.0.1:8765/help.json ./run.sh`.
plain HTTP is accepted only for this explicit loopback address. validation and
cache checks live in `tests/help_test.cpp`; the renderer uses plain text.
