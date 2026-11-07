# Payload Manager

Payload Manager is the Toolbox page for managing user payloads. It downloads a
catalog over HTTPS, browses and installs entries, adds payloads to autostart,
and removes payloads it installed. The default catalog is the one published by
this repository.

Open it from **Toolbox → Payload Manager**.

## Pages

Opening **Payload Manager** goes straight to **Installed**. That page shows
every installed payload with its version, Running/Stopped state and autostart
position, plus links to **Browse Catalog** and **Autostart**:

- **Installed** — selecting a payload opens its per-payload page. The header
  shows the FTP drop path and the USB drop path; **Refresh list** rescans both.
- **Autostart** — the master switch, **Add payload** and an ordered list of the
  payloads that start with AniketosHEN. The top row loads first.
- **Browse Catalog** — the online catalog with icons, category/version/status
  subtitles and search. **Catalog settings** at the bottom controls sources,
  cache and the last refresh date.

There is no **Install from URL** entry. Payloads are installed from a catalog
entry only.

### Installed and per-payload pages

Selecting an installed payload opens `payload_<id>.xml`. That page has:

- **Current session** — starts or stops the payload now.
- **Remove** only for payloads under `/data/AniketosHEN/payloads`.
- Metadata: version, source, updated date, category, filename, installed date,
  size and SHA-256.
- **Update** only when the catalog has a newer version.

Autostart membership is managed only from **Autostart**. Adding a payload there
appends it to the end of the boot order; **OPTIONS → Remove** on its row
deletes it (or, outside the payloads folder, removes the marker only). A payload
that is already running keeps running until it is stopped from **Current
session**. The old priority
text field, Move up/Move down buttons, Enabled toggle and separate "Auto-start
next time" toggle are gone.

The Installed status line is a combination of Running/Stopped, the 1-based
autostart position and, when available, an update marker.

### Autostart

Autostart lists only payloads with an `.auto_start` marker, ordered by their
persisted `priority` (`999 - index * 10`, so the first row has the highest
priority). **Add payload** lists installed payloads that are not yet in
autostart; selecting one appends it to the end of the order. The master switch
is `[payload_center] autostart_enabled` in `config.ini`; when it is off the
bootstrapper returns before scanning.

Each row shows its load delay on the right. Pressing **X** on a row picks the
payload up; **Up**/**Down** move it one place (every move rewrites the enabled
payloads' `priority` at once) and **X** or **O** puts it down. While a payload
is held the page keeps the controller, so the focus cannot leave the row.
**OPTIONS** on a row opens a native menu with **Delay** (0-10 s or 15 s) and
**Remove**: payloads under `/data/AniketosHEN/payloads` are deleted, others
only leave autostart. The list is published through the Settings reconciler,
so it redraws in place without reopening the page.

## Files

```
/data/AniketosHEN/
├── config.ini
├── payload_center/
│   ├── sources.json
│   ├── icons/<fnv64(icon_key)>.png
│   └── cache/
│       ├── catalog-<src8>.json
│       ├── catalog-<src8>.status.json
│       ├── icons-<src8>.status.json
│       └── tmp/
└── payloads/
    ├── <slug>.elf
    ├── <slug>.elf.json
    ├── <slug>.elf.meta.json
    └── <slug>.elf.auto_start
```

`/user/data/AniketosHEN` is the SceShellUI sandbox view of the same storage.
The util daemon writes through `/data`; ShellUI reads both views.

### `sources.json`

```json
{
  "version": 1,
  "sources": [
    {"name": "AniketosHEN Payloads", "url": "https://raw.githubusercontent.com/xS3Cx/AniketosHEN/main/payloads.json", "builtin": true},
    {"name": "My catalog", "url": "https://example.com/payloads.json", "builtin": false}
  ]
}
```

The built-in source is always present and cannot be removed. Users can add up
to eight HTTPS sources. `http://` sources and catalog entries are rejected;
the URL and name are length-limited and validated before use. There is no
mirror fallback: if the built-in catalog cannot be fetched, refresh reports a
normal network error and keeps the previous cache.

Cache files are pruned when a source is removed, so an old source hash cannot
stay on disk after its source is gone.

### Catalog cache

`src8` is the first eight hex digits of FNV-1a 32-bit over the source URL.
Each source keeps a copy of the last good catalog and a status file with the
URL, fetch time, byte count and SHA-256 of the downloaded bytes. A failed
refresh leaves the previous copy in place.

`icons-<src8>.status.json` records the last icon refresh. Icons are refreshed
at most once every seven days; a failed icon download keeps the previous icon.
If the catalog does not supply a usable icon, a colored tile with the payload's
initial is generated locally.

Refreshing the catalog is a background task. It returns to the page
immediately and reports success or failure with a notification; the progress
page is used only for installs and updates.

### Installed metadata

`<slug>.elf.meta.json` is version 1:

```json
{
  "version": 1,
  "id": "ftpsrv",
  "match_key": "https://github.com/ps5-payload-dev/ftpsrv/releases",
  "source_url": "https://raw.githubusercontent.com/xS3Cx/AniketosHEN/main/payloads.json",
  "source_name": "AniketosHEN Payloads",
  "name": "ftpsrv",
  "item_version": "v0.21.1",
  "filename": "ftpsrv-ps5.elf",
  "checksum": "0123456789abcdef...",
  "last_update": "2026-08-20",
  "category": "Servers",
  "installed_at": 1793986660
}
```

`version` is the sidecar schema version. `item_version` is the catalog item
version. `match_key` is the item's `source` field when present, otherwise
`source_url + "#" + id`; it identifies the same catalog entry across catalogs.
The existing `<slug>.elf.json` (`priority`, `delay_seconds`) and
`<slug>.elf.auto_start` files are unchanged.

The target is `<slug(name)>.elf`. If another source already owns that file,
the install uses `<slug>-<src8>.elf`. Replacing a file with no metadata
requires an explicit confirmation in the UI; the util daemon refuses it unless
the install request sets `replace`.

Newly installed payloads do not get an `.auto_start` marker, so autostart is
off by default. Add them from **Autostart → Add payload**.

## Catalog schema

Both shapes are accepted:

```json
{
  "name": "AniketosHEN Payloads",
  "icons_url": "icons.json",
  "payloads": [
    {
      "id": "ftpsrv",
      "name": "ftpsrv",
      "filename": "ftpsrv-ps5.elf",
      "url": "https://github.com/ps5-payload-dev/ftpsrv/releases/download/v0.21.1/ftpsrv-ps5.elf",
      "source": "https://github.com/ps5-payload-dev/ftpsrv/releases",
      "description": "FTP server for PlayStation 5.",
      "last_update": "2026-08-20",
      "version": "v0.21.1",
      "category": "Servers",
      "checksum": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      "icon_key": "ftpsrv"
    }
  ]
}
```

```json
[
  {"name": "ftpsrv", "filename": "ftpsrv-ps5.elf", "url": "https://example.com/ftpsrv.elf"}
]
```

Limits: 16 MiB catalog, at most 4096 entries, name 256 bytes, description
2048 bytes, category 128 bytes, URL 2048 bytes. Long UTF-8 text is truncated
on a character boundary. Invalid entries are skipped; a malformed checksum
rejects only that entry. `.bin` entries are shown but are not installable.

`id` is optional. When absent it is `slug(name)`. `filename` is optional and is
derived from the URL when absent. `checksum` is optional; when present it must
be a SHA-256 hex string (an optional `sha256:` prefix is accepted).

A missing checksum shows a warning and a confirmation before install. A
malformed checksum rejects the entry. A checksum mismatch after download is a
hard failure; the partial file is deleted and the existing payload is kept.

Entries with `http://` URLs are listed as insecure and cannot be installed.

### Maintaining the bundled catalog

`scripts/build_payload_catalog.py` builds the repository's `payloads.json` and
`icons.json`. For GitHub projects it queries `releases/latest`, picks the
matching `.elf` asset, downloads it and computes its SHA-256. Non-GitHub
projects use an explicit `direct_url`. The script uses only the Python standard
library and honors `GITHUB_TOKEN` when present:

```bash
python3 scripts/build_payload_catalog.py
```

`icons.json` is an object mapping each `icon_key` to a
`data:image/png;base64,...` 128×128 tile generated from the payload's initial
and category color; no third-party logos are bundled.

## Install pipeline

1. Resolve the catalog item server-side from `(source_id, item_id)`. Filesystem
   paths never cross IPC.
2. Check free space (64 MiB margin).
3. Download into `payload_center/cache/tmp/` while streaming SHA-256.
4. Verify the catalog checksum when present.
5. Verify the ELF header (`0x7f ELF`, 64-bit, little endian).
6. `chmod 0644`, `fsync`, atomic rename into `/data/AniketosHEN/payloads/`,
   and `fsync` the directory.
7. Write `<slug>.elf.meta.json` atomically.

Only one task runs at a time. The progress page polls the daemon every 500 ms;
pressing Back while an install is running cancels it. A cancelled or failed
download leaves no `.part` file and does not touch an installed payload.
Refresh jobs never open the progress page.

The directory tree is created with recursive `mkdir_tree` before every task,
including on a fresh console where
`/data/AniketosHEN/payload_center/cache` does not exist yet. Filesystem
failures log the failing operation with `errno`/`strerror`.

Errors are reported as `network`, `protocol`, `tls`, `clock`, `hash`, `elf`,
`io`, `space`, `busy`, `rejected` or `cancelled`. Catalog cache write failures
use the dedicated catalog I/O message. Certificate-not-yet-valid and expired
errors are reported as `clock`; other verification failures as `tls`.

## Toolbox entry

The Toolbox root exposes **Payload Manager** directly. **Plugins** now lives in
**Content Install & Management**, together with Package Installer and Add-on
Content Manager. Installed payloads are reached from **Payload Manager**.

`elfldr` payloads never autostart: the bootstrapper already skips filenames
containing `elfldr`. `kstuff`, `etaHEN`, `pldmgr`, `lapy-jb`, `backpork`,
`ps5-linux-loader`, `onionHEN`, `AniketosHEN` and other system/conflicting
payloads are not blocked, but the detail page warns and asks for confirmation.

## Security

- HTTPS only. HTTP catalog entries are shown but cannot be installed; redirects
  are restricted to HTTPS.
- TLS peer and host verification use the CA bundle embedded in util.
- Catalog bodies, icons and payload downloads have hard byte caps.
- Names, filenames, URLs, checksums and identities are validated before any
  file is created.
- Downloads go to `payload_center/cache/tmp/`, never inside `payloads/`.
- Writes are atomic and `0644`; directories are `fsync`ed after rename.
- `BREW_UTIL_PAYLOAD_REMOVE` accepts a path identity, then util rescans
  `/data/AniketosHEN/payloads`; it never accepts an arbitrary path.
- Removing a payload deletes its `.elf`, `.json`, `.meta.json` and
  `.auto_start` files only.

## FTP drop

The optional FTP plugin exposes `/data/AniketosHEN/payloads`, so a payload can
be dropped manually without the online catalog. The Installed page shows the
FTP drop path and the USB drop path, but no IP address.

## Settings

`config.ini`:

```ini
[payload_center]
autostart_enabled=true

[kstuff]
autoload=false

[shadowmount]
autoload=false
```

`kstuff` remains embedded as a fallback. Both kstuff and ShadowMount have
toggles on **Payload Manager → Included Payloads**. Their behavior is unchanged
by the Payload Center rework.
