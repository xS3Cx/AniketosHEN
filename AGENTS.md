# Working on AniketosHEN (for people and AI agents)

AniketosHEN is made by AlexZero and Andriushka.exe; both push to `main` of
`xS3Cx/AniketosHEN`. Always `git pull --rebase` before you push.

## Every change players can notice
1. **Add a line to [`release-notes/next.md`](release-notes/next.md)** in the
   same commit. Sections: `## Added`, `## Changed`, `## Fixed`; one `- `
   sentence per item, player-facing wording, start with where it is in the
   menu ("Autostart: ...", "Payload Manager: ..."), no file or function names.
   Edit an existing line instead of adding a second one about the same thing.
   This file collects everything until the next release.
2. **Update [`README.md`](README.md)** when you add, remove or change a feature,
   a setting, a default or a path. The README never names a version number.
3. Keep every user-facing string in all 14 locale files of `source/i18n/`,
   translated (not English copied into the other languages).

## Releasing
- Releases are published only on GitHub, one channel, with
  `scripts/release_hen.sh <version> "<summary, max 300 chars>"` (see
  [docs/update.md](docs/update.md)). It uses `release-notes/next.md` as the
  release notes unless `NOTES_FILE` names another file. The Discord bot
  announces the release in #releases by itself; never post releases by hand.
- Before releasing: show `release-notes/next.md` and the summary to a human and
  wait for approval; `git pull --rebase`; build; run the host tests
  (`source/util/tests`), all must pass.
- After releasing: empty `release-notes/next.md` (leave the three section
  headings) and commit that together with the `ANIKETOSHEN_VERSION` bump in
  `source/cmake/AniketosOptions.cmake`.

## Do not
- Never commit tokens or keys, never create prereleases or a `beta` tag.
- Never put a version number in the README.
