# NOTICE

AniketosHEN is a fork of **OnionHEN**
(<https://github.com/aydencharles/onionHEN>), which is itself derived from
**etaHEN** (<https://github.com/LightningMods/etaHEN>).

## Authorship

The software in this repository was written by the OnionHEN authors and
contributors — 麒麟/0xp0co, kvnhrt and others — and by LightningMods and the
etaHEN contributors before them. Their copyright notices
(`Copyright (C) … OnionHEN / LightningMods`, and those of John Törnblom, Ben
Hoyt and the other third-party authors) remain in every source file exactly as
upstream published them. The projects this one is based on, refers to and
embeds are listed in the *Credits* section of [README.md](README.md); the
Toolbox *About* page carries the lineage and the list of included projects.

AniketosHEN does not claim authorship of that work.

## License

OnionHEN is distributed under the GNU General Public License, version 3
(see [LICENSE](LICENSE); the per-file headers say "either version 3, or (at
your option) any later version"). AniketosHEN is distributed under the same
terms. Components under `third_party/` and files that carry their own notice
(for example `source/include/ini.h`, BSD-3-Clause) keep their own licenses.

## Changes made by this fork

As required by section 5(a) of the GPL, the modifications relative to
OnionHEN at commit `b23ffe6` are:

- The project is named AniketosHEN in user-facing text, logs,
  notifications, documentation, build output (`AniketosHEN.elf`) and
  container/service names.
- Persistent data moved from `/data/OnionHEN` to `/data/AniketosHEN`
  (and the `/user/data` view of each). A one-time, non-destructive import of
  an existing OnionHEN data directory was added
  (`source/libonion_platform/source/migrate.c`).
- The settings library reads OnionHEN's `config.ini` as a fallback and accepts
  OnionHEN's `show_onionhen_options` key as an alias of
  `show_aniketoshen_options`.
- The project's identity is defined in one place
  (`source/include/onion/distribution.h`).
- The bug-report address in the crash notification became a build-time setting
  instead of OnionHEN's issue tracker.
- An OnionHEN lineage line and project link were added to the Toolbox *About*
  page.

- A Shell Menu (L1+R3) and shell-appearance features were added, written by
  this fork's maintainer. The visual references they follow (Akari,
  Apparition) are listed in
  [docs/shell-menu.md](docs/shell-menu.md). One crit-daemon command (restart of the
  system interface) and a `[shell_menu]` settings section were added for them.

- Features added by the fork in the ShellUI module (`source/shellui`), all of
  them looked up by name at run time rather than by firmware offset:
  - Home screen: a looping video or a still picture behind Home, drifting
    particles, an "Emulators" tab, a status dot on the user's avatar (also in
    Control Center), and a top-bar entry on firmware the byte patch has no
    profile for;
  - the top-bar button's menu: Toolbox, package installation from `/data`
    and external drives, the Internet connection switch, unloading;
  - Shell Menu: an optional native (PUI3) rendering, a hardware information
    page (processor, kernel, drive, masked console identifiers shown only
    after a confirmation), alternative looks for the system's on/off
    switches, free-space and memory readouts, friend-style test notifications;
  - Settings: the Debug Settings entry is renamed and given the fork's icon on
    every firmware that has it, not only on a list of known builds;
  - fixes: the firmware version is derived from the version number, selection
    in the Shell Menu no longer resets when a live reading changes.
- Settings added to `source/libonion_settings` for the above, and strings in
  all fourteen languages of `source/i18n`.

The kernel-side helpers, the cheat engine, the plugin runtime and the loaders
are as OnionHEN has them. Every OnionHEN name that was deliberately kept is
listed in [docs/aniketoshen-migration.md](docs/aniketoshen-migration.md).

## Not affiliated

AniketosHEN is not endorsed by, or affiliated with, the OnionHEN or etaHEN
projects or Sony Interactive Entertainment.

## Maintainer

AniketosHEN is maintained by AlexZero. The welcome notification's "made by"
line and the Toolbox *About → Find me on* page name the maintainer of this
fork; they are not a claim of authorship over the OnionHEN and etaHEN code,
whose authors are credited under *About → Credits → Lineage*, in the README
and above. OnionHEN's donation page, donor list, contributor list and tester
list were removed from the Toolbox and the README because they describe
OnionHEN, not this fork; they are in OnionHEN's own repository.
