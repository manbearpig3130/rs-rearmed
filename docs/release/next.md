# The next release: what's new

<!-- One entry for each change a player or a mod's author will notice, added in the same piece of work as the
change, in the words the release notes will use: the bold lead says what is now true, the rest says what it was
like before. Say which edition when it isn't both. tools/release/bump.py moves these entries into the release
notes and empties this file. -->

- **RS Re-ARMed's picture switches are in the game's own menu (Mac):** OPTIONS → ADV DISPLAY SETTINGS lists ten of
  them after the game's own rows, from FRAME RATE and RENDERER to ANTI-ALIASING and COLOR, saved where the installer
  saves them, for the next start. The other renderer's rows are greyed, as are those a switch set with `launchctl
  setenv` holds (SET FOR THIS SESSION), and the longer list scrolls, with a scroll bar that can be dragged. Before,
  they could be set in the installer only.
- **Game Mode can be turned on:** RS_GAME_MODE=1, Game Mode in the installer's Settings and switches, makes the game's
  app a game for macOS, which then turns Game Mode on in full screen: the game first for the CPU and GPU, and lower
  Bluetooth latency. It stays off by default, as before, since a plug-in host beside the game can crackle with it.
