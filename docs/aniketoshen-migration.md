# AniketosHEN: distribution identity and migration from OnionHEN

AniketosHEN is a distribution of [OnionHEN](https://github.com/aydencharles/onionHEN).
This note records what was renamed, what was deliberately kept, where data
lives on the console, and how an existing OnionHEN installation is taken over.
It is the reference for anyone merging from upstream or changing the name again.

## 1. Rule of thumb

> Everything a user, a packager or the console's persistent storage sees says
> **AniketosHEN**. Everything that is code namespace, runtime contract with
> other binaries, or attribution keeps the name upstream gave it.

The identity is defined once, in
[`source/include/onion/distribution.h`](../source/include/onion/distribution.h).
C and C++ code builds its paths and messages from those macros; nothing else
in `source/` hardcodes the distribution name.

## 2. Classification of every `OnionHEN` reference

| # | Category | Decision | Examples |
|---|---|---|---|
| 1 | User-facing branding | **Renamed** | Toolbox titles and all 14 locale catalogs, notifications and the `[AniketosHEN]` watermark, log tag, Settings version banner, README, issue templates |
| 2 | Build and packaging | **Renamed** | `project("AniketosHEN")`, final artifact `AniketosHEN.elf`, compose services `aniketoshen-build` / `aniketoshen-tests`, `scripts/shutdown_aniketoshen.py`, stub marker `AniketosHEN-STUB` |
| 3 | Persistent paths and config identifiers | **Renamed, with migration** | `/data/AniketosHEN/…`, `config.ini` key `show_aniketoshen_options` (section 4) |
| 4 | Credits, copyright, license, provenance | **Preserved** | `Copyright (C) … OnionHEN / LightningMods` headers (200 lines, identical count before and after), `LICENSE`, README *Support* and *Credits*, `ONIONHEN_AUTHOR`, About-page testers/donors/donation links, `about.lineage.etahen` |
| 5 | Historical and upstream references | **Preserved** | `docs/pkg-writeup.md` (third-party writeup, untouched), links to `aydencharles/onionHEN`, the `OnionBuddies/onionHEN-*-plugin` projects and the OnionHEN Plugin SDK |
| 6 | Compatibility contracts | **Preserved** (section 3) | `/system_tmp/onionhen`, `onion_*.elf` process names, `OnionHEN?…` ShellUI URIs, `/system_ex/vsh_asset/onionhen.png`, `onionhen_jailbreak` request file |
| 7 | Internal code namespace | **Preserved** | `onion_` / `ONION_` / `ONIONHEN_` identifiers, `namespace onion`, `libonion_*`, `<onion/…>` headers, CMake helper `OnionTarget.cmake` |

Category 7 is roughly 5,000 lines. Renaming it would change no behaviour and no
output, would make every future merge from OnionHEN conflict, and would
obscure where the code came from. It stays.

## 3. Names kept on purpose

| Name | Why it must not follow the distribution name |
|---|---|
| `/system_tmp/onionhen/{ipc,ready,pid}/…` | Runtime contract. External plugin ELFs built with the OnionHEN Plugin SDK connect to `ipc/plugin_service`; the readiness and PID markers are how a newly loaded stack finds and replaces a running one. Volatile storage, never visible to users. |
| `onion_daemon.elf`, `onion_util.elf`, `onion_elfldr.elf` (process names) | `stop_owned_service()` matches these names before killing a previous instance. Keeping them means loading AniketosHEN over a running OnionHEN (or the reverse) is a clean replacement instead of two stacks fighting over ports 9020/9048. |
| `OnionHEN?NavUI=1`, `OnionHEN?Cheats`, … | Internal ShellUI deep-link tokens. They appear inside byte-exact, fixed-length patches of the HomeUI JavaScript bundle (`homeui_top_nav_patch.cpp`, five firmware families). A longer name does not fit the patched slots. Never shown on screen. |
| `/system_ex/vsh_asset/onionhen.png` | Same patches reference this icon path by its exact bytes. |
| `etahen_jailbreak`, `onionhen_jailbreak` | File names that third-party homebrew writes to request privileges. An API owned by those applications. |
| TCP ports 9020, 9021, 9048, 9088; control magic `'ONIO'` | Wire protocol used by host tools. |
| `/data/shadowmount/…`, `/update`, `/system`, `/system_ex` | Sony- or third-party-owned locations. |

## 4. Filesystem audit

`/data` is the elevated view and `/user/data` the SceShellUI sandbox view of
the same storage; the code treats them as twins and so does the migration.

### A. Distribution-owned — renamed, not migrated

Regenerated on every boot or meaningless to another distribution.

| OnionHEN | AniketosHEN | Writer |
|---|---|---|
| `/data/OnionHEN/assets/*.png` | `/data/AniketosHEN/assets/*.png` | bootstrapper |
| `/user/data/OnionHEN/onionhen.png` | `/user/data/AniketosHEN/aniketoshen.png` | bootstrapper (notification icon) |
| `/user/data/OnionHEN/onionhen.elf` | `/user/data/AniketosHEN/aniketoshen.elf` | unpacker (bootstrapper cache, SHA-1 checked) |
| `/data/OnionHEN/OnionHEN.log`, `_crash.log`, `_util_daemon.log`, `_util_crash.log` | `/data/AniketosHEN/AniketosHEN*.log` | daemon, util |
| — | `/data/AniketosHEN/migration.log`, `.migrated_from_OnionHEN` | bootstrapper |

### B. Compatibility-critical — unchanged

See section 3.

### C. Shared persistent data — migrated

| OnionHEN | AniketosHEN | Notes |
|---|---|---|
| `/data/OnionHEN/config.ini` | `/data/AniketosHEN/config.ini` | converted and validated, section 5 |
| `/data/OnionHEN/payloads/` | `/data/AniketosHEN/payloads/` | ELFs, `<elf>.json`, `<elf>.auto_start` |
| `/data/OnionHEN/plugins/` | `/data/AniketosHEN/plugins/` | descriptor-bearing ELFs, `.auto_start` |
| `/data/OnionHEN/cheats/` | `/data/AniketosHEN/cheats/` | |
| `/data/OnionHEN/sprx/` | `/data/AniketosHEN/sprx/` | includes `catalog.ini` |
| `/data/OnionHEN/games/` | `/data/AniketosHEN/games/` | |
| `/data/OnionHEN/kstuff.elf`, `no_kstuff` | `/data/AniketosHEN/…` | kstuff override and opt-out flag |
| anything else the user put in the root | same relative path | unknown files are user data: copied |

### D. Temporary — renamed or left behind

| Path | Decision |
|---|---|
| `/data/OnionHEN/cheats_tmp/`, `cheats_staging/` | not migrated; recreated under the new root on demand |
| `/user/data/OnionHEN/<TITLE>_cheats_<n>` | not migrated; per-request exchange files |
| `*.tmp.*` | not migrated; in-flight atomic-write temp files |
| `/system_tmp/onionhen/…` | unchanged (section 3) |

### Removable media and opt-in markers

| OnionHEN | AniketosHEN |
|---|---|
| `/mnt/usbN/OnionHEN/payloads/`, `/mnt/usbN/onionhen/payloads/` | still read, after `/mnt/usbN/AniketosHEN/payloads/` |
| `/data/I_want_logging_for_onionhen` | still honoured, alongside `/data/I_want_logging_for_aniketoshen` |

USB drives cannot be migrated, so the old folder names remain valid there.
Internal storage is different: the payload scanners list **only** the
AniketosHEN root, because a payload present in both roots would otherwise be
auto-started twice.

## 5. Configuration

`config.ini` is an INI file with `[meta] schema_version=1`. The schema is
unchanged from OnionHEN and holds no absolute paths. One key carried the old
name:

| OnionHEN | AniketosHEN |
|---|---|
| `[game_menu] show_onionhen_options` | `[game_menu] show_aniketoshen_options` |

- The loader reads both spellings; when both are present the current key wins.
- The serializer writes only the current key.
- Migration rewrites that key (and the generated comment line above it) and
  passes every other byte through — unknown sections, unknown keys, comments,
  CRLF line endings.

Lookup order in `settings_load()`:

1. `/data/AniketosHEN/config.ini`
2. `/user/data/AniketosHEN/config.ini`
3. *only if neither of those files exists:* `/data/OnionHEN/config.ini`, then
   `/user/data/OnionHEN/config.ini` — read-only.

Step 3 is the safety net for the first boot (the bootstrapper reads settings
before it migrates) and for a migration that could not complete. An
AniketosHEN config that exists but is unusable yields defaults, as before; it
is never silently replaced by OnionHEN's older file. `settings_ensure_default()`
seeds a new file from step 3 when it can, so the daemon creating "the default
config" does not shadow the user's OnionHEN settings.

Saves always go to the AniketosHEN paths. Nothing writes under `/data/OnionHEN`.

## 6. Migration

Implemented in `source/libonion_platform/source/` (`<onion/migrate.h>`):
`migrate.c` runs the import, `migrate_tree.c` copies the files and
`migrate_config.c` converts `config.ini`. It is called from the bootstrapper
(`bootstrap_migration.cpp`) after privileges are raised and the conflict check
has passed, and before any directory or file is created under the data root.

For each of the two views (`/data`, `/user/data`):

1. Marker `.migrated_from_OnionHEN` present → done; return. (One `lstat`.)
2. No OnionHEN directory, or it is not a real directory → nothing to do.
3. Measure the bytes still to copy and compare with free space (plus 1 MiB).
4. Convert `config.ini` to a temp file, validate it with the real settings
   loader, and only then `rename()` it into place.
5. Copy the tree. Each file goes to `<name>.migrate.tmp`, is `fsync`ed and
   renamed.
6. Write the marker (atomically) and append one line to `migration.log`.

Properties:

| Requirement | How it is met |
|---|---|
| Original data recoverable | The OnionHEN root is opened read-only; no `unlink`, `rename` or write ever targets it. It is the backup. |
| No overwrite of AniketosHEN data | A destination that exists is skipped, whatever its content. An existing `config.ini` is `kept-existing`. |
| Not repeated | Marker checked first. No marker is written when there was nothing to migrate, so an OnionHEN directory that appears later is still imported once. |
| Transactional per file | temp + `fsync` + `rename`; leftovers from an interrupted run are removed, never promoted. |
| Resumable | No marker until the end; a re-run skips what is already present. |
| Malformed config | Rejected if it is not a regular file, exceeds 256 KiB, contains NUL bytes, or fails `settings_load_file()`. Not installed; defaults apply; the run still completes because retrying cannot help. |
| I/O failure | Temp file removed, first error recorded, no marker, retried next boot. `ENOSPC`/`EROFS`/`EIO` stop the walk at once. |
| Untrusted input | `lstat` on every entry; symbolic links and special files are neither followed nor copied; sources opened with `O_NOFOLLOW` and re-checked by device/inode; temp files created with `O_EXCL | O_NOFOLLOW`; depth limit 12; names with `/` and over-long paths skipped; roots must be absolute and must not contain each other. |
| Diagnostics | `migration.log` (capped at 64 KiB) and the main log record status, counts, `errno` text and the relative path that failed. No file contents. A notification reports success or failure in plain English. |

Status values: `no-legacy-data`, `already-done`, `completed`, `failed`.
Config values: `none`, `kept-existing`, `migrated`, `rejected`, `failed`.

### Recovery

- **Import failed** — free space or fix the storage and start AniketosHEN
  again. Meanwhile settings are read from `/data/OnionHEN/config.ini`;
  payloads, plugins and cheats are not visible until the import completes.
- **Start over** — delete `/data/AniketosHEN` (or just the marker). The next
  start imports from `/data/OnionHEN` again.
- **Back to OnionHEN** — reboot and send `OnionHEN.elf`. Its directory is as
  it left it.

## 7. Coexistence

Not supported, by construction rather than by policy: both stacks bind ports
9020 and 9048, hook the same ShellUI functions and share
`/system_tmp/onionhen`. Because the process names and readiness markers are
shared, loading either one while the other runs stops the running daemon and
utility and replaces them — the behaviour upstream already has for re-sending
its own payload. The persistent directories are separate, so the two
distributions never write to the same file.

## 8. Not verified on hardware

Everything above was validated with the host unit tests and a cross-compile of
all targets. No PS5 was available. Before a release, check on a console:

1. Fresh console: boot, Toolbox opens, `/data/AniketosHEN/config.ini` created.
2. Console with OnionHEN data: first boot shows the import notification;
   settings, payload auto-start, plugins, cheats and the kstuff override behave
   as they did; `/data/OnionHEN` is byte-identical afterwards.
3. Second boot: no notification, `migration.log` unchanged.
4. Whether `/data` and `/user/data` are one directory on the target firmware
   (the code is correct either way; on one directory the second pass is a no-op).
5. An OnionHEN-built plugin (for example the FTP plugin) installed under
   `/data/AniketosHEN/plugins/` starts and shows its settings page. A plugin
   that hardcodes `/data/OnionHEN/…` for its own files would keep using that
   directory.
6. Settings → System Software shows the `AniketosHEN <version>` banner.
7. Time taken by the first-boot import with a full cheat collection.
8. Loading AniketosHEN while OnionHEN is running, and the reverse.

## 9. Open item for the publisher

`ONION_DIST_ISSUES_URL` (CMake `-DANIKETOSHEN_ISSUES_URL=…`, or the
environment variable of the same name for `scripts/build.sh`) is the address
the crash notification gives for bug reports. It has no default URL, because
the only URL in the tree was OnionHEN's tracker and crash reports from this
distribution do not belong there. Set it for release builds.

The logo (`assets/logo.png`) and the console icon
(`source/bootstrapper/assets/aniketoshen_sicon.png`, the same image at
128x128) are AniketosHEN's own artwork with a transparent background. The
README screenshots still show OnionHEN's interface and need to be retaken.
