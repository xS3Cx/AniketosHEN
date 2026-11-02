<p align="center">
  <img src="assets/logo.png" alt="AniketosHEN" height="128" width="128"/>
</p>

<p align="center">
  <b>AniketosHEN</b><br/>
  An all-in-one HEN and Toolbox for PlayStation 5<br/>
  <sub>A fork of <a href="https://github.com/aydencharles/onionHEN">OnionHEN</a>
  by 麒麟/0xp0co and contributors — see <a href="#credits">Credits</a> and <a href="NOTICE.md">NOTICE</a></sub>
</p>


<p align="center">
  <b><a href="#features">Features</a></b>
  ·
  <b><a href="#run">Run</a></b>
  ·
  <b><a href="#shell-menu">Shell Menu</a></b>
  ·
  <b><a href="#build">Build</a></b>
  ·
  <b><a href="#configuration">Configuration</a></b>
  ·
  <b><a href="#coming-from-onionhen">Coming from OnionHEN</a></b>
  ·
  <b><a href="#credits">Credits</a></b>
</p>

---

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="license"/></a>
  <img src="https://img.shields.io/badge/Platform-PlayStation%205-003791?style=flat&logo=playstation" alt="PlayStation 5"/>
  <img src="https://img.shields.io/badge/C-00599C?style=flat&logo=c&logoColor=white" alt="C"/>
  <img src="https://img.shields.io/badge/C++-00599C?style=flat&logo=cplusplus&logoColor=white" alt="C++"/>
  <img src="https://img.shields.io/badge/Build-CMake-064F8C?style=flat&logo=cmake" alt="CMake"/>
</p>

<p align="center">
  AniketosHEN is a modular payload stack for the PS5. One entry ELF prepares
  the system, injects a ShellUI Toolbox,<br/>and then runs overlays, cheats,
  payloads, and background services.
</p>

<p align="center">
  <img src="assets/screenshot_01.png" alt="AniketosHEN on the Home screen: Emulators and Bookmark tabs and the stats overlay"/>
</p>

<p align="center">
  <img src="assets/screenshot_02.png" alt="The HEN button menu: HEN Toolbox, Install packages, Payloads, Internet connection, Unload HEN"/>
</p>

<p align="center">
  <img src="assets/screenshot_03.png" alt="AniketosHEN Toolbox"/>
</p>

<br>

# Features

AniketosHEN is a practical homebrew stack for jailbroken PS5 consoles. It is a
fork of OnionHEN: it keeps OnionHEN's feature set, stores its data under its
own name, takes over an existing OnionHEN installation, and adds features of
its own on top.

- **ShellUI Toolbox** — a settings page injected into the PS5 ShellUI
- **Shell Menu** — an overlay menu on the home screen (**L1 + R3**) for console info, power actions, fan, controller light bar, menu styles and the look of the system interface; see [Shell Menu](#shell-menu)
- **System preparation** — raise privileges, remount filesystems, and block the update partition
- **fSELF / fPKG** — bundled kstuff for homebrew SELF / PKG; loads by default, can be turned off in the Toolbox
- **Plugin runtime** — discovers, validates, starts, stops, and removes descriptor-bearing plugin ELFs
- **PS5 FTP server** — available as the optional external `onionHEN-ftpsrv-plugin`
- **ShadowMount+** — available as the optional external `onionHEN-shadowmountplus-plugin`
- **Remote Play pairing** — enable the native PS5 Remote Play service, generate a pairing PIN, and register a client from the Network section
- **User payload manager** — start and stop user-provided `.elf` payloads, with optional auto-start
- **Game overlay** — an in-game bar for FPS, CPU, GPU, RAM, temperatures, and network info
- **Game Options menu** — Cheats, trophy unlock, **Copy / Move** and **Hide App** on a game's or app's Options menu, with a Hidden apps page that brings hidden tiles back
- **Cheat engine** — local JSON, SHN, MC4, and ShnExt files that can be toggled at runtime
- **DPI v2** — available as the optional external `onionHEN-dpiv2-plugin`
- **Console tools** — account activation, external HDD, Title IDs, fan control, shortcuts, and game options
- **App jailbreak** — allowlisted homebrew can ask the daemon for extra privileges through a sandbox FIFO
- **Resilient runtime** — the critical daemon and utility daemon run apart; the main daemon can restart the utility
- **Shared configuration** — the Toolbox and daemons use one versioned `config.ini`

AniketosHEN does not include a kernel exploit. The first hop still needs an
external `elfldr` on **9021**. After that, AniketosHEN starts its own private
loader on **9020** for later ELF and user-payload launches.

<br>

# Shell Menu

The Shell Menu is AniketosHEN's own overlay menu, drawn inside the PS5 system
interface. It is opened and closed with **L1 + R3** on the home screen. It
belongs to the home screen: while a game or an application is in the
foreground it stays closed and the shortcut is ignored, so it never takes input
away from a title.

**Controls:** D-pad up/down moves, left/right changes a value, **Cross**
selects, **Circle** goes back and closes the menu from its first page. Changes
apply at once and are written to the `[shell_menu]` section of `config.ini`.
Nothing in the menu modifies a system file: every effect is applied to the
running interface by name and is gone when AniketosHEN is unloaded.

### Console Information

A live readout of the console. Rows that a console cannot provide are left out.

| Row | What it shows |
|---|---|
| System name, hardware model | The console's name and its model code (for example `CFI-1216A`) |
| Serial number, IDPS, PSID | Masked by default. *Show serial number and IDs* opens a confirmation that explains what they are before revealing them, every time; they are hidden again when the menu closes |
| PS5 firmware | The system software version as `major.minor` (`11.40`, `13.42`) |
| System kernel version | Kernel revision and build date |
| Processor model, threads | The processor's part number and its thread count |
| Current / base processor frequency | The clock the processor runs at right now (it drops when idle) and its fixed base clock |
| Disc drive | Whether an optical drive is detected |
| Virtual memory on disk | Size of the system's swap space |
| CPU / SoC temperature, fan duty | Live sensor readings |
| RAM (used / total), free RAM | System memory |
| Free disk space | Free and total space of the internal user storage |
| System uptime, local IP | Time since boot and the console's LAN address |

### Console Management

- **Power** — refresh the home screen, restart the system interface, enter
  Rest Mode, reboot, power off.
- **Buzzer** — set the console's beep volume and play its short, long and
  error beeps.
- **Power LED indicator** — choose the front light's colour (white, blue,
  orange) or hand it back to the system.
- **Fan control** — turn on a manual temperature threshold for the fan and set
  it, with the temperatures and fan duty shown beside it. The setting is
  applied by the daemon, the same one the Toolbox uses.
- **Controller** — set the DualSense light bar's colour with one colour
  slider (the controller takes the colour as it moves), run an animation
  on it (breathing, pulse, rainbow, fast spectrum, neon wave), or restore
  system control.
- **Hidden apps** — the apps taken off Home by *Hide App* in their Options
  menu. *Show <name>* puts one back, *Show all hidden apps* puts them all
  back, and *Filter hidden apps* (`home_hide_apps` in `[shell_menu]`, on by
  default) turns the Home filter off without emptying the list.

### OSD

- **Home Screen OSD** — a text readout drawn on the home screen. Each line can
  be switched on its own: interface frame rate, viewport, CPU and SoC
  temperature, fan duty, RAM used and free, free disk space, system software,
  uptime and local IP. Its settings cover the refresh delay, frame-rate
  precision, temperature and RAM units, text size, and the corner it sits in.
- **Performance OSD** — the settings of the in-game monitor bar: on/off,
  background, position, and which of GPU, CPU (optionally every core), memory
  and IP address it shows.

### Appearance

**Global customization** is one switch for a single colour scheme: while it is
on, the themed surfaces (the menu itself, the system background, the focus
outline of game tiles, the clock and panels) take the same
colour and animate in step.
The colour is set with one colour slider, and the animation is one
of Static, Rainbow (solid or gradient), Pulse and Ocean, with an adjustable
speed.

**System Customize** changes how the PS5 interface itself looks:

| Page | What it changes |
|---|---|
| ShellUI Background | The system's own background: its colour and the colour of its glow and particles, each with a colour slider, the glow intensity, animation and speed, the native particle effect, and whether it is forced on the home screen. One action restores the original |
| Home Background | A looping video behind Home (with optional sound), or a still picture, and drifting PlayStation-symbol particles with an adjustable count. Videos (`.mp4` / `.m4v`, H.264) go in `/data/AniketosHEN/ShellUI/videos` or `AniketosHEN/videos` on a USB drive; pictures in `/data/AniketosHEN/ShellUI/background`; particle pictures in `/data/AniketosHEN/ShellUI/particles` |
| Game Tiles | Circular game tiles instead of rounded squares |
| System Focus Outlines | The colour of the focus outline around the selected item, with a colour slider and an animation; it can follow the background colour |
| Clock & Date | The home-screen clock: font size and colour, 12- or 24-hour format, seconds, and an optional date line with its own size, colour, order, month and weekday style, year and separator |
| Console UI Backgrounds | A tint for the system's panels and cards, with strength and opacity |
| Toggle switches | Another look for the system's on/off switches, drawn with the system's own widgets and animated: Indicator, Classic, Metal, Skull, Indicator Light, or the system's own |
| Avatar status dot | A presence dot (online, busy or appearing offline) on the user's avatar on Home and in Control Center, as when signed in to PSN |

**Menu Animations** sets the animation and its speed for the system
background theme.

**Aniketos Customize** changes the Shell Menu itself: one of three menu styles
(Akari, Anomaly, Apparition), its colours (one colour slider for each end), a
gradient and its direction, colour animation, border, scale, width and
on-screen position, text size and weight, background opacity, and whether the
button hints are shown.

Every colour is chosen with one colour slider: a rainbow track with white at
its end, moved with left and right.

### Settings

- **Interface customization** — the master switch for everything the menu
  changes in the look of the interface: background, focus outlines, circular
  tiles, panels, clock, switches, the Home video, picture and particles, and
  the avatar status dot. Turned off, the console looks stock; the settings are
  kept for when it is turned on again. The AniketosHEN button and the
  Emulators tab are not affected.
- **Save / Load / Reset configuration** — write the menu's settings now, read
  them again from `config.ini`, or return every one of them to its default
  (the menu style, light or dark mode, colours and all appearance settings).

Implementation notes, the wiring into the rest of the tree and the performance
measurements are in [docs/shell-menu.md](docs/shell-menu.md).

<br>

# Run

### Requirements

1. A jailbroken PS5
2. An external [PS5 `elfldr`](https://github.com/ps5-payload-dev/elfldr) listening on port **9021** for the first hop

### Load AniketosHEN

1. Run the kernel exploit and start the external `elfldr` service.
2. Send `AniketosHEN.elf` through the loader your exploit host provides.
3. Wait for the utility daemon, `kstuff`, and the main daemon to start, in that order.
4. Open PS5 Settings and enter the AniketosHEN Toolbox.

Startup is sequential. After the first hop, AniketosHEN uses its own
`onion_elfldr.elf` on port **9020** and keeps **9021** only as a fallback.

```text
AniketosHEN.elf → bootstrapper → onion_elfldr.elf (:9020) → util.elf → kstuff.elf → daemon.elf → Toolbox
```

External plugins are discovered and started by the daemon after `kstuff` is
ready.

### FTP server

FTP is provided by the separate
[`onionHEN-ftpsrv-plugin`](https://github.com/OnionBuddies/onionHEN-ftpsrv-plugin).
Install it as `/data/AniketosHEN/plugins/FTPS00001.elf`; AniketosHEN discovers and
starts the plugin, and its dynamic settings page provides enable, port, and
restart controls. The plugin retains upstream `ftpsrv` commands such as
`KILL`, `SELF`, `SCHK`, `MTRW`, and `AUTHID` where supported.

### ShadowMount+

ShadowMount+ is provided by the optional external
[`onionHEN-shadowmountplus-plugin`](https://github.com/OnionBuddies/onionHEN-shadowmountplus-plugin).
Install its descriptor-bearing ELF under `/data/AniketosHEN/plugins/`; the daemon
then owns discovery, start, stop, replacement, and removal. Its dynamic
page provides an immediate scan action. The plugin requires `kstuff` and keeps
its own options under `/data/shadowmount/config.ini`.

The plugins above are separate OnionHEN projects and keep their own names.
They talk to the daemon over the unchanged `/system_tmp/onionhen/ipc/*`
sockets, so plugin ELFs built for OnionHEN load as they are; only the install
folder is now `/data/AniketosHEN/plugins/`.

### DPI (network package installer)

DPI v2 is provided by the optional external
[`onionHEN-dpiv2-plugin`](https://github.com/OnionBuddies/onionHEN-dpiv2-plugin).
Install it as `/data/AniketosHEN/plugins/DPIV00001.elf`; AniketosHEN discovers and
starts it, and its dynamic settings page provides enable, API port, WebUI port,
and restart controls. The browser remains the installer interface, including
chunked uploads, queue management, staged-file reuse, and SSE progress. The
default WebUI URL is `http://<console>:12800`.

### Remote Play

Remote Play pairing is available from **Toolbox → Network → Remote Play**.
The current account must already be activated. If it is not, AniketosHEN blocks
the Remote Play page and directs you to **Toolbox → Account → Account
activation**. Once the account is activated, AniketosHEN enables the Remote Play
registry setting and displays the pairing PIN. Enter that PIN in the official
Remote Play client while the page is open; AniketosHEN uses the native PS5 Remote
Play service to confirm the registered device.

The feature provides pairing and registration only. Video streaming and client
transport remain handled by Sony's native Remote Play service.

### Payloads

Place standalone payloads in:

```text
/data/AniketosHEN/payloads/
```

Only plain `.elf` files are supported. Auto-start can be turned on in the Toolbox;
AniketosHEN remembers that choice with a matching `.auto_start` file next to the ELF.

All `.elf` filenames use the same Payload page, loader, and auto-start flow. A
recorded running instance is left running by later launch and auto-start
requests. AniketosHEN plugins are a separate category and belong in
`/data/AniketosHEN/plugins/`.

### Cheats

Put cheat files in one directory. Names are `TITLEID_VERSION`, with an
optional process and 8-hex source ID:

```text
/data/AniketosHEN/cheats/<TITLE_ID>_<VERSION>[_<PROCESS>][_<SOURCE_ID>].json
/data/AniketosHEN/cheats/<TITLE_ID>_<VERSION>[_<PROCESS>][_<SOURCE_ID>].shn
/data/AniketosHEN/cheats/<TITLE_ID>_<VERSION>[_<PROCESS>][_<SOURCE_ID>].mc4
/data/AniketosHEN/cheats/<TITLE_ID>_<VERSION>[_<PROCESS>][_<SOURCE_ID>].ShnExt
```

`PROCESS` is optional. `SOURCE_ID` is a stable physical-source discriminator.
AniketosHEN loads every compatible source for the title/version/process, so
independent JSON, SHN, MC4 and ShnExt files can coexist. A source with an
explicit process is only used for that process; a source without one is
generic.

Cheats load from disk. If a file changes, AniketosHEN reloads it without restarting the whole stack.

`DOWNLOAD_CHEATS` downloads a cheat catalog ZIP over HTTPS (GitHub or cnb.cool), extracts the `cheats/` tree, and copies `json/`, `shn/`, and `mc4/` files into this directory with their original names. `[cheats] mirror=auto` uses cnb.cool when the UI/system language is Simplified Chinese, otherwise GitHub.

<br>

# Build

### Dependencies

| Dependency | Purpose |
| --- | --- |
| [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) | Prospero compiler, target headers, runtime, and CMake wrapper |
| CMake 3.20+ and Ninja | Configure and build the payload graph |
| Clang / LLVM | Compile the `x86_64-sie-ps5` targets |
| `lzma` or `xz` | Compress the bootstrapper |
| Git and `curl` or `wget` | Initialize submodules and fetch external payload inputs |

### Full build

```shell
git submodule update --init --recursive

export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
./scripts/build.sh --jobs 8
```

The script configures the project, fetches external dependencies, and builds
the embed chain in the required order.

For the reproducible Docker build:

```shell
docker compose build aniketoshen-build
docker compose run --rm aniketoshen-build
```

### Common options

| Option | Description |
| --- | --- |
| `--build-type Debug\|Release` | Select the CMake build type |
| `--build-dir <path>` | Override the default `build/` directory |
| `--cache-dir <path>` | Override `.cache/dependencies/` |
| `--stub-missing` | Use compile-only placeholder external ELFs; never use on hardware |
| `--skip-unpacker` | Stop after building the bootstrapper |
| `--init-submodules` | Initialize submodules before dependency sync |

Set `ANIKETOSHEN_ISSUES_URL=https://…` in the environment (or pass
`-DANIKETOSHEN_ISSUES_URL=…` to CMake) to choose the bug-report address shown
in crash notifications. It should be the tracker of whoever publishes the
build; without it the notification names the tracker but carries no link.

Run `./scripts/build.sh --help` for the complete option list.

### Outputs

| Path | Description |
| --- | --- |
| `build/bin/AniketosHEN.elf` | Final payload sent to the console |
| `build/bin/bootstrapper.elf` | Uncompressed bootstrapper |
| `build/bin/bootstrapper.elf.lzma` | Bootstrapper embedded by the final payload |
| `build/bin/daemon.elf` | Critical daemon |
| `build/bin/util.elf` | Utility daemon |
| `build/bin/shellui.elf` | Toolbox injection payload |
| `build/lib/*.a` | In-tree static libraries |

Build outputs stay in `build/`. Downloads are cached in `.cache/dependencies/`.
Nothing is written back into `source/`.

### Tests

Host tests cover settings, IPC, payload helpers, cheat parsers, relocation,
Toolbox routing, and shared platform code:

```shell
make -C source/util/tests clean
make -C source/util/tests test -j8
```

Linking the host tests needs Keystone on `KEYSTONE_PREFIX`
(default: `/opt/homebrew`).

<br>

# Configuration

AniketosHEN reads and writes the same config in two places:

```text
/data/AniketosHEN/config.ini
/user/data/AniketosHEN/config.ini
```

Most options can be changed in the Toolbox. The format is
`schema_version=1`, unchanged from OnionHEN. If no file exists, AniketosHEN
writes a commented default from [`config.ini.example`](config.ini.example) —
or, on a console that ran OnionHEN, a copy of the settings found in
`/data/OnionHEN/config.ini` (see [Coming from OnionHEN](#coming-from-onionhen)).

| Key | Default | Values |
| --- | --- | --- |
| `meta.schema_version` | `1` | `1` |
| `toolbox.language` | `system` | `system`, `zh-Hans`, `zh-Hant`, `en`, `ja`, `ko`, `fr`, `de`, `it`, `es`, `pt-BR`, `pl`, `ru`, `ar`, `th` |
| `startup.open_after_load` | `none` | `none`, `home_menu` |
| `home_screen.show_title_ids` | `false` | `true`, `false` |
| `game_menu.show_aniketoshen_options` | `true` | `true`, `false` (OnionHEN's `show_onionhen_options` is still read) |
| `rest_mode.resume_reinject_delay_seconds` | `10` | `0` through `255` seconds |
| `cheats.mirror` | `auto` | `auto`, `github`, `cnb` |
| `app_jailbreak.debug_notifications` | `false` | `true`, `false` |
| `cooling.fan_control` | `automatic` | `automatic`, `temperature_threshold` |
| `cooling.temperature_threshold_celsius` | `77` | `0` through `100` |
| `overlay.enabled` | `true` | `true`, `false` |
| `overlay.background` | `true` | `true`, `false` |
| `overlay.edge` | `top` | `top`, `bottom` |
| `overlay.align` | `center` | `left`, `center`, `right` |
| `overlay.font_size` | `medium` | `small`, `medium`, `large` |
| `overlay.order` | `fps,cpu,gpu,memory,ip,fan` | comma-separated `fps`, `cpu`, `gpu`, `memory`, `ip`, `fan` |
| `overlay.show_cpu` / `overlay.show_gpu` / `overlay.show_memory` / `overlay.show_fps` | `true` | `true`, `false` |
| `overlay.cpu_usage_mode` | `average` | `average`, `per_core` |
| `overlay.show_ip_address` | `false` | `true`, `false` |
| `shortcuts.cheats_menu` | `off` | `off`, `r3_l3`, `l2_triangle`, `long_options`, `long_share`, `share` |
| `shortcuts.toolbox` | `off` | `off`, `l2_r3`, `long_share`, `share` |

### Runtime data

| Path | Purpose |
| --- | --- |
| `/data/AniketosHEN/payloads/` | User payload ELFs |
| `/data/AniketosHEN/plugins/` | Descriptor-bearing AniketosHEN plugin ELFs |
| `/data/AniketosHEN/cheats/` | Cheat files |
| `/data/AniketosHEN/cheats_tmp/` | Temporary HTTPS ZIP and extraction files, cleaned after sync |
| `/data/AniketosHEN/kstuff.elf` | Optional runtime override with priority over the embedded `kstuff` |
| `/data/AniketosHEN/AniketosHEN.log` | Main runtime log |
| `/data/AniketosHEN/AniketosHEN_crash.log` | Preserved daemon signal and backtrace log |
| `/data/AniketosHEN/AniketosHEN_util_daemon.log` | Utility daemon log |
| `/data/AniketosHEN/AniketosHEN_shellui.log` | ShellUI module and Shell Menu log |
| `/data/AniketosHEN/ShellUI/background/` | Images offered by the Shell Menu's background page |
| `/data/AniketosHEN/migration.log` | Result of the one-time OnionHEN import |
| `/data/AniketosHEN/.migrated_from_OnionHEN` | Marker: the import is done and will not run again |
| `/system_tmp/onionhen/` | Volatile sockets, ready markers and PID files (name shared with OnionHEN on purpose) |

<br>

# Coming from OnionHEN

Send `AniketosHEN.elf` instead of `OnionHEN.elf`. On its first start it
imports what OnionHEN left on the console:

| OnionHEN | AniketosHEN | What happens |
| --- | --- | --- |
| `/data/OnionHEN/config.ini` | `/data/AniketosHEN/config.ini` | Converted, checked that it loads, then installed. Unknown keys and comments are kept. |
| `/data/OnionHEN/payloads/`, `plugins/`, `cheats/`, `sprx/`, `games/` | same names under `/data/AniketosHEN/` | Copied, including `.auto_start` markers and per-payload `.json` settings. |
| `/data/OnionHEN/kstuff.elf`, `no_kstuff` | `/data/AniketosHEN/…` | Copied. |
| `OnionHEN*.log`, `assets/`, `cheats_tmp/`, cached ELF and icon | — | Not copied; AniketosHEN writes its own. |
| `/mnt/usbN/OnionHEN/payloads/` | `/mnt/usbN/AniketosHEN/payloads/` | Both folder names are read from USB drives. |
| `/data/I_want_logging_for_onionhen` | `/data/I_want_logging_for_aniketoshen` | Either file enables bootstrap log redirection. |

What you can rely on:

- **`/data/OnionHEN` is never modified or deleted.** It is your backup and your
  way back to OnionHEN. Delete it yourself once you are satisfied.
- **Nothing already in `/data/AniketosHEN` is overwritten**, so an existing
  AniketosHEN setup always wins over older OnionHEN files.
- **It runs once.** A marker file ends it; later starts cost a single file check.
  Changes made in `/data/OnionHEN` afterwards are not picked up.
- **It is safe to interrupt.** Files arrive through a temporary name and a
  rename, so a power loss leaves whole files or none, and the next start
  continues where it stopped.
- **If it cannot finish** (for example the storage is full) a notification says
  so, `/data/AniketosHEN/migration.log` records why, your settings are still
  read from `/data/OnionHEN/config.ini`, and the next start tries again.
- **A damaged OnionHEN `config.ini` is not installed.** AniketosHEN starts on
  defaults and the original file stays where it was.

The import needs free space for a second copy of your payloads, plugins and
cheats.

OnionHEN and AniketosHEN cannot run side by side: they use the same ports
(9020, 9048), the same ShellUI hooks and the same `/system_tmp/onionhen`
namespace. Loading one while the other is running replaces the running
daemons, exactly as re-sending the same payload does. To go back, reboot and
send `OnionHEN.elf`; it finds its own directory as it left it.

The design, the full path audit and the list of names that were deliberately
kept are in [docs/aniketoshen-migration.md](docs/aniketoshen-migration.md).

<br>

# Host tools

| Tool | Purpose |
| --- | --- |
| [`scripts/daemon_log.py`](scripts/daemon_log.py) | Stream daemon logs from the console |
| [`scripts/shutdown_aniketoshen.py`](scripts/shutdown_aniketoshen.py) | Shut down the AniketosHEN userland stack without killing `kstuff` |
| [`scripts/ps5_cmake.sh`](scripts/ps5_cmake.sh) | Run CMake through the PS5 payload SDK |
| [`scripts/sync_dependencies.sh`](scripts/sync_dependencies.sh) | Fetch or build external payload inputs |

<br>

# Repository layout

```text
.
├── assets/                    Project artwork
├── docs/                      Architecture and technical notes
├── scripts/                   Build, dependency, and host-side helpers
├── source/                    In-tree PS5 sources
│   ├── bootstrapper/          Startup and embedded payload chain
│   ├── daemon/                Critical daemon and Toolbox injection
│   ├── util/                  Utility daemon, IPC, and cheats
│   ├── shellui/               Toolbox and ShellUI hooks
│   ├── i18n/                  Shared Toolbox / notification locale catalogs
│   ├── unpacker/              Final AniketosHEN payload wrapper
│   ├── libonion_*/            Shared in-tree libraries
│   ├── common/                Shared low-level implementations
│   └── platform/ps5/stubs/    PS5 system-library link stubs
├── third_party/               External source, archives, and submodules
├── tools/                     Repository-side generators
├── build/                     Generated build outputs (ignored)
└── .cache/dependencies/       Downloaded external inputs (ignored)
```

See [the architecture document](docs/arch.md) for the complete module and IPC map.

<br>

# Troubleshooting

1. Before loading AniketosHEN, make sure the external `elfldr` is reachable on port **9021**.
2. Make sure the payload was built for this firmware with a complete PS5 Payload SDK.
3. If the Toolbox does not appear, wait for `util → kstuff → daemon` to finish and check the logs.
4. If the build cannot get `kstuff.elf`, run `./scripts/sync_dependencies.sh` again or initialize the submodule.
5. When you report a problem, include firmware, exploit host, SDK version, build type, logs, and steps to reproduce.

<br>

# Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

<br>

# Credits

AniketosHEN is a fork of
[OnionHEN](https://github.com/aydencharles/onionHEN). The code it started from
is the work of 麒麟/0xp0co, kvnhrt and the OnionHEN contributors, built in turn
on etaHEN by LightningMods; the copyright notices in the source files are
theirs and are unchanged. What the fork changed and added is listed in
[NOTICE.md](NOTICE.md).

AniketosHEN exists because of the PS5 homebrew and reverse-engineering
community.

### Based on

- [OnionHEN](https://github.com/aydencharles/onionHEN) — 麒麟/0xp0co, kvnhrt and contributors; the project this one is forked from
- [etaHEN](https://github.com/LightningMods/etaHEN) — LightningMods and contributors; source base of this tree
- [GoldHEN](https://github.com/GoldHEN/GoldHEN) — SiSTR0 and contributors; the PS4 all-in-one HEN this project takes after

### Referenced

- [kstuff-lite](https://github.com/EchoStretch/kstuff-lite) — EchoStretch, sleirsgoevy, and contributors; Rest Mode Toolbox recovery follows its SceSysCore `NOTE_EXEC` watch for `NPXS40087` and wait for `libSceNpTrophy.sprx` / `libSceNpTrophy2.sprx`
- [ps5-payload-manager](https://github.com/itsplk/ps5-payload-manager) — itsplk; listen-socket rebind after Rest Mode (Unix IPC and TCP accept-fail self-heal) follows this project
- [HEN-Cheats-Collection](https://github.com/TeeKay87/HEN-Cheats-Collection) — TeeKay87; the community cheat collection downloaded by the built-in cheat sync
- [PHU Games Tools](https://github.com/ArkSama) — ArkSama; the in-game FPS counter follows PHU Games Tools skip-hook sampling (`/dev/dce` scanout and DMAP reads of `libSceAgcDriver`)

### Used or embedded

- [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) — Prospero toolchain and headers
- [elfldr](https://github.com/ps5-payload-dev/elfldr) — first-hop loader on port 9021; not shipped in the payload
- [kstuff-lite](https://github.com/EchoStretch/kstuff-lite) — EchoStretch, sleirsgoevy, and contributors; optional `kstuff.elf`
- [onionHEN-ftpsrv-plugin](https://github.com/OnionBuddies/onionHEN-ftpsrv-plugin) — optional external FTP server plugin based on drakmor/ftpsrv
- [onionHEN-shadowmountplus-plugin](https://github.com/OnionBuddies/onionHEN-shadowmountplus-plugin) — optional external ShadowMount+ scanner/mounter plugin based on Drakmor's project
- [libhijacker](https://github.com/astrelsky/libhijacker) — astrelsky; process hijack and kernel R/W
- [NineS](https://github.com/buzzer-re/NineS) — buzzer-re; ShellUI injection
- [cJSON](https://github.com/DaveGamble/cJSON) — JSON parsing
- [7-Zip LZMA SDK](https://www.7-zip.org/sdk.html) — unpacker decompression
- [miniz](https://github.com/richgel999/miniz) — cheat-file decompression

<br>

# License

AniketosHEN is free software under the
[GNU General Public License, version 3](LICENSE) or, at your option, any later
version (GPL-3.0-or-later) — the terms OnionHEN and etaHEN, from which it is
forked, are published under, as their source file headers state. Third-party
components keep their own licenses and notices. [NOTICE.md](NOTICE.md) records
what this fork changed, as section 5 of the license requires, and the complete
corresponding source is this repository.

> AniketosHEN is an unofficial homebrew project and is not affiliated with Sony Interactive Entertainment.
> Use it only on hardware you own and at your own risk. No warranty is provided.
