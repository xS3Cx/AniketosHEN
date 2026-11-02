# Shell Menu implementation partitions

`shell_menu.cpp` holds the public lifecycle: bind and release the host, show,
hide, the per-frame `Tick` and `HandleInput`. It includes the partitions below
in this order, and they stay one C++ translation unit. A partition that was
split keeps its file as the ordered list of its parts.

- `state.inc` — constants, enums, `Option`, `Widgets`, every static of the
  unit, and the coalesced settings save.
- `background_catalog.inc` — the background folder and the chosen file's place
  in each file list.
- `ui_primitives.inc` — Mono/PUI object lifetime and basic widget helpers.
  - `menu_look.inc` — the active menu style, colours and animation clock.
  - `text_metrics.inc` — label text measuring.
- `page_model.inc` — what the pages hold.
  - `option_format.inc` — labels and value text.
  - `option_builders.inc` — row builders, the theme list, colour sliders.
  - `pages.inc` — `build_page` and its page families.
  - `selection.inc` — selection key, `ensure_selection`, `commit_selection`.
- `view.inc` — everything drawn.
  - `widgets.inc` — widget creation; creation order is runtime-sensitive.
  - `legend.inc` — the open/close legend under the menu.
  - `option_controls.inc` — the Appendum-compatible checkbox and slider
    renderer used by Akari and Anomaly.
  - `anomaly_theme.inc` — the Anomaly layout and its under-menu legend.
  - `apparition_theme.inc` — the Apparition layout: layered dark-grey HUD,
    `#3998FE` accent lines, full-width scroller, title and compact checkbox,
    recreated in PUI from CF4x99's reference.
  - `menu_view.inc` — laying out the current page.
  - `overlay.inc` — the Home scene layer and the OSD in it.
  - `clock_date.inc` — the date under the clock.
  - `page_transition.inc` — the animation between two pages.
  - `animations.inc` — open, close and highlight motion.
- `settings_actions.inc` — what the options do.
  - `theme_apply.inc` — switching the menu's look.
  - `option_targets.inc` — which setting each option reads and writes.
  - `option_groups.inc` — which feature an option belongs to.
  - `option_change.inc` — Left and Right.
  - `option_activate.inc` — Cross.
- `sweety_native.inc` — the same pages drawn by the console's own pop-up menu.

`easing.cpp` is a separate translation unit: the animation curves.

The single unit is deliberate. Mono objects are pinned and retained through
shared handles whose initialization and release order is runtime-sensitive.
Splitting it into real translation units needs a console to validate on.
