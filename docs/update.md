# HEN Update Manager

The HEN Update Manager checks GitHub Releases for a newer AniketosHEN build,
downloads it after an explicit confirmation and lets the user reboot so the new
ELF can be loaded. It never restarts itself.

## Manifest

There is one channel, GitHub's latest release:

```
https://github.com/xS3Cx/AniketosHEN/releases/latest/download/update.json
```

`update.json` is a single JSON object:

```json
{
  "version": "0.05a",
  "tag": "v0.05a",
  "channel": "stable",
  "published_at": "2026-11-07T12:00:00Z",
  "url": "https://github.com/xS3Cx/AniketosHEN/releases/download/v0.05a/AniketosHEN.elf",
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "size": 1048576,
  "summary": "Short release summary"
}
```

Validation rules:

- the manifest is at most 64 KiB and must be a JSON object;
- `version` is 1-32 characters of letters, digits, `.`, `_`, `+`, `-` and must
  contain a digit; a leading `v` is accepted;
- `tag` normalises to the same version;
- `channel` is `stable`;
- `published_at` is non-empty, printable and at most 40 characters;
- `url` is HTTPS, at most 2048 characters, on `github.com`,
  `objects.githubusercontent.com` or `release-assets.githubusercontent.com`,
  and ends in `.elf`;
- `sha256` is 64 lowercase hex characters (a `sha256:` prefix is accepted and
  removed);
- `size` is an integer from 1 byte to 64 MiB;
- `summary` is non-empty, printable and at most 300 characters.

`ONIONHEN_VERSION` from `<onion/version.h>` is compared with `version` through
`onion::payload_catalog::compare_versions`, with a leading `v` stripped. A
development or dirty build (`dirty`, `unknown` or empty) is shown as
**Development build** and never produces a boot notification.

## Boot check

Automatic checks are **on by default** (`[update] auto_check=true`; a config
written by 0.04o/0.04p, which still has the old `channel` key, is switched on
once). The util daemon starts a detached thread after boot. The thread waits
for a local IPv4 address for up to 60 seconds, fetches the manifest, caches it
and sends a notification when the release is newer, once per version
(`update/notified.txt` holds the version last announced). It never downloads
automatically.

## Manual flow

**Toolbox > HEN Update Manager** shows only the installed version and the last
check time. When a newer build exists it adds **Update available: version** and
the single action button changes from **Check for updates now** to
**Download version** and then to **Reboot to apply**.

**Update options** is a separate page with the `auto_check` switch. **Download from GitHub** opens the latest-release page, which renders
the cached manifest and a **Download** button.

The progress page polls the util service and shows the `check`, `download`,
`verify` and `save` phases. Back cancels the running task. Successful checks and
downloads return to the previous page automatically.

## Download and apply

The download:

1. reads the cached manifest for the selected channel;
2. streams the asset over HTTPS, enforcing the manifest size and the 64 MiB
   cap;
3. hashes the bytes with SHA-256 while writing into
   `/data/AniketosHEN/update/tmp/AniketosHEN.elf.part.XXXXXX`;
4. verifies the SHA-256 and the ELF header (`0x7f ELF`, ELFCLASS64, x86-64);
5. `fsync`s, sets mode `0644` and atomically renames the file to
   `/data/AniketosHEN/update/AniketosHEN.elf`;
6. writes `/data/AniketosHEN/update/ready.json` atomically.

Only the latest ELF is kept; there is no backup. A failed download removes its
temporary file and leaves any previous update untouched.

`ready.json`:

```json
{
  "version": "0.05a",
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "size": 1048576,
  "summary": "Short release summary",
  "downloaded_at": 1793544000
}
```

The update page then shows **Update ready: version**, the saved path and **Reboot
to apply**. The button has a confirmation and calls
`sceSystemServiceRequestReboot()`. AniketosHEN does not attempt to restart or
replace itself. After the console reboots, retrieve the saved ELF over FTP and
send it with the external `:9021` loader. **Delete downloaded update** removes
the ELF and `ready.json`.

## Files

| Path | Purpose |
| --- | --- |
| `/data/AniketosHEN/update/latest-stable.json` | Last stable manifest response |
| `/data/AniketosHEN/update/notified.txt` | Version the boot check last announced |
| `/data/AniketosHEN/update/check.json` | Last check time (`fetched_at`) and error |
| `/data/AniketosHEN/update/AniketosHEN.elf` | Downloaded update |
| `/data/AniketosHEN/update/ready.json` | Ready marker shown by the Toolbox |
| `/data/AniketosHEN/update/tmp/` | `.part` download files |

## Security

- HTTPS only; redirects are restricted to HTTPS by the shared libcurl
  transport, with the embedded CA bundle and host verification enabled.
- The manifest is capped at 64 KiB and the ELF at 64 MiB.
- The manifest channel must match the requested channel.
- The asset must be on a GitHub release host and end in `.elf`.
- SHA-256 is mandatory; there is no hash-less path.
- The ELF header is validated before the file becomes visible as the update.
- All metadata writes use a temporary file, `fsync` and an atomic rename.

## Settings

```ini
[update]
# auto_check checks GitHub for a newer AniketosHEN build at boot.
# Available values: true, false
auto_check=true
```

## Releasing

`scripts/release_hen.sh` builds and publishes a release:

```shell
GITHUB_TOKEN=... ./scripts/release_hen.sh 0.05a "Short summary"
```

The second argument is a summary string (at most 300 characters, shown in the
Update Manager), or an existing file whose contents are used. The release notes are
[`release-notes/next.md`](../release-notes/next.md), where every player-facing
change is added as it is made (see [AGENTS.md](../AGENTS.md)); `NOTES_FILE` names
another file. Its `## Fixed` section becomes Fixes on the Discord card and the
other sections Added. After the release, empty it for the next one. The script builds Release with `ANIKETOSHEN_VERSION=<version>` and,
unless the caller sets `ANIKETOSHEN_REPORT_URL`, the project's crash-report
relay; copies `build/bin/AniketosHEN.elf`, writes `AniketosHEN.elf.sha256`,
generates `update.json` and publishes through the GitHub REST API with `curl`.
It does not use `gh`, and on macOS it runs inside the Linux build image (it
uses GNU `stat`/`date`/`sha256sum`).

Releases use tag `v<version>` and become GitHub's latest release; the asset URL
is `https://github.com/xS3Cx/AniketosHEN/releases/download/v<version>/AniketosHEN.elf`.

`published_at` uses `SOURCE_DATE_EPOCH` when set (useful for reproducible test
builds); otherwise it is the current UTC time. The JSON object is written with a
fixed field order and two-space indentation.
