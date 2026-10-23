# Sounds

`stone_break_1.wav`, `stone_break_2.wav` and `stone_break_3.wav` are original
procedurally synthesized stone impacts for this project. No recorded or
third-party samples are used. They combine a short contact, a dry fracture and
small debris taps, with three fixed seeds to vary repeated mining.

Regenerate them with Python 3, using only the standard library:

```sh
python3 tools/generate_mining_sounds.py
```

Each clip is mono, 44.1 kHz, signed 16-bit PCM, lasts 172–188 ms and peaks at
0.65 full scale. They have no loop markers and start/end at zero. these original clips remain available in the asset pack. the native client uses
the reference samples described in [prison/README.md](prison/README.md).
PCM avoids starting a compressed decoder for short, frequent effects.

`activate.ogg`, `equip.ogg` and `identify.ogg` retain their existing provenance.
See [asset credits](../ASSET_CREDITS.md).
