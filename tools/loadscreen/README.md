# Title-screen logo: "Re-ARMed 2026"

A red graffiti tag, "Re-ARMed 2026", tilted across "REMASTERED" on the title-screen logo.
Optional: at each start the Mac launcher (`tools/play-native/launch.sh`) and the Learn & Play app
(`win-x86/app/lp_main.c`) link the game copy's `static.psarc` to `static.psarc.rearmed` when it is
there (`RS_REARMED=0` for the original logo). The install's own `static.psarc` is never changed. The two
editions have the same logo textures.

Both installs write it straight to the copy, never to Steam's folder: on the Mac `install.sh`, for Learn & Play
`lp_install.py build`. By hand:

```bash
RS_GAME_DIR="$COPY" tools/loadscreen/build.sh "$COPY/static.psarc.rearmed"   # COPY: .../RS Re-ARMed/game or .../learn-play/game
```

| File | What it does |
|---|---|
| `build.sh <out.psarc> [variant]` | Extracts `gfxassets/views/logo_elk_final.dds` and `logo_graded_elk_final.dds` (both used by `titlescreen.gfx`) from `$RS_GAME_DIR`'s `static.psarc` (or `static.psarc.orig`, or a game copy's `static.psarc.original`, if present), tags them, encodes DXT5 with etcpak after the game's original 128-byte DDS header, and repacks. Deterministic |
| `logo.py <static.psarc> <out.psarc>` | The same result with the standard library alone, for a machine without Pillow, numpy, etcpak and Marker Felt (the Linux setup, `linux/install/rs_install.py`): the tag is read from `tag.png`, put over the two textures, and only the 4 x 4 blocks it touches are decoded and encoded again (its own DXT5 encoder), the rest keeping their bytes. About two seconds |
| `tag.png` | The tag alone (variant A), transparent elsewhere: our own artwork, made by `rearmed.py --tag-only tag.png`. `test_logo.py` checks it is what `rearmed.py` draws, where Pillow, numpy and the font are |
| `test_logo.py` | `logo.py`'s test (ctest `loadscreen.logo`): the PNG reader, the blocks, the painting, the tag |
| `rearmed.py` | Draws the tag on a 1024x256 logo PNG. `--variant A\|B\|C` (A, the default: over "REMASTERED"; B also strikes out "2014 EDITION"; C slashes across the whole logo), `--seed N` |
| `../psarc_extract.py`, `../psarc_pack.py` | Read a psarc; rebuild one with entries replaced (other entries' blocks copied byte for byte) |

Needs a Python with Pillow, numpy and etcpak (`RS_PY=...`, or `build.sh` makes a venv in
`$RS_BUILD_DIR/loadscreen-venv`, which needs the network once).
Pillow's own DXT5 encoder smears thin outlines; etcpak keeps the edges clean.
