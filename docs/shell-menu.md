# Shell Menu

The Shell Menu is an overlay menu inside SceShellUI, opened with **L1 + R3**
on the home screen — the PS5 counterpart of a PS3 VSH menu. It is AniketosHEN's
own addition to the OnionHEN tree it is forked from.

## What it provides

- Console information and live telemetry (temperatures, fan duty, RAM, IP).
- Power actions: refresh Home, restart ShellUI, Rest Mode, reboot, power off,
  unload the HEN stack.
- Fan threshold, DualSense light-bar colours and animations, console buzzer and
  power-LED tests.
- Three menu styles (Akari, Anomaly, Apparition), colours,
  gradients, animations, scale and position.
- Shell appearance: native background colour/animation/image, tile focus
  outline, panel tint, home-screen clock and date.
- A home-screen OSD (on by default), and the settings of the in-game monitor
  bar.

## Where the code lives

| Path | Content |
|---|---|
| `source/shellui/src/aniketos_shell/` | menu lifecycle, input, telemetry, the menu frontend (`akari_menu*`) |
| `source/shellui/src/aniketos_ui/` | PUI primitives and menu style |
| `source/shellui/src/aniketos_theme/` | native shell appearance (background, tiles, notifications, panels) |
| `source/shellui/src/controller/`, `controller_tools.cpp` | DualSense reader and light-bar control |
| `source/shellui/src/shell_clock.cpp` | home-screen clock and date |
| `source/libonion_settings/include/onion/shell_menu_settings.def` | every setting the menu owns |

Its directories and namespaces are named `aniketos_*`, `AniketosShell` and
`AniketosTheme`.

## How it is wired into this tree

| Hook point | Call |
|---|---|
| `prx.cpp` `main()` after the Mono images load | `AniketosShell::Initialize()`; failure only disables the menu |
| `prx.cpp` `install_hooks()` | `Notifications`, `HomeTiles`, `SharedUI`, `ShellClock` `Install()` |
| `prx.cpp` after hooks are published | telemetry worker, L1+R3 pad reader, controller manager |
| `prx_overlay.cpp` `OnRender_Hook` | per-frame ticks; the tile pass runs after the native frame |
| `hook_boot.cpp` `GetData_hook` | while the menu is open, ShellUI receives no controller input |

## What was adapted

- **Settings.** The 120-odd fields are declared once in
  `shell_menu_settings.def`; the `Settings` struct, the parser and the
  serializer are generated from it and stored under `[shell_menu]` in
  `config.ini`. The INI parser's capacity was raised from 100 to 256 keys.
- **Overlay settings.** The home-screen OSD uses `home_osd_*`, and the menu's
  "performance overlay" page edits the existing in-game monitor bar
  (`[overlay]`), so nothing is drawn twice.
- **Fan.** The daemon owns the fan in this tree. The menu forwards to
  the same IPC command as the Toolbox instead of running a second fan loop
  inside SceShellUI.
- **Daemon command.** `BREW_RESTART_SHELLUI` was added to the crit daemon.
- **Runtime symbols.** Mono reflection helpers and the ICC buzzer/LED exports
  are resolved by name at start-up; all are optional and null-checked.
- **Translations.** 317 strings were merged into the 14 locale catalogs
  (Thai falls back to English for them).
- **Circular home tiles.** The two Hermes-bytecode edits (tile radius and
  content mask) were moved into this tree's HomeUI patch routine as
  `homeui_home_tiles.inc`. They are located by instruction structure and are
  applied only when exactly one site matches.
- **Defaults.** The menu starts with the Apparition skin and the red global
  theme.
- **Logging.** SceShellUI now writes `/data/AniketosHEN/AniketosHEN_shellui.log`.

## Home video background

*Appearance → System customize → Home Video Background* plays a looping video
behind the Home screen.

- **Adding videos:** copy `.mp4` / `.m4v` files (H.264 video) to
  `/data/AniketosHEN/ShellUI/videos` (over FTP, for example), or to
  `AniketosHEN/videos` on a USB drive, then use *Refresh video list*. The
  folder is created on first start.
- **Settings:** on/off, which video, and whether its sound is played (off by
  default; with sound on, the console ducks its own music as for any video).
  *Video volume %* sets how loud it plays (the player's own scale, 0 to 100,
  the one React Native's video view uses); 0 counts as sound off, and turning
  the volume up turns the sound on. They are stored as
  `home_video_enabled`, `home_video_path`, `home_video_sound` and
  `home_video_volume` in `[shell_menu]`.
- **Effects:** five sliders, 0 (off) to 100, lay full-screen layers over the
  video or picture, under the particles and the rest of Home, and apply as
  they move: *Background blur %* (a PUI `BackgroundBlurEffect` on a clear
  panel, which blurs what lies beneath it), *Background dimming %* (black,
  up to 85 %), *Background haze %* (black too, up to 50 %; both panels
  are kept out of the system-panel theme, which would tint them), *Vignette %*
  and *Darken top and bottom %* (black gradients embedded in the module,
  stretched over the screen). A layer exists only while its slider is above
  0. Stored as `home_bg_blur`, `home_bg_dim`, `home_bg_haze`,
  `home_bg_vignette` and `home_bg_edges`.
- **How it works:** the console's own `Sce.Vsh.Video.VideoPlayer` and PUI
  `VideoView` are created by name and placed under Home's React tree, so the
  video fades and moves with Home. The player core cannot open files from the
  data folder but can stream, so `home_video.cpp` serves the chosen file from
  an HTTP server bound to `127.0.0.1` on a system-assigned port. No system
  file is changed.
- **Safety:** playback pauses while Home is hidden. If two starts in a row
  never reach playback (ShellUI went down), the video is left off until it is
  switched on again in the menu. A file named `no_home_video` in
  `/data/AniketosHEN` disables the feature regardless of settings.

## Game tile shape

*Appearance → System customize → Game tiles → Rounded game tiles* replaces
Sony's rounded squares on Home, and *Tile rounding %* sets how round they
are: 0 is square, about 30 is Sony's own corner, 100 (the default) a full
circle. Home reloads with the new shape shortly after the slider stops, so
holding a direction reloads it once; moving the slider also turns the
rounded tiles on.

- **Settings:** `shell_tiles_circular` (on/off) and
  `shell_tiles_corner_radius` (percent) in `[shell_menu]`. The PS5 case
  mockup, or the customization switch off, keeps the stock shape.
- **How it works:** `homeui_home_tiles.inc` edits two constants in HomeUI's
  Hermes bytecode as the bundle loads: the 106px icon's corner radius and
  the 168px focus frame's, scaled by Sony's own factor, and clips the
  artwork to that corner. The focus outline drawn by `home_tiles.cpp` takes
  the same radius.

## Bookmark tab

A game's Options menu has *Add to Bookmark*. The first time, the system
keyboard asks for a name: the bookmark becomes a tab of its own after
Emulators, captioned with a star and that name, holding a row of the games
added to it (they stay in the Games row too). After that the entry reads
*Add to <name>*, and *Remove from <name>* for a game already in it. One
bookmark for now; removing its last game removes it. L1 and R1 move through
all the tabs (Games, Media, Emulators, the bookmark).

- **Settings:** *Appearance → System customize → Game tiles → Bookmark tab*
  (`home_bookmark` in `[shell_menu]`, on by default) and *Rename bookmark*.
  The bookmark is kept in `/data/AniketosHEN/home_bookmark.json`.
- **How it works:** `home_bookmark.cpp` adds the Options entries
  (`hook_launch.cpp`) and asks for the name (`system_dialogs.cpp`). The tab
  is made the same way as Emulators: the script in Home
  (`home_inject_js.inc`) adds the space, and the query hook of
  `home_tabs.cpp` gives its list the bookmark's title ids. Home is reloaded
  when the tab comes, goes or is renamed; while it is there, its row is
  fetched again when it changes (the script follows a revision read through
  Home's registry module). The system's own app database is only read.
- **Safety:** with the setting off, or `no_home_js` in `/data/AniketosHEN`,
  Home stays as the system has it.

## More on the Information screen

A game's or app's *Options → Information* lists, under the system's rows,
rows of AniketosHEN's own, each label starting with a star: Title ID,
Content ID, region (from the Content ID), platform, master version (PS4),
required system software, SDK version (PS5), installation path, the folder a
dumped game is mounted from (its `mount.lnk`), metadata path, install time
and last use. A row whose value the system does not have is left out. Times
are the console's local time.

- **Settings:** it follows *AniketosHEN game options* (`onionhen_game_opts`),
  like the entries in the Options menu.
- **How it works:** the screen is its own React Native app
  (`rnps-content-information`, NPXS40017), started anew for every title with
  `pscontentinfo:info?titleId=...`. `home_js.cpp` evaluates
  `content_info_inject_js.inc` before its bundle, with the labels and the
  `/user/app/*/mount.lnk` targets from `content_info.cpp`. The script reads
  the title from the URL the app was opened with (`LinkingPS.initialURL`),
  the values from the app database through the module the screen itself
  uses (`AppInfoOperation.getAppInfo`, keys of `appinfo.db`), and appends
  its rows to the list the screen draws (`infoList`), made from the
  screen's own first row. Nothing is written.
- **Safety:** `no_home_js` in `/data/AniketosHEN` leaves the screen as the
  system has it.

## Glyph key on the system keyboard

The system keyboard gets a key with the PlayStation logo right after Shift,
which gives up a third of its width for it. Pressing it puts the console's
own glyphs (the private-use characters of the system font: buttons, logos,
icons) on the letter keys, one page at a time, and its label shows the page
(`1/8`). Pressing it on the last page, or Shift, @#: or the accent key, brings
the letters back. A glyph key types its character like any letter.

- **Settings:** *Appearance → System customize → Glyph key on keyboard*
  (`osk_glyph_key` in `[shell_menu]`, on by default). It is taken in the
  next time a keyboard opens.
- **How it works:** `osk_glyphs.cpp` hooks the keyboard's own classes in
  `Sce.Vsh.ShellUI.Ime` by name. The key is one more entry in the function
  area's layout list (`FunctionAreaPanel.AddBasedOnLanguageArea`) and one
  more model in its button map, bound by the system like the others
  (`FunctionKeyAreaPresenter.SetBindings`); the system never passes that
  model a press, so the confirm button on the key is taken in PUI's
  `Widget.OnKeyEvent`. A page is put on the keys the
  language has just filled (`MainKeyAreaModel.UpdateKeycodeInfo`): each key
  becomes a copy of the letter there with the glyph as its label and text and
  no key code. The glyph list is `osk_glyph_table.inc`.
- **Safety:** a file `no_osk_glyphs` in `/data/AniketosHEN` leaves the keyboard
  as the system has it. If ShellUI stops within seconds of the key being laid
  out for the first time in a run, the key is left out for the next run.
- **Known limit:** the system's IME engine keeps the glyphs while the text is
  being typed (preedit) but drops them when it confirms the text, so they do
  not reach the field yet.

## Performance

Measured on a PS5 (firmware 11.40) with the built-in profiler, per rendered
frame of a 16.7 ms budget:

| | As ported | Now |
|---|---|---|
| Menu closed, steady cost | ~1.2 ms | ~0.15 ms |
| Stall once a second (process-table scan on the UI thread) | 18-24 ms | gone |
| Entering / leaving a page | 20-27 ms / 14-20 ms | under 1.5 ms |
| One log record written from the UI thread | 5.2 ms | 0.002 ms |
| Slider step or toggle (settings written synchronously) | ~20 ms each | one ~6 ms write after the menu settles |

What changed:

- **Logging off the render thread.** `onion_log_set_async()` hands records to a
  writer thread; the three sinks cost a caller about 5 ms per record on a
  console, and the menu and theme code log on navigation.
- **Cached managed accessors** (`aniketos_ui/mono_fast.hpp`). Property setters
  were looked up by name and re-resolved on every call; colours allocated a
  managed object each time.
- **Write skipping** in `aniketos_ui/pui_primitives.cpp`: panel geometry,
  colours and label text are only written when they changed, with a full
  re-assert every five seconds.
- **No system queries per frame**: the running-title query is sampled at 4 Hz,
  the once-a-second process scan is gone, the clock probes its native label at
  30 Hz and only recolours on animation frames.
- **Deferred settings writes**, an indexed translation table, cached class
  lookups for page transitions.
- The dev tooling builds **Release** (`-Os`) by default.

### Profiler

Create `/data/AniketosHEN/shellmenu_profile` before loading the HEN. Every 300
frames `AniketosHEN_shellui.log` then gets one line of average/maximum
microseconds per section. With profiling on, a file
`/data/AniketosHEN/shellmenu_script` containing `T U D L R X O .` (toggle, d-pad,
Cross, Circle, wait) is replayed as controller input, one step every 12
frames, so the menu can be exercised and measured from a PC.

## Credits

Menu and shell-appearance code: AniketosHEN, GPL-3.0-or-later. Visual and
interaction references:
[Akari](https://github.com/kernaltrap8/Akari) (kernaltrap8; original author
jordywastaken) and [Apparition](https://github.com/CF4x99/Apparition)
(CF4x99). These are PS5/PUI reimplementations of the look;
none of the referenced projects' code or functionality is included.
