## Added
- System look: "HEN button icon" can show a toolbox icon in the system's own style, like Search and Settings beside it, instead of the AniketosHEN logo in Home's top bar; Home reloads to show it.
- Preferences > PIN lock: an optional PIN (off by default) that the Toolbox, the Shell Menu and the HEN button menu ask for before they open, so children cannot change HEN settings; forgot it? Delete /data/AniketosHEN/pin_lock over FTP or put a file named no_pin_lock on the first USB drive.
- Shell Menu > Home screen > Game tiles: custom game icons. A picture named after a game's title id in /data/AniketosHEN/ShellUI/icons (for example PPSA01234.gif) replaces its tile icon on Home; animated GIF and WebP play while Home is on screen.
- SPRX loader: GoldHEN-style `plugins.ini` support. List plugins by title ID in `/data/AniketosHEN/plugins.ini` (or keep your existing `/data/GoldHEN/plugins.ini`) and they load into the matching game like GoldHEN does.
- Game plugins: put .sprx, .prx or .elf plugins in /data/AniketosHEN/pluginloader/<title ID> (or /data/AniketosHEN/pluginloader/all for every game) and they load when that game starts; Toolbox > Game Tools > Game plugins lists your games with their icons, and each game has Load on startup and a switch per plugin with its last status; a game's Options menu has ★ Plugins with the same switches.

## Changed
- Start-up: the "Loading Kstuff / Loading ShadowMountPlus" notification is gone; "AniketosHEN is starting..." and the welcome remain.
- About > Credits and the welcome notification name both makers of AniketosHEN, AlexZero and Andriushka.exe.

## Fixed
- Home: L1/R1 now move through every visible tab in the order shown, custom tabs included, and back again, with the highlight following.
- Updating from an older version no longer switches kstuff and ShadowMountPlus off; a config.ini from before they became optional keeps them on.
- Start-up: kstuff and ShadowMountPlus never start twice (the included one plus a kstuff-lite or ShadowMountPlus payload in Autostart, or after reloading the HEN).
- ShadowMountPlus starts when kstuff comes from somewhere else (the exploit chain or a kstuff-lite payload in Autostart); if there is no kstuff at all, you get a notification.
- Autostart: the same payload on internal storage and a USB drive starts only once.
- Autostart: the controls no longer react while the Control Center is open.
- HEN menu: a choice made in a payload's OPTIONS menu could be taken by the HEN menu and install a package.
- Payload Manager: updating a payload the catalog renamed no longer leaves the old copy behind, still starting with AniketosHEN.
- A missing file on the server now says "not on the server" / "no release found on GitHub" instead of a network error.
- A payload that crashes the console during Autostart now counts as an unfinished start, so the next start is a safe one.
