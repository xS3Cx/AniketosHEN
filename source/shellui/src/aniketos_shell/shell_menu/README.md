# Shell Menu implementation partitions

`shell_menu.cpp` owns the frontend state and public lifecycle. The files in
this directory group the private implementation by responsibility while they
remain part of the same C++ translation unit:

- `background_catalog.inc` — custom background discovery and selection.
- `ui_primitives.inc` — Mono/PUI object lifetime and basic widget helpers.
- `page_model.inc` — page labels, option descriptors, and page construction.
- `view.inc` — widget creation, layout, overlays, clock, and animations.
- `option_controls.inc` — the single Appendum-compatible checkbox and slider
  renderer used by Akari and Anomaly in ShellUI.
- `anomaly_theme.inc` — presentation-only Anomaly layout and its dedicated
  under-menu button legend.
- `apparition_theme.inc` — presentation-only Apparition layout: its layered
  dark-grey HUD, `#3998FE` accent lines, full-width scroller, title, and
  compact checkbox treatment are recreated in PUI from CF4x99's reference.
- `settings_actions.inc` — settings targets, mutations, reset, and actions.

This layout is deliberate. Mono objects are pinned and retained through shared
handles whose initialization and release order is runtime-sensitive. A later
refactor can introduce explicit state/view classes and separate translation
units after this behavior-preserving split has been validated on hardware.
