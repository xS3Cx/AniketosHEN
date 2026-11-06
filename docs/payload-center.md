# Payload Center

Payload Center is the Toolbox page for installing user payloads from an online
catalog. It downloads a catalog over HTTPS, lists its entries by category,
installs one ELF at a time, verifies checksums when the catalog provides one,
detects updates from the installed metadata, and removes payloads it installed.

Open it from **Toolbox → Payloads & Kernel → Payload Center**.

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
    {"name": "PS5 Payloads", "url": "https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json", "builtin": true},
    {"name": "My mirror", "url": "https://example.com/payloads_v2.json", "builtin": false}
  ]
}
```

The built-in source is always present and cannot be removed. Users can add up
to eight HTTPS sources. `http://` sources and catalog entries are rejected;
the URL and name are length-limited and validated before use.

### Catalog cache

`src8` is the first eight hex digits of FNV-1a 32-bit over the source URL.
Each source keeps a copy of the last good catalog and a status file with the
URL, fetch time, byte count and SHA-256 of the downloaded bytes. A failed
refresh leaves the previous copy in place.

`icons-<src8>.status.json` records the last icon refresh. Icons are refreshed
at most once every seven days; a failed icon download keeps the previous icon.

### Installed metadata

`<slug>.elf.meta.json` is version 1:

```json
{
  "version": 1,
  "id": "ftpsrv",
  "match_key": "https://github.com/ps5-payload-dev/ftpsrv/releases",
  "source_url": "https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json",
  "source_name": "PS5 Payloads",
  "name": "ftpsrv",
  "item_version": "v0.21.1",
  "filename": "ftpsrv_v0.21.1.elf",
  "checksum": "0123456789abcdef...",
  "last_update": "2026-08-20",
  "category": "Networking & Servers",
  "installed_at": 1793986660
}
```

`version` is the sidecar schema version. `item_version` is the catalog item
version. `match_key` is the item's `source` field when present, otherwise
`source_url + "#" + id`; it identifies the same catalog entry across mirrors.
The existing `<slug>.elf.json` (`priority`, `delay_seconds`) and
`<slug>.elf.auto_start` files are unchanged.

The target is `<slug(name)>.elf`. If another source already owns that file,
the install uses `<slug>-<src8>.elf`. Replacing a file with no metadata
requires an explicit confirmation in the UI; the util daemon refuses it unless
the install request sets `replace`.

Newly installed payloads do not get an `.auto_start` marker, so autostart is
off by default.

## Catalog schema

Both shapes are accepted:

```json
{
  "name": "PS5 Payloads",
  "icons_url": "https://example.com/icons/",
  "payloads": [
    {
      "name": "ftpsrv",
      "filename": "ftpsrv_v0.21.1.elf",
      "url": "https://example.com/ftpsrv_v0.21.1.elf",
      "source": "https://github.com/ps5-payload-dev/ftpsrv/releases",
      "description": "FTP server",
      "last_update": "2026-08-20",
      "version": "v0.21.1",
      "category": "Networking & Servers",
      "checksum": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      "icon_key": "ftpsrv"
    }
  ]
}
```

```json
[
  {"name": "ftpsrv", "filename": "ftpsrv_v0.21.1.elf", "url": "https://example.com/ftpsrv.elf"}
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

## Install pipeline

1. Resolve the catalog item server-side from `(source_id, item_id)` or a
   direct HTTPS URL. Filesystem paths never cross IPC.
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

Errors are reported as `network`, `protocol`, `tls`, `clock`, `hash`, `elf`,
`io`, `space`, `busy`, `rejected` or `cancelled`. Certificate-not-yet-valid
and expired errors are reported as `clock`; other verification failures as
`tls`.

## Autostart

The Autostart page lists payloads found by the normal payload scanner and uses
the existing `.auto_start` markers, so it stays compatible with the Payloads
page. The master switch is `[payload_center] autostart_enabled` in
`config.ini`; when it is off the bootstrapper returns before scanning.

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

The Payload Center hub shows the console address in the form
`ftp://<ip>:2121 -> /data/AniketosHEN/payloads` when the LAN address can be
resolved. The optional FTP plugin exposes the same directory, so a payload can
be dropped manually without the online catalog.

## Hosting a catalog

Serve a `payloads_v2.json` over HTTPS. Keep filenames unique per entry, use
stable `source` values (for example the project release page) so updates match
after a mirror change, and publish a SHA-256 checksum whenever possible. Put
the icon key into `icon_key` (or a `data:image/png;base64,...` URI) and point
`icons_url` either at a JSON object mapping icon keys to data URIs/HTTPS URLs,
or at a directory ending in `/` where `<icon_key>.png` can be fetched.

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
Toolbox toggles; ShadowMount's toggle lives on the Plugins page.
