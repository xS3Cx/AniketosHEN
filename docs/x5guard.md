# x5Guard™

x5Guard™ is AniketosHEN's guard inside ShellUI. A guard is made of
**shields**; each shield stops one kind of action that would cost the user the
jailbreak and reports it. The first shield is the **PlayStation Network
Blocker** (`Shield::PsnBlocker`): no PSN sign-in, no PSN connection, no system
update. User-facing behaviour is in the [README](../README.md#x5guard).

nanoDNS already sinkholes PSN's servers at the DNS level; x5Guard works on the
interface, so the user is never invited into a sign-in or an update.

## Layout

| Path | Role |
| --- | --- |
| `source/shellui/include/x5guard/guard.hpp`, `src/x5guard/guard.cpp` | Core: `Enabled(Shield)`, `Report(Shield, event, detail)`, `BlockedCount()`, `LastBlocked()` |
| `source/shellui/include/x5guard/guard_banner.hpp`, `src/x5guard/guard_banner.cpp` | `ShowBanner(utf8)`: the top banner |
| `source/shellui/include/x5guard/psn_shield.hpp`, `src/x5guard/psn_shield.cpp`, `psn_shield_js.inc` | JS shield (`X5Guard::PsnShield`) for the React Native apps |
| `source/shellui/include/x5guard/native_shields.hpp`, `src/x5guard/native_shields.cpp` | Native shields: detours on managed ShellUI methods |
| `source/libonion_settings/include/onion/shell_menu_settings.def` | `x5guard_psn_blocker` (default `true`), `x5guard_alerts` (default `true`) |
| `source/shellui/src/toolbox_xml.cpp`, `toolbox_values.cpp` | Toolbox → x5Guard™ page |

The `src/x5guard/*.cpp` files are picked up by the ShellUI CMake glob.

## Flow

```
RN app (JS shield)  --console.log("ANIKETOS:x5guard|<event>|<detail>")-->  script_injector console_log_hook --+
managed detour (native shield) ------------------------------------------------------------------------------+--> X5Guard::Report
                                                                                                                  |  counter, last text
                                                                                                                  |  rate limit
                                                                                                                  |  /data/AniketosHEN/x5guard.log (capped)
                                                                                                                  +--> ShowBanner (if x5guard_alerts)
```

**Core (`guard.cpp`).** `Report` builds the banner `<warning sign U+26A0 U+FE0E>
x5Guard™ <what> blocked` (template `x5guard.banner_fmt`), where `<what>` is the
stopped call or link itself, uncut (`SigninUtility.Open()`,
`psaccounts:signin`, `NUC294`); the longer technical label (`guard_policy.hpp`)
is what Toolbox shows as Last blocked. It counts the block, appends a line to
`x5guard.log` (size-capped) and shows the banner unless Guard alerts is off. A
rate limit (the same block once per 2.5 s, any banner once per 0.4 s) keeps one
user action, which often trips several hooks at once, to one banner. Any
thread may call it.

**Banner (`guard_banner.cpp`).** The notification overlay's
`PersistentToast` template, the one Remote Play's "connected" pill uses, sent
with `sceNotificationSendById` (looked up by name at install, priority High).
Text is UTF-8, console glyphs (private-use code points) render. The overlay
draws it in fixed white; the warning sign is a yellow emoji, and the JS
shield in the overlay paints `x5Guard™` the same yellow as a nested Text. Apps that
ShellUI starts with itself run before the script hook is in, so the
notification overlay is reloaded once a few seconds after the HEN is up
(`PsnShield::Tick`). Each banner shown also queues the system's own warning
sound (PUI `Application.PlaySystemSound`, `DisabledWidgetPressed`), played on
the UI thread on the next frame.

**JS shield (`psn_shield`).** `script_injector` evaluates
`psn_shield_js.inc` before the bundle of each app `WantsApp()` accepts
(`elysion` for the store, `universal-checkout`, `millennium-falcon` and the
`rnps-*` apps: explore hub, control center, profile, settings, trophy, game
hub, library, media gallery, capture menu, agent popup, action cards,
notification overlay, system modal dialog). The script wraps the sign-in and
update entry points of `NativeModules` (`VshNMPsnShellUISigninUtility`,
`VshNMSystemUpdaterModule`, `VshNMSystemModalDialogModule`,
`VshNMApplicationLinkingPSModule`, `VshNMSettingsSystem`, ...), turns the
call into a no-op and logs the report line. The switch is read when an app
opens, so a change applies to apps opened afterwards.

**Native shields (`native_shields.cpp`).** Detours on managed ShellUI methods
that reach PSN without going through an RN app (error dialog sign-in, network
test, sign-up, update launch). They are installed next to the other managed
detours and report `native.*` events.

Parts of this are still being finished on `feat/psn-disabled`; where the code
and this page differ, the code is right and this page needs a fix.

## Event catalog

| Code | What was stopped |
| --- | --- |
| `signin.utility` | `VshNMPsnShellUISigninUtility.Open` |
| `signin.link` | `psaccounts:signin` deep link |
| `signin.app` | an app launched for sign-in (closed) |
| `update.link` | `pssmdlg` `SystemUpdaterDialog` / pssettings update links |
| `update.dialog` | `SystemModalDialog.Open` of a `SystemUpdater*` dialog |
| `update.app` | the updater dialog app (closed at launch) |
| `update.process` | `VshNMSystemUpdaterModule.<method>` |
| `update.auto` | registry write of the automatic download/install switches |
| `update.feature` | System Feature Updates (`CheckRnpsApps`) |
| `update.check` | update availability query; silent (counted and logged, no banner) |
| `notify.drop` | a system notification inviting sign-in or an update; detail is the NUC id or `Pup` |
| `native.*` | managed detours: `native.errordialog`, `native.nettest`, `native.signup`, `native.launchupdate`, ... |

`detail` is the technical name of what was stopped (method, link, use case
id). Add a code to the label table in `guard.cpp` when you add one here.

## Adding a shield

1. Add a value to `enum class Shield` in `guard.hpp` and a bool setting
   (`x5guard_<name>`) to `shell_menu_settings.def`; map it in `Enabled()`.
2. Add a switch row to the x5Guard™ page of the Toolbox and its `x5guard.*`
   strings to all 14 files in `source/i18n/`, translated.
3. Put the hooks in a new `src/x5guard/<name>.cpp`. Check `Enabled()` at the
   moment of the action, not at install time, so the switch works without a
   reload.
4. Call `Report(shield, "<area>.<what>", detail)` for each stop and add the
   codes and labels to the catalog above and in `guard.cpp`.
5. README (no version), a line in `release-notes/next.md`.

## Resolving things dynamically

Nothing is tied to a firmware: no offsets, no addresses, no per-firmware
tables.

- RN apps are matched by the executor's app name (`elysion`, `rnps-...`).
- Native modules are found by name on `NativeModules` and wrapped only if the
  method exists; a missing module or method is skipped.
- Dialogs, notifications and links are recognised by their use case id,
  `msgid` / NUC id, URL scheme (`psaccounts:`, `pssmdlg:`) or dialog type
  name.
- Managed detours resolve class and method by name through the Mono runtime;
  sysmodule functions by NID.

## Safety rules

- **Fail soft.** If a name is not found, log once and leave that path alone;
  never crash ShellUI. An unblocked path is better than a restart loop.
- **No boot-path hooks.** Nothing is installed in code that runs during
  ShellUI start-up before Home is up; a fault there means a console that does
  not boot to Home.
- **Runtime only.** Nothing is written outside `/data/AniketosHEN`; no system
  file or registry default is changed by the guard itself.
- **No side effects before verified.** A new hook starts log-only; it blocks
  only once the log shows it fires for the right action and nothing else.
- JS injected into an app is ASCII only; non-ASCII text (the banner glyph,
  `™`) stays native.

## Testing

- Klog over TCP port 3232 (the `klogsrv` payload) shows the live kernel log,
  including the ShellUI module's lines.
- `ANIKETOS:` lines from injected scripts land in
  `/data/AniketosHEN/AniketosHEN_shellui.log`; grep `ANIKETOS:x5guard` to see
  every report the JS shield sent.
- `/data/AniketosHEN/x5guard.log` lists every block with its code and detail;
  the Toolbox page shows the count and the last one.
- Test passively on one console first: open the screen, press the button,
  read the log. No load or busy-loop tests on a console.
