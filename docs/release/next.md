# The next release: what's new

<!-- One entry for each change a player or a mod's author will notice, added in the same piece of work as the
change, in the words the release notes will use: the bold lead says what is now true, the rest says what it was
like before. Say which edition when it isn't both. tools/release/bump.py moves these entries into the release
notes and empties this file. -->

- **The branding switch names the app too.** `RS_REARMED` (Settings and switches…: RS Re-ARMed branding) shows the
  Re-ARMed title logo and app name, in the Dock, the menu, the window title and the launcher's icon; off shows
  the game's own title (`Rocksmith 2014`, `Rocksmith 2014 Learn & Play`). The logo and the window title change at
  the next start; the Dock, menu and launcher names at the next install, so Build & Install again for a new name.
  On the Mac the game now shows as `RS Re-ARMed` in the Dock by default, where before it showed as `Rocksmith2014`;
  Learn & Play's Dock name and the Mac menu bar name are unchanged by default. (Both editions; Learn & Play has no
  launcher: its app's icon is the game.)
- **Render scale above 100% for a cleaner picture:** Render scale, under Advanced in the installer's Settings and
  switches and RENDER SCALE in the game's ADV DISPLAY SETTINGS, goes up to 150% and 200% (`RS_METAL_SCALE`): the
  game draws more pixels, for less shimmer on thin lines and fine textures. Where that is more than the screen has (a
  screen that isn't Retina, or with Full Retina resolution) each frame is scaled down to it (supersampling); on a
  Retina screen without Full Retina, 200% draws the screen's own pixels. Before, it went from 50% to 100%.
