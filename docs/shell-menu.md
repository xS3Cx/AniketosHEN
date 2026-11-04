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
- Four menu styles (Akari, Anomaly, Apparition, and Sweety, the console's
  own PUI pop-up menu on the left), colours, gradients, animations, scale and
  position.
- Shell appearance: native background colour/animation/image, tile focus
  outline, panel tint, system icon colour, home-screen clock and date.
- A home-screen OSD (on by default), and the settings of the in-game monitor
  bar.

## Where the code lives

| Path | Content |
|---|---|
| `source/shellui/src/aniketos_shell/core/` | menu lifecycle (`menu_host`), what the features install and tick (`shell_features`), the profiler |
| `source/shellui/src/aniketos_shell/shell_menu/` | the menu frontend, one translation unit (see [its README](../source/shellui/src/aniketos_shell/shell_menu/README.md)) |
| `source/shellui/src/aniketos_shell/input/` | L1+R3 pad reader, mouse, keyboard capture, on-screen keyboard glyphs |
| `source/shellui/src/aniketos_shell/hardware/` | telemetry, memory, fan, buzzer, power LED, pad speaker |
| `source/shellui/src/aniketos_shell/controller/` | DualSense reader and light-bar control |
| `source/shellui/src/aniketos_shell/home/`, `games/`, `system_ui/`, `toolbox/` | Home features, per-title features, system dialogs, our Toolbox pages |
| `source/shellui/src/aniketos_shell/rn_scripts/` | scripts put into the React Native apps |
| `source/shellui/src/aniketos_ui/` | PUI primitives, menu style, helpers the others share |
| `source/shellui/src/aniketos_theme/` | native shell appearance (background, panels, switches, font) |
| `source/shellui/src/aniketos_theme/tiles/` | tile outline and shape, tab labels |
| `source/shellui/src/aniketos_theme/clock/` | home-screen clock and date |
| `source/libonion_settings/include/onion/shell_menu_settings.def` | every setting the menu owns |

Its directories and namespaces are named `aniketos_*`, `AniketosShell` and
`AniketosTheme`.

## How it is wired into this tree

| Hook point | Call |
|---|---|
| `prx.cpp` `main()` after the Mono images load | `AniketosShell::Initialize()`; failure only disables the menu |
| `prx.cpp` `install_hooks()` | `ShellFeatures::Install()`: `HomeTiles`, `PanelTint`, `ShellClock` and the other features, in order |
| `prx.cpp` after hooks are published | `ShellFeatures::StartWorkers()`: telemetry worker, L1+R3 pad reader, controller manager |
| `prx_overlay.cpp` `OnRender_Hook` | `ShellFeatures::BeforeNativeFrame()` and per-frame ticks; the tile pass runs after the native frame (`AfterNativeFrame`) |
| `hook_boot.cpp` `GetData_hook` | `AniketosShell::FilterPad()`: while the menu is open, ShellUI receives no controller input |

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
  `homeui_tile_patch.inc`. They are located by instruction structure and are
  applied only when exactly one site matches.
- **Defaults.** The menu starts with the Apparition skin and the red global
  theme.
- **Logging.** SceShellUI now writes `/data/AniketosHEN/AniketosHEN_shellui.log`.

## HEN button menu

The HEN button in Home's top bar opens a menu: *HEN Toolbox*, *Install
packages* (the places packages can be in, then the `.pkg` files of each),
*Payloads*, *Connect to the Internet* (a switch) and *Unload HEN*.
*Payloads* lists every payload of the payloads folders (the Toolbox's
Payloads page lists the same ones, in their load order) with whether it is
running; choosing one starts it, or stops it when it runs, and a
notification says so. Auto-start, priority and delay stay on the Toolbox's
page.

- **How it works:** `hen_menu.cpp` builds the rows with the native pop-up
  of `popup_menu.cpp` each time the menu opens. The payloads come from
  the scan the Toolbox page uses (`shellui_scan_payloads`, `toolbox_xml.cpp`);
  starting goes through util and stopping kills the recorded process
  (`onpress_payloads.cpp`), as the Toolbox's switch does.

## Start-up notifications

The two notifications every start shows (*AniketosHEN is starting* and the
welcome) are toasts only: they are not kept in the notification list. Error
and warning notifications are kept, so they can be read later.

- **How it works:** both go to `sceNotificationSend` with its logged flag
  off (`onion_notify_rich_toast` / `onion_notify_try_rich_toast` in
  `libonion_platform/source/notify.c`); every other rich notification keeps
  it on.

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
- **Effects:** four sliders, 0 (off) to 100, lay full-screen layers over the
  video or picture, under the particles and the rest of Home, and apply as
  they move: *Background dimming %* (black, up to 85 %), *Background haze %*
  (black too, up to 50 %; both panels are kept out of the system-panel
  theme, which would tint them), *Vignette %* and *Darken top and bottom %*
  (black gradients embedded in the module, stretched over the screen). A
  layer exists only while its slider is above 0. Stored as `home_bg_dim`,
  `home_bg_haze`, `home_bg_vignette` and `home_bg_edges`.
- **How it works:** the console's own `Sce.Vsh.Video.VideoPlayer` and PUI
  `VideoView` are created by name and placed under Home's React tree, so the
  video fades and moves with Home. The player core cannot open files from the
  data folder but can stream, so `home_video_server.cpp` serves the chosen file from
  an HTTP server bound to `127.0.0.1` on a system-assigned port. No system
  file is changed.
- **Safety:** playback pauses while Home is hidden. If two starts in a row
  never reach playback (ShellUI went down), the video is left off until it is
  switched on again in the menu. A file named `no_home_video` in
  `/data/AniketosHEN` disables the feature regardless of settings.

## Home clock

The custom clock (seconds, 12/24-hour form, colour and size, the date under
it) is drawn by Sony's own clock, in Home and in the Control Center, which
have the same component. The script (`aniketos_shell/rn_scripts/home_inject_js.inc`)
finds the module that exports `Clock` and puts its own component behind that
name: it renders Sony's clock (a View
holding one Text) and swaps in the text with the seconds and a second Text
for the date, in the same View. The time is laid out by React from the text
it really shows, so it no longer jumps at the minute, and the date moves,
fades and hides with the clock. With *System* as the hour format the time is
Sony's own (the time cache's `toTime`) with the seconds added after the
minutes.

- **Settings** come from `ShellClock::ScriptConfig()`: in the script's config
  at the app's start, then every 2 s through its registry module
  (`aniketos.clock`, answered in `home_tabs_store.cpp`), so a change in the
  Shell Menu shows without reloading Home. The JSON is plain ASCII: the
  script executor cuts a source holding other text short.
- **Font sizes**: Sony's Text accepts only the sizes of react-native's
  `FontSizePS` table (any other is a fatal error that restarts Home), so a
  size is taken to the nearest of them; the time keeps Sony's size while the
  setting is at its default.
- **Fallbacks**: an error in the clock code, or in drawing it (a React error
  boundary), leaves Sony's clock as it is. A Home that restarts twice before
  the script reports it running gets no script until ShellUI starts again;
  the script logs Home's JS errors (`ANIKETOS fatal error: ...`).
- **The Control Center** gets the script while the custom clock (or the
  presence dot) is on; it is reloaded once, 4 s after the HEN's hooks are in,
  for the script to get in.
- **Label hooks:** the time Text the script draws has Sony's test id with
  `.aniketos` added (`clock-text.aniketos`). The label hooks in
  `clock_label.cpp` still follow that label (Home's root, the opacity) but
  write nothing into it; any clock without the mark (an app without the
  script, a firmware without that `Clock`) is drawn by the hooks as before.
  A dotted time such as "10.40" counts as a clock only on the label Sony
  marks as the clock, so a game's Information screen keeps its values.

## Game tile shape

*Appearance → System customize → Game tiles → Rounded game tiles* replaces
Sony's rounded squares on Home, and *Tile rounding %* sets how round they
are: 0 is square, about 30 is Sony's own corner, 100 (the default) a full
circle. Home reloads with the new shape shortly after the slider stops, so
holding a direction reloads it once; moving the slider also turns the
rounded tiles on.

- **Settings:** `shell_tiles_circular` (on/off) and
  `shell_tiles_corner_radius` (percent) in `[shell_menu]`. The
  customization switch off keeps the stock shape.
- **How it works:** `homeui_tile_patch.inc` edits two constants in HomeUI's
  Hermes bytecode as the bundle loads: the 106px icon's corner radius and
  the 168px focus frame's, scaled by Sony's own factor, and clips the
  artwork to that corner. The focus outline drawn by `home_tiles.cpp` takes
  the same radius.

## System icon colour

**System Customize → Icon color** colours the console's own interface
icons on Home. It is separate from the Global theme and from the panel tint:
the colour slider sets the icon colour, the animation is Static or Rainbow,
and its speed is adjustable. App and game icons, artwork, avatars and
wallpapers are untouched.

`source/shellui/src/aniketos_theme/icon_color/` finds the PUI type that
exposes `IconFilterColor`, checks that its setter is `void(UIColor)`, and only
then installs a detour. The detour replaces the colour and tracks each object
it sees with the colour the system asked for, so turning the feature off gives
each one that colour back, once. If the setter is never called, the existing
`StyleManager.TryGetComponentTokenColor` hook is the fallback: component
tokens whose key contains `icon` take the chosen colour, preserving alpha.

Settings screen icons are not covered yet (TODO). Those rows expose their
glyph through `MenuListItem.iconElement`, a `Lazy<ImageElement>` whose element
is a value type; writing into the Lazy is unsafe, so the renderer path
(`get_Renderers` / `RenderInRow`) still needs a diagnostic prototype.

## Home tabs

*Home screen → Tabs* lists one row per tab, in the order of the tab bar:
*Games* is *Tab 1*, *Media* is *Tab 2*, and every custom tab is
*Tab 3: Name* or *Tab 4: Name*, with *(hidden)* after a
hidden one. An empty slot offers *Create tab %d*; *Restore tab names and icons*
clears Games' and Media's names, resets every tab's icon to its default and
*Tab icons* switches the icon in front of every caption on or off.

A tab's row opens its detail page, with the tab's icon and name at the top.
Games and Media offer *Rename Games tab* / *Rename Media tab* and *Games tab
icon* / *Media tab icon*; a custom tab offers *Rename tab*, *Tab icon*, *Show
tab* or *Hide tab*, *Move left*, *Move right* and *Delete tab*. Rename and icon
open a system prompt for one system glyph; an empty field restores `U+EFCE`
(controller) and `U+F871` (TV). Create and Rename use the system keyboard;
Delete asks for confirmation and returns the menu to the list of tabs.
Custom tabs default to `U+EFAA` (the Aniketos star). The four-tab limit is
enforced with a notification. Hide, show, delete and reorder keep the no-lost
apps invariant: a title moved to a tab returns to its stock row when no visible
custom tab holds it.

The same tabs appear in a game's Options menu under **★ Copy / Move**, with
*Copy to*, *Move to*, *Remove from* and **New tab...**. The Options menu is the
quickest way to fill a tab: choosing *New tab...* asks for its name and puts
the title in it, copied or moved as chosen.

- **Settings:** `home_tab_games_name` and `home_tab_media_name` (empty: the
  system's name), `home_tab_games_icon`, `home_tab_media_icon` and
  `home_tab_icons` in `[shell_menu]`. The custom tabs, their icons and their
  titles are kept in `/data/AniketosHEN/home_tabs.json`; see
  [home-tabs.md](home-tabs.md).
- **How it works:** Home's script (`rn_scripts/home_inject_js.inc`) adds one
  React space for each visible custom tab after Games and Media, with a content
  limit of 7780 or 7781. The query hook of `home_rows.cpp` recognises
  that limit before looking at the space and gives the list the tab's title
  ids. The Games and Media rows get the titles copied or moved into them and
  lose the moved ones while a visible custom tab holds them. Games and Media
  captions are given to React by the script, so its layout is right from the
  first frame; the text hook (`space_switcher_labels.cpp`) writes the user's
  caption into the labels that turn out to sit in the tab bar.
- **Reloads:** a change of the tab set, a name, an icon, visibility or order
  reloads Home. A change of a tab's titles only bumps a revision that the
  script reads through Home's registry module (`aniketos.home_tabs`) every two
  seconds and turns into a refetch of the rows.
- **Safety:** with `no_home_js` in `/data/AniketosHEN` Home stays as the system
  has it, without custom tabs. A corrupt `home_tabs.json` behaves as empty and
  is logged, never fatal. A leftover `home_bookmark.json` is deleted at
  startup and is not migrated.

## Hidden apps

A game's or app's Options menu has *Hide App*, with the same star as the
other AniketosHEN entries, after *Copy / Move* and before Sony's *Delete*. It
takes the tile out of Home's Games and Media rows and every custom tab
without uninstalling anything and without writing to the system's
app database: the query hook of `home_rows.cpp` adds
`AND (icon.titleId NOT IN (...))` to those queries. The app stays
launchable from Search and the library, which is the safety net.

*Console → Hidden apps* keeps the list:

- *Filter hidden apps* (`home_hide_apps` in `[shell_menu]`, on by default):
  off, every app shows again and the list is kept;
- *Show <name>*: one app goes back to Home;
- *Show all hidden apps*: every app in the list goes back;
- *No hidden apps*: the list is empty.

- **Settings:** `home_hide_apps` in `[shell_menu]`; the list is
  `/data/AniketosHEN/hidden_apps.json` (`{"version":1,"hidden":[...]}`).
- **How it works:** `game_options_menu.cpp` adds the entry at the first of
  `MENU_ID_APPLICATION_DELETE` and `MENU_ID_CHECK_PATCH` it meets, one block
  per menu (a 500 ms gap between bursts of entries tells a new menu apart); the
  URI comes back through the boot hook and `hidden_apps.cpp` records the
  Title ID, writes the file atomically, reloads Home, and shows a toast. The
  name shown in the list is read from `/user/app/<TID>/sce_sys/param.json`
  and cached, so rebuilding the page every frame does not read the disk.
- **Safety:** delete `/data/AniketosHEN/hidden_apps.json` over FTP, or turn
  `home_hide_apps` off, to bring every app back.

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
  `pscontentinfo:info?titleId=...`. `script_injector.cpp` evaluates
  `content_info_inject_js.inc` before its bundle, with the labels and the
  `/user/app/*/mount.lnk` targets from `content_info.cpp`. The script reads
  the title from the URL the app was opened with (`LinkingPS.initialURL`),
  the values from the app database through the module the screen itself
  uses (`AppInfoOperation.getAppInfo`, keys of `appinfo.db`), and appends
  its rows to the list the screen draws (`infoList`), made from the
  screen's own first row. Nothing is written.
- **Safety:** `no_home_js` in `/data/AniketosHEN` leaves the screen as the
  system has it.

## Glyph key and emoji on the system keyboard

The system keyboard gets a key with the PlayStation logo right after Shift,
which gives up a third of its width for it. Pressing it puts the console's
own glyphs (the private-use characters of the system font: buttons, logos,
icons) on the letter keys, one page at a time, and its label shows the page
(`1/8`). Pressing it on the last page, or Shift, @#: or the accent key, brings
the letters back. A glyph key types its character like any letter, and the
glyphs stay in the text once it is confirmed (a tab name, a search, a
message).

Every keyboard also opens with the system's own emoji key and panel
(categories, recently used, skin tones), which the system otherwise keeps
for a few fields such as messages. Password fields are left without it.

- **Settings:** *Appearance → System customize → Glyph key on keyboard*
  (`osk_glyph_key`) and *Emoji on keyboard* (`osk_emoji`), both in
  `[shell_menu]` and on by default. They are taken in the next time a
  keyboard opens.
- **Emoji:** a field asks for its keyboard's options in PUI's
  `TextFieldEditIme.CreateImeOptions`; `osk_glyphs.cpp` adds
  `ImeOptions.EnableUnicodeEmoji` there unless the field is a password
  field (`ImeOptions.PasswordMode`). The keyboard and its engine do the
  rest.
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
  as the system has it, emoji included. If ShellUI stops within seconds of the key being laid
  out for the first time in a run, the key is left out for the next run.
- **Confirming:** the system's IME engine keeps the glyphs only while the
  text is being typed (the preedit). At the confirm it sends that text again
  without them and the field confirms the preedit it holds, so the glyphs
  would be lost. `osk_glyphs.cpp` keeps the field's last preedit with glyphs
  (`TextField.UpdatePreedit`) and, when the confirm is that text without
  them, hands the field the preedit with its glyphs just before the confirm
  goes through (`TextField.ConfirmPreedit`). A preedit that loses its glyphs
  with no confirm in the same frame is the user deleting them, and is left
  as it is.

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
- **Cached managed accessors** (`aniketos_ui/mono_property_cache.hpp`). Property setters
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
