# The next release: what's new

<!-- One entry for each change a player or a mod's author will notice, added in the same piece of work as the
change, in the words the release notes will use: the bold lead says what is now true, the rest says what it was
like before. Say which edition when it isn't both. tools/release/bump.py moves these entries into the release
notes and empties this file. -->

- **RS Re-ARMed's picture switches are in the game's own menu (Mac):** OPTIONS → ADV DISPLAY SETTINGS lists them after
  the game's own rows, from FRAME RATE and RENDERER to COLOR and PERFORMANCE OVERLAY, saved where the installer saves
  them; most take effect at once, and a row that can't apply is greyed with why, as is one a switch set with
  `launchctl setenv` holds (SET FOR THIS SESSION). The longer list scrolls, with a scroll bar that can be dragged.
  Before, they could be set in the installer only.
- **A performance overlay:** RS_METAL_HUD=1, Performance overlay in the installer, shows Apple's Metal HUD over the
  game: its frame rate, frame times and GPU memory. Before, there was none.
- **Low-latency presentation:** RS_METAL_DRAWABLES=2, Low-latency presentation in the installer (under Advanced), queues
  two frames for the screen instead of three, so each is shown a refresh sooner. Before, only the Mac edition read it,
  and the installer didn't offer it.
- **Sharpening:** RS_METAL_SHARPEN=N (0-100), Sharpening in the installer and SHARPENING in the game's ADV DISPLAY
  SETTINGS (Mac), sharpens the picture on the screen after any upscaling. Before, there was none.
- **Render scale above 100% for a cleaner picture:** Render scale, under Advanced in the installer's Settings and
  switches and RENDER SCALE in the game's ADV DISPLAY SETTINGS (Mac), goes up to 150% and 200% (`RS_METAL_SCALE`): the
  game draws more pixels than the screen has and scales them down to it (supersampling), for less shimmer on thin lines
  and fine textures. Before, it went from 50% to 100%.
