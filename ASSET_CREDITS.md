# asset credits

the scenery textures in `textures/luanti` were copied unchanged from lunarchy's
`games/devtest/mods/basenodes/textures`, including its `dirt_with_grass` subfolder.
`default_dirt.png` comes from `games/devtest/mods/unittests/textures`, which
replaces the marked dirt placeholder when Luanti loads devtest.
these are Luanti textures, licensed under CC BY-SA 3.0; the upstream
[media license and attribution](textures/luanti/LICENSE.txt) are included.
the renderer tints stone for metal and sand for lamps, and layers grass over dirt.

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
