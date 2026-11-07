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
