## Added
- Toolbox > x5Guard™: a new guard. Its PlayStation Network Blocker (on by default) stops every PSN sign-in, connection and system update attempt, keeps automatic system, game and app updates off, and shows an x5Guard™ banner with a warning sound saying what it blocked.
- Game plugins: .sprx, .prx and .elf plugins in /data/AniketosHEN/pluginloader/<title ID> (or /all) load when the game starts; switch them per game in Toolbox > Game Tools > Game plugins or the game's Options > ★ Plugins; a "PluginLoader" notification lists what loaded.
- SPRX loader: GoldHEN-style plugins.ini (/data/AniketosHEN/plugins.ini or /data/GoldHEN/plugins.ini).
- Game tiles: custom game icons, also animated GIF/WebP: /data/AniketosHEN/ShellUI/icons/<title ID>.gif.
- Preferences > PIN lock: an optional PIN before the Toolbox, Shell Menu and HEN menu open (forgot it? delete /data/AniketosHEN/pin_lock).
- System look: the HEN button can show a toolbox icon in the system's own style.

## Changed
- Start-up: the "Loading Kstuff / Loading ShadowMountPlus" notification is gone.

## Fixed
- Home: L1/R1 move through every visible tab, custom tabs included.
- Updating no longer switches kstuff and ShadowMountPlus off.
- kstuff and ShadowMountPlus never start twice.
- ShadowMountPlus starts when kstuff comes from elsewhere; with no kstuff at all you get a notification.
- Autostart: the same payload on internal storage and USB starts only once, and the controls ignore input while the Control Center is open.
- HEN menu: a choice in a payload's OPTIONS menu no longer installs a package.
- Payload Manager: updating a renamed payload no longer leaves the old copy behind.
- A missing file on the server says "not on the server" instead of a network error.
- A payload that crashes the console during Autostart makes the next start a safe one.
- Start-up: the Toolbox retries getting into the system UI instead of staying off.
- Games: no more stray "ELF failed to inject!" notification.
