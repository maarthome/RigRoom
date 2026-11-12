# Changelog

## Unreleased

- LV2 GUIs that need direct access to the plugin (instance-access/data-access, e.g. DuskVerb and other JUCE-style GUIs) now open inside RigRoom instead of the helper process, where they failed with "Data or instance access missing". X11 GUIs opened inside RigRoom are embedded directly and also get these features.
- Fixed plugin GUIs in the helper failing to appear (BadWindow errors): the plugin's area now exists on the X server before the plugin creates its window in it.
- An X error about a plugin window that just went away (BadWindow) no longer ends RigRoom or the plugin GUI; it is logged and ignored.
- Fixed plugin GUIs (e.g. Ouraverb) growing with every zoom until the screen zoom hit its limit: the plugin is created in its own area below the bar, and a later size change is only shown, not zoomed to again.
- Less flicker when a plugin GUI opens: the screen zoom is changed only when it really differs, and only after the plugin has settled on its size.
- The on-screen keyboard has a BACK key, which leaves the field as it was (like Escape). Number fields and parameter values get a large number pad down the side of the screen away from the control, so the control and its value stay visible.
- Plugin GUIs that make their window larger than they announced are no longer cut off: the plugin area follows the plugin's real size.
- Tapping a parameter value opens the on-screen keyboard straight away, with the value in a large entry line at its top; type the new value and press Enter. The small window with arrows is gone.
- Plugin GUIs fill the whole screen in black, with the plugin centred and its name in large letters in the bar.
- Click an empty preset slot to start a new preset there; Save keeps it without asking for a name. Right-click an empty slot to copy the current preset into it. The separate New Preset item is gone; Ctrl+N starts a new preset in the first free slot of the bank.
- Preset names no longer have to be unique (the slot, e.g. 05B, tells them apart) and can use any characters, such as ä and ö.
- Save As is now Save Copy To (Ctrl+Shift+S): it puts the current board, edits included, into a free slot and leaves the original as it was saved. Duplicate and the new Move pick their target in the bank grid.
- The bank grid is larger and easier to read, and moving, duplicating, renaming and deleting there keep it open in place. Moves can be undone (Ctrl+Z); deleting still asks first.
- Deleting the loaded preset clears the board and leaves the slot empty, instead of loading the next preset.
- The bank number is now a button that opens the bank grid; right-click it to name the bank.
- A 32-frame buffer can be picked in Settings. If the audio interface doesn't accept a buffer size, the setting shows the size actually in use.
- Text size can be set in Settings (85% to 200%) and applies the next time RigRoom starts.
- A plugin's saved presets are listed under Plugin Presets > Load Saved, so one click loads them.
- Plugin Presets > Plugin's Own Presets lists the presets a plugin ships and loads them: LV2 presets, VST3 programs and .vstpreset files, and CLAP presets found through preset discovery.
- Drop-down lists and selection lists use the dark theme instead of a near-white background.
- On small screens, such as a Raspberry Pi touchscreen, the main window fills the screen and scrolls when its controls need more room, Settings tabs scroll, and dialogs are kept on screen.
- The whole interface scales to fit small screens automatically. Settings > Screen Scale can set it by hand (40% to 200%); it applies the next time RigRoom starts.
- Plugin windows larger than the screen are fitted to it: RigRoom asks VST3 and CLAP plugins to draw at a smaller scale, resizes plugins that allow it, and lets fixed-size plugin windows scroll.
- Every plugin window has a Close button at the top, so it can be closed on a screen without a window manager. LV2 X11 GUIs (such as Dragonfly) are started at a smaller LV2 scale factor when they are larger than the screen, never grow past it, and get arrow buttons to pan over a GUI that still does not fit.
- A plugin window fills the screen when it opens: RigRoom zooms the whole X screen (xrandr) so the plugin fits the panel exactly, for any plugin, and restores the previous zoom when the window closes.
- Parameters can open as a full-screen page: the main screen shows only the board, and double-tapping a block opens its parameters (BACK returns). Settings > GUI switches between this and the deck below the board.
- Parameter controls are arranged in rows of a set number (Settings > GUI > Control Buttons per Row, 5 by default). Text Size and Screen Scale moved to the new GUI tab.
- The signal path can wrap into rows: when it is wider than the screen it continues at the left of the next row, like lines of text (Settings > GUI > Signal Chain).
- Settings > GUI also sets Blocks per Row, Plugin Block Size, and the sizes of the bank/presets/scenes buttons, the top toolbar and the board's corner controls. No size can push a part off the screen: it shrinks to what fits.
- Wrapped rows of plugin blocks after the first start at the left edge, under the input block.
- Blocks per Row counts System Input and System Output; the output opens a row of its own when the last row is full. Default Columns is no longer in the Audio settings.
- With a set Blocks per Row the blocks fill the board: rows sit close together and the blocks grow to fill its width or height (zoom up to 300%). No horizontal scroll bar in the wrapped layout.
- The preset name moved to the middle of the status line; the SPLIT tool sits at the left of the board's bottom row of controls.
- In the wrapped layout the board has exactly as many columns as are in use (one is added when inserting), so System Output follows the last block at the normal spacing; the zoom and column controls are hidden there.
- A long press on a block opens its menu, like a right-click.
- The plugin list opened from a gap on the board has BACK and SPLIT HERE buttons, scrolls with a finger and has a wider scroll bar. In the wrapped layout the SPLIT tool is no longer on the board, giving the blocks more room.
- Plugin windows have BACK at the top left (was Close at the right).
- On-screen keyboard: types directly into the field (values were not taken), keeps its size inside dialogs, and opens only when a field is tapped.
- Settings > GUI: Control Buttons per Row is a list near the top; Control Button Size sets the size of a plugin's control buttons.
- The Settings window is 1.5 times larger by default (Settings > GUI > Settings Window).
- An on-screen keyboard appears when a text or number field is tapped; the field's window moves up so the keyboard does not cover it (Settings > GUI > On-screen Keyboard).
- Number fields have large up/down buttons; tapping them changes the value without opening the keyboard (only tapping the number does).

## 0.13.0 - 2026-10-01

- Presets and plugin presets keep a VST3 plugin's full settings, such as the amp picked in AmpliTube, not only its parameters. Scenes still switch only parameters.
- A VST3 plugin's window follows changes made in RigRoom (controls, MIDI, scenes), and edits made in the plugin window mark the preset as changed.
- VST3 plugins show only the parameters meant for the host; many plugins also list ~2000 internal "MIDI CC" entries, which made them slow to add and their parameter list unusable.
- Generic plugin slots such as "Param 1" show their new name once you assign a control to them in the plugin.
- Slow plugins (Windows plugins through Wine) show a loading card instead of RigRoom just freezing, and at startup the window opens before the last preset loads.
- The AppImage is now a standard AppImage: it needs no libfuse2, no longer copies itself to /tmp at start, and works with AppImage tools.
- Fixed: the editor of some Windows VST3 plugins (vstbridge) opened as a blank window.
- Fixed (AppImage): native plugins using the system's libcurl, such as Amp Locker, failed to load.
- Fixed (AppImage): TONE3000 images didn't show on distros other than Ubuntu.
- Fixed (AppImage): RigRoom didn't start on systems without JACK.
- Fixed: RigRoom could crash on exit after a Windows plugin's bridge had stopped.

## 0.12.0 - 2026-09-21

- RigRoom starts much faster: LV2 plugin details are remembered between runs and read again only for plugins that were installed or updated. "Rescan Plugins Now" still reads every plugin.
- The NAM block's "Browse TONE3000" button is now "Browse Captures" (and "Browse IRs" for impulse responses), since it opens your own library as well as TONE3000.
- The block inspector keeps the height you drag it to, also after RigRoom exits unexpectedly.
- The TONE3000 browser (for NAM captures and impulse responses) is rebuilt to be fast and to match the plugin browser. Searching and scrolling no longer stall: results load in the background and more of them appear as you scroll, and a thin bar above the list shows while anything is loading. The previous results stay visible while a new search runs. Images, results and capture lists are remembered, so reopening the browser or going back from a creator is instant. Downloads run in the background with a progress bar, so you can keep browsing, and errors show up in the list with a retry instead of a pop-up.
- New filters in the TONE3000 browser: pick one or more gear types (for example only Amp, or Amp and Pedal) and one or more tags (Clean, Crunch, High gain, Metal and more, or any tag of your own). Clicking a tag on a capture filters by it. These filters also work for impulse responses, with types Cab, Pedal, Outboard and Space.
- The TONE3000 browser has a sidebar with Trending, Newest, Most downloaded, Creators, TONE3000 favorites (the tones you star, saved on your TONE3000 account) and your saved searches.
- New capture library: the browser opens on a Library tab with the captures and impulse responses you chose to keep, one row per file, filed in folders of your own ("Imported" to start with; add, rename and delete folders in the sidebar and drag captures onto them). Add captures from the TONE3000 tab ("Add to Library", with a choice of folder, or "Add all"), from your own files or folders, or pick from earlier downloads; adding several at once lets you pick the folder and give them all tags, type, tone, make, model and a rating. Files from TONE3000 know their tone and creator and show its picture and description; group the list by tone, creator or folder. Each file gets tags from its own details (its type always from the file itself) and from TONE3000, filtered with the Type, Tone, Brand, My tags and Tech chips. You can rename a file, add your own tags, rate it, write notes, and correct its gear type, make, model and tone; select several captures to change their folder, pack, rating, gear or tags together (applied when you press Apply). Your own files can be gathered into packs with a name, creator and description, and then show together like the variants of a TONE3000 tone. Recently used and Top rated live in the sidebar; rate captures with stars to keep your best ones at hand. A capture added from TONE3000 shows only the files you added; "Show N more from this tone" reveals the tone's other captures to preview or add. Captures on this computer load and preview instantly, and an "In library" badge on the TONE3000 tab opens the capture in your library.
- Fixed: the TONE3000 browser listed only a tone's A2 captures when no NAM filter was set (for example in Favorites), and said none matched for tones with only A1 or custom captures. It now lists every capture of the tone.

- Fixed: Windows plugins bridged with yabridge did not show up in the plugin browser. VST3 folders are now searched through their subfolders, so plugins grouped by vendor (as yabridge does) are found, and a plugin offered in more than one folder is listed once.

## 0.11.1 - 2026-09-20

- Fixed: RigRoom crashed on startup if any installed LV2 bundle was missing its name; such a plugin is now listed by its URI instead. Plugins and ports with incomplete metadata no longer bring the whole application down.
- Crash reports record the version and build ID of the binary that produced them.

## 0.11.0 - 2026-09-20

- Secure Secret Service storage for user-provided TONE3000 API keys.
- Reproducible VST3 submodule checkout, safer plugin rescan and LV2 world lifetime.
- Ubuntu 22.04 AppImage build baseline.
- Preset library in numbered banks of four (01A–32D) with named banks, a grid of all banks (drag to move or swap), and a preset panel: bank selector, A–D footswitch tiles, scenes and Save.
- Scenes: up to 8 per preset switching block on/off, all parameter values (right-click a knob to keep it the same in all scenes) and a scene level without reloading plugins (no audio gap). Blocks changed by scenes get an "S" badge.
- MIDI-controlled parameters no longer fight with scenes: learning a CC keeps that parameter the same in all scenes, and each assignment chooses whether the controller waits until it reaches the stored value (default) or takes over at once, as a pedal should. Knob labels show "=" and "CC n" marks.
- Previous/next preset can be limited to the shown bank (Settings > MIDI), for controllers that have bank up/down; applies to the buttons and keyboard too.
- MIDI input: Program Change for presets or scenes, global CC commands (scene select by value, Scene 1–8, previous/next, bank, A–D) with Learn, and per-preset MIDI Learn for block on/off and expression pedals.
- Click-free block on/off, canvas auto-fit with animated zoom, and a canvas label showing the loaded preset and scene.
- Insert between blocks: markers between blocks, before splits, after merges and at path ends make room by sliding blocks within the lane, else later blocks right into a free column (or earlier blocks left when the right side is full) in every lane; splits and merges get a marker on each side, and the drag preview slides splits and merges along with the blocks, without adding columns or changing the signal path; parallel paths widen when a block is added inside them. Live preview while dragging.
- Plugin pictures (experimental, off by default): opens each plugin's own GUI on a hidden display (Xvfb or kwin_wayland), photographs it and shows it in the browser's info panel; click for a full-size view. X11, Gtk and CLAP GUIs are all covered. Run from Settings > Plugins & Formats: one button plus a drop-down choosing what it covers (never tried / also the failed ones / everything again), with the count on the button, progress while it runs and a Cancel button that stops after the current plugin. Background, lowest priority, one process per plugin so a crash costs only that picture; a separate box turns on doing new plugins after a scan. Plugins without a window, or whose GUI will not open, are recorded and skipped by later runs.
- Settings pages rebuilt to one pattern: long explanations moved behind an "i" button on each section heading, headings above the frame instead of across it, one hint colour, left-aligned labels in a shared column, and a hand-drawn icon set for the tabs.
- New plugin browser: instant search with ranking, sidebar (All / Favorites / Recently used / categories), format chips, info panel with picture and full metadata (the list itself shows category cards); drag plugins onto the board; "＋ Plugins" (Ctrl+B) keeps it open next to the board. Better category detection and a CLAP scan cache.
- TONE3000: creator pages (click a name) and creator search; impulse responses browsable and previewable directly in IR loader blocks.
- Fixed: TONE3000 previews could overwrite a loaded file; capture variants of different tones could overwrite each other in the cache.
- Fixed: toggling a block did not mark the preset as edited; built-in Bypass blocks reloaded as missing plugins; MIDI ports could appear in the audio device lists.
