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
  They are stored as `home_video_enabled`, `home_video_path` and
  `home_video_sound` in `[shell_menu]`.
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

## Game folders

Games and apps on Home's Games row can be grouped into folders.

- **Using them:** a game's Options menu has *Add to Folder* (and *Remove
  from Folder* for a game in one). It opens a native menu of the folders,
  with *New Folder…* (the system keyboard asks for the name) and *Manage
  Folders* (rename, delete). A folder is a tile in the row after the system's
  own tiles; the games in it leave the row. Options on a folder's tile
  renames or deletes it. Down on a folder's tile, or pressing it, shows the
  folder's games below it as the Game Library's native grid; they launch
  and have their Options menu as usual, and Circle goes back up. Deleting a
  folder only puts its games back in the row.
- **Settings:** *Appearance → System customize → Game tiles → Game folders*
  (`home_folders` in `[shell_menu]`, on by default) and *Manage game
  folders*, which lists every folder to rename or delete, also while the
  folders are switched off. The folders themselves are kept in
  `/data/AniketosHEN/home_folders.json`.
- **How it works:** `home_folders.cpp` adds the Options entries
  (`hook_launch.cpp`) and shows the menus and prompt (`system_dialogs.cpp`).
  The query hook of `home_tabs.cpp` leaves the folders' titles out of the
  Games row. The script in Home (`home_inject_js.inc`) adds the folder tiles
  to the row's data and reads the folders through Home's registry module,
  whose `Get` is hooked for one key. A folder tile's hub is the Game Library
  with the folder in its parameters; the script put into that library
  (`library_inject_js.inc`) marks its lists for the query hook, which keeps
  only the folder's titles, and leaves out its tabs. Everything is found by
  name at run time; the system's own app database is only read, never
  written.
- **Safety:** with the setting off, or `no_home_js` in `/data/AniketosHEN`,
  Home and the Game Library stay as the system has them.

## Favorites tab

A game's Options menu has *Add to Favorites* (*Remove from Favorites* once
it is one). The favourites are a tab of their own after Emulators, captioned
with a heart, holding a row of those games; they stay in the Games row too.
The tab is there while it holds a game: Home refreshes once when the first
game is added and when the last one is removed. L1 and R1 move through all
the tabs (Games, Media, Emulators, Favorites). *Appearance → System
customize → Game tiles → Favorites tab* (`home_favorites`) switches it off.
The list is kept with the folders in `/data/AniketosHEN/home_folders.json`;
the tab is made the same way as Emulators (`home_inject_js.inc` adds the
space, `home_tabs.cpp` gives its query the favourites' title ids).

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
