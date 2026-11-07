# SPRX Catalog Manifest

`SprxCatalog` uses a deliberately small INI manifest. This follows the
project's existing `config.ini` conventions and the reference `ploader.ini`
without introducing another parser or a nested JSON schema.

```ini
[plugin.base-runtime]
path=/data/AniketosHEN/sprx/base-runtime.sprx
exact_title_ids=CUSA12345,CUSA67890
priority=10

[plugin.overlay]
path=/data/AniketosHEN/sprx/overlay.sprx
exact_title_ids=CUSA12345
auto_start=true
priority=100
dependencies=base-runtime

[plugin.native]
path=/data/AniketosHEN/sprx/native.prx
title_id_prefixes=PPSA
auto_start=true
```

Each plugin has one `[plugin.<id>]` section. The ID is unique and uses the
same printable identifier rule as the existing plugin UI and plugin manager.
`path` is an absolute `.sprx` or `.prx` path; the loader performs the final
`realpath()` and regular-file checks before touching the target process.

`exact_title_ids` and `title_id_prefixes` are comma-separated allowlists. A
plugin is eligible for a target only when one of these fields matches its
Title ID. `auto_start=true` selects the roots for automatic startup. A
dependency is included in the startup plan even if it is not auto-started
itself. Dependencies must be declared and must also match the target Title
ID; missing, mismatched, duplicate, or cyclic dependencies reject the plan.

`enabled` defaults to `true` when it is absent, preserving existing catalogs.
The SPRX page can persist it as `enabled=true` or `enabled=false`. Disabling a
module only removes it from future Big App startup plans; it does not and
cannot unload a module already injected into a game process.

## Game plugins page

Players drop `.sprx`/`.prx` files in `/data/AniketosHEN/sprx/` (the daemon
creates the folder and an empty `catalog.ini` on start). The daemon's inventory
lists every catalog entry, then every file of that folder that no entry names
yet; such a file gets the ID of its name without the suffix (characters outside
the ID rule become `_`, cut to 31) and has no title IDs, which the UI shows as
"not set up".

Choosing games (Toolbox > Game Tools > Game plugins, or ★ Plugins in a game's
Options menu) writes the entry through `SprxCatalogStore::put`:
`exact_title_ids` are the chosen games, "All games" is
`title_id_prefixes=PPSA,CUSA` (prefixes written by hand are kept), and
`auto_start=true`. An entry left with no game at all is dropped from the
catalog (the file stays and shows as not set up again). `put` re-parses the
whole serialized catalog before writing it, so a change the parser would
refuse (65 title IDs, a bad ID) is rejected instead of breaking the catalog.

Remove drops the `[plugin.<id>]` entry and deletes the file when it is directly
inside the plugins folder; a path written by hand elsewhere is left alone. It
is rejected while that module is loaded in the current matching game.

Startup order is a topological order. Among entries whose dependencies are
already satisfied, larger `priority` values start first; ties are resolved by
plugin ID for deterministic behavior. Dependencies always precede their
dependents, regardless of priority.

This is intentionally not a general package-management format. Version
ranges, optional dependencies, environment expressions, permissions, and
per-plugin retry overrides are deferred until a concrete use case requires
them. Keeping those concerns out of Phase 2 makes malformed manifests easy to
reject and keeps policy in `SprxLoader` rather than duplicating it here.
