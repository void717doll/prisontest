# prison mining sounds

these samples are restored from the reference `prison` checkout's
`mods/apis/default/sounds/`. the native client uses the same stone sound families
as `mods/core/prisontest/items/nodes.lua` and `default/functions.lua`:

- `default_dig_cracky.1.wav` through `.3.wav`: digging, gain 0.35.
- `default_hard_footstep.1.wav` through `.3.wav`: completed stone break, gain 1.0.

source credits from `mods/apis/default/README.txt`:

- crack samples: Benboncan, [original recording](https://freesound.org/people/Benboncan/sounds/71823/).
- stone impact samples: Erdie, [original recording](https://freesound.org/people/Erdie/sounds/41579/).
- both families: [creative commons attribution 3.0](https://creativecommons.org/licenses/by/3.0/).

the source OGG variants were decoded to mono, 44.1 kHz, signed 16-bit PCM WAV.
no trimming, pitch change or additional effects were applied. conversion used:

```sh
ffmpeg -i source.ogg -map_metadata -1 -ac 1 -ar 44100 -c:a pcm_s16le output.wav
```

PCM keeps playback independent of compressed-audio decoders. the native client
preloads the six samples and mixes at most eight voices, with no per-hit loading
or allocation. rapid block breaks are limited to one sound per 60 ms, and area
effects/automining do not play a sound for every block.
