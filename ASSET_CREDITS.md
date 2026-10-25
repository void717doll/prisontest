# asset credits

the scenery textures in `textures/luanti` are copied unchanged from the supplied
Minetest Game `default` and `flowers` mods. the renderer applies prisontest's
scenery palette and layers the grass-side overlay over dirt. the source texture
files are unchanged. no generated artwork is used for these scenery materials.

- terrain, wood and lamps: [original authors](textures/luanti/README-default.md)
  and [media license](textures/luanti/LICENSE.txt). most are CC BY-SA 3.0;
  `default_gravel.png` is CC0 1.0, as listed in the original attribution.
- flowers: [original authors](textures/luanti/README-flowers.md) and
  [CC BY-SA 3.0 license](textures/luanti/LICENSE-flowers.txt).

the source attribution files are included with the textures in release packages.

the bundled DejaVu fonts retain their [license](fonts/LICENSE-DejaVu.txt).
the three `stone_break_*.wav` sounds are original procedural effects made by
[the included generator](tools/generate_mining_sounds.py), using no recordings
or third-party samples. [Sound notes](sounds/README.md) describe them.
the native port restores the reference mining samples by Benboncan and Erdie
(CC BY 3.0), decoded from OGG to PCM WAV. [source credits and conversion notes](sounds/prison/README.md)
are included with those assets.
activation, equipment and identification sounds were carried over from the
prisontest reference; their licensing is unchanged. this is not a completed
audio or dependency license audit.

meshes are built by the game's renderer. prisontest remains the gameplay and
balance reference. no new license is assigned to third-party assets here.
