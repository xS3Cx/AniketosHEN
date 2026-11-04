# Home tabs

Home has exactly four tabs: **Games** (Tab 1), **Media** (Tab 2), **Tab 3**
and **Tab 4**. Games and Media are always there and cannot be deleted; the two
custom tabs are optional. All four can be renamed, and each has an icon shown
in front of its caption while *Tab icons* is on.

The feature is split between:

- `source/shellui/src/aniketos_shell/home/home_tabs_store.cpp` — the data file,
  the Copy / Move menu, tab creation and editing, icon validation and the
  registry answer Home's script uses to follow title changes;
- `source/shellui/src/aniketos_shell/home/home_tabs.cpp` — the tab bar's
  captions, the names and icons of the Games and Media tabs, and their prompts;
- `source/shellui/src/aniketos_shell/home/home_rows.cpp` — the query hook that
  gives each custom tab its row of tiles and adds or removes titles in the
  Games and Media rows;
- `source/shellui/src/aniketos_shell/rn_scripts/script_injector.cpp` and
  `home_inject_js.inc` — the spaces added to Home's React tab bar.

## Data file

`/data/AniketosHEN/home_tabs.json`, version 2:

```json
{
  "version": 2,
  "revision": 1,
  "tabs": [
    {
      "id": "c1",
      "name": "Favourites",
      "icon": "\uEFAA",
      "visible": true,
      "titles": ["PPSA01234"]
    }
  ],
  "games": [],
  "media": [],
  "moved": []
}
```

- `tabs` holds at most two objects. `id` is `c1` or `c2` and decides
  the tab's content limit (`7780`, `7781`). The array order is the tab
  order in the bar.
- `name` is the user's name. `icon` is one system private-use glyph
  (`U+E000`–`U+F8FF`). `visible` hides the tab from Home without deleting it
  or its titles. `titles` are the title IDs shown in the tab.
- `games` and `media` are titles added to the stock rows by Copy or Move.
- `moved` are titles moved out of the stock rows. A moved title stays out of
  its stock row only while a visible custom tab holds it; hide or delete that
  tab and the title returns to its stock row.

Every field is validated on load. A file that is not valid JSON, not version
2, or cannot be read is treated as empty and the problem is logged; the bad
file is left in place. Missing title IDs, duplicate tab IDs and bad icons are
skipped or replaced with the default icon. The file is written atomically with
`write_file_atomic` and then `chmod 0644`.

`/data/AniketosHEN/home_bookmark.json` is not migrated. If it exists at
startup it is deleted and a fresh `home_tabs.json` is created.

## Limits

- At most two custom tabs, giving the four-tab Home the feature promises.
  Creating another one is refused with a notification.
- Custom tabs can be created, renamed, shown or hidden, reordered among
  themselves and deleted from **Home screen → Tabs** in the Shell Menu.
- Games and Media can only be renamed (and have their icon changed); they are
  never hidden or deleted.
- Restoring the system names clears the Games and Media names only. Custom
  tab names are always the user's.

## Copy, Move and Remove

A game's or app's Options menu gets **★ Copy / Move**. It opens *Copy to*,
*Move to* and *Remove from*, each listing Games, Media and every visible custom
tab, followed by **New tab...** while a slot is free.

- **Copy** adds the title to the chosen tab and leaves it everywhere else it
  was.
- **Move** removes the title from every other tab and from the `games` and
  `media` lists, then adds it to the chosen tab. Moving to a custom tab also
  marks it in `moved`, which takes it out of both stock rows while that tab is
  visible. Moving to Games or Media adds it to that stock row and marks it in
  `moved`, which takes it out of the other stock row.
- **Remove from** lists only the tabs that hold the title. Removing it from a
  custom tab may return it to its stock row when no visible custom tab holds it
  any more.

The Copy / Move submenu uses the plain tab names; the captions in the bar may
carry the icon.

## Hide App

Hide App is independent of tabs. A hidden title is filtered from the Games row,
the Media row and every custom tab. Restoring it in **Console → Hidden apps**
puts it back in every list it belonged to. The query hook always appends the
hidden list last.

## Icons

Icons are system private-use glyphs (`U+E000`–`U+F8FF`) only; Unicode emoji are
not accepted. Games and Media icons are stored in the `home_tab_games_icon` and
`home_tab_media_icon` settings keys. Custom tab icons live in
`home_tabs.json`. Defaults are:

| Tab | Glyph | Meaning |
| --- | --- | --- |
| Games | `U+EFCE` | controller |
| Media | `U+F871` | TV |
| Custom | `U+EFAA` | Aniketos star |

The Shell Menu's Tabs page has an *Icon* action for every tab. It opens a
system prompt; the user picks a glyph with the system keyboard's glyph key
(`osk_glyph_key`, on by default). An empty field restores the default icon. A
value that is not exactly one code point, is outside the private-use range, or
is not in `osk_glyph_table.inc` is rejected with a notification and nothing is
changed.

A caption is the icon, a space and the name when *Tab icons* is on and the icon
is not empty; otherwise it is the name alone.

## Reloads and revisions

Changing the tab set, a name, an icon, visibility or the order asks
`shellui_request_home_reload()`, because Home builds its tab bar when it
starts. Adding or removing a title only increments `revision`; the script
reads `aniketos.home_tabs` through Home's registry module every two seconds
and asks Home's title store to fetch the rows again. This keeps a Copy or Move
instant without reloading Home.

## Failure behaviour

- A corrupt or unreadable `home_tabs.json` behaves as empty and never aborts
  the HEN; the next successful write replaces it.
- If the query hook cannot be installed there is nothing to fill a custom tab
  with, so the script is not injected for Home.
- If the registry hook cannot be installed, the Options menu entry is not
  offered.
- With `no_home_js` in `/data/AniketosHEN` Home stays as the system has it:
  no custom tabs, and the Games and Media rows keep their stock contents.

## Console test caveats

The host tests cannot run this module: it depends on ShellUI's Mono runtime,
PUI dialogs and the system keyboard. The following still need a console:

- the tab bar with zero, one and two custom tabs, including L1/R1
  moving through them and back to Games;
- creating a tab from the Options menu and from the Shell Menu, including the
  name prompt;
- renaming, changing the icon, hiding, showing, reordering and deleting each
  custom tab;
- copying and moving the same title into several tabs, and moving it back to
  Games or Media;
- deleting or hiding the last tab holding a moved title, and checking that the
  title returns to its stock row;
- Hide App on a title that is in several custom tabs, then restoring it;
- the icon prompt with an invalid, empty and valid glyph;
- a corrupt `home_tabs.json` and a leftover `home_bookmark.json` on startup.
