# AniketosHEN SPRX Loader

`libonion_sprx` provides a reusable, synchronous use-case for loading a
`.sprx`/`.prx` module into an already-running PS5 process. It is intentionally
separate from the FPS sampler and from the ELF plugin manager.

## Design

The public `SprxLoader` owns validation and idempotency policy. It depends only
on `IRemoteSprxRuntime`, so host tests or another platform can provide a fake
runtime without linking ptrace or PS5 kernel code. `PtraceSprxRuntime` is the
production adapter and contains the PS5-specific details:

1. Elevate the loader process to `PTRACE_AUTHID` for one scoped operation.
2. Attach to the target and resolve `sceKernelLoadStartModule` and
   `pthread_create` by NID.
3. Allocate three temporary target pages for context, path, and thread data.
4. Start a short remote thread thunk. The thunk calls
   `sceKernelLoadStartModule(path, 0, 0, 0, 0, 0)` and records its return code.
5. Detach while the module initializes and poll the target's dynlib table.
6. Reattach only after the thunk completed, then unmap temporary pages.

The loader does not inject a persistent ELF payload and does not leave a
custom SPRX resident beyond the module requested by the caller.

## Usage

```cpp
#include <onion/sprx_loader.hpp>

onion::sprx::PtraceSprxRuntime runtime;
onion::sprx::NoopTargetAccessPolicy access;
onion::sprx::TitleIdAllowlist allowlist;
allowlist.add_exact("CUSA12345");
onion::sprx::SprxLoader loader(runtime, access, allowlist);

const auto result = loader.load({
    .pid = game_pid,
    .path = "/data/plugins/example.sprx",
    .options = {.timeout_ms = 5000, .poll_ms = 10},
});

if (!result.succeeded()) {
  // load_status_name(result.status), result.remote_result
}
```

The target must be able to resolve the two NIDs and access the requested path.
For a sandboxed game, the caller must apply the project's existing target
jailbreak/privilege policy before loading, or provide a runtime adapter that
does so. The framework deliberately does not silently jailbreak arbitrary
processes.

## Why this is not wired into FPS

FPS currently uses daemon-side `/dev/dce`, DMAP, and a BC GNM detour. This
loader is a general infrastructure component for future user-requested SPRX
plugins; adding it to the FPS path would reintroduce the old FPS injection
coupling.

## GoldHEN-style plugins.ini (read-only import)

Besides the strict `sprx/catalog.ini`, the loader reads GoldHEN-style plugin
lists so existing GoldHEN plugins can be reused without rewriting them:

- `/data/AniketosHEN/plugins.ini`
- `/data/GoldHEN/plugins.ini` (imported if present; never modified)

Format is GoldHEN's own:

```ini
; comment (; or #)
[default]                              ; every PS4 (CUSA) game
/data/GoldHEN/plugins/game_patch.prx
[CUSA00419]                            ; one title; PS5-native PPSA IDs also work
/data/GoldHEN/plugins/afr.prx
/data/GoldHEN/plugins/off.prx=false    ; =false/off/0 skips the line
[settings]                             ; GoldHEN loader settings, ignored
```

File order is load order. A path listed under several sections becomes one
plugin that matches all of them. `[default]` maps to the `CUSA` prefix (PS4
titles). Imported entries are `auto_start`, their ids are `goldhen-<name>` /
`ini-<name>`, and they are managed only by editing the file — the Toolbox
shows them read-only. A path already in `catalog.ini` is left to the catalog.
Bad lines are skipped with a `LOG_WARN`; the rest of the file still loads.

### GoldHEN SDK compatibility — what works and what does not

A plugin built against the GoldHEN Plugin SDK is an Orbis PRX. The loader runs
its entry point the way GoldHEN's own `plugin_loader` does: after
`sceKernelLoadStartModule`, it resolves `plugin_load` with `sceKernelDlsym` and
calls `plugin_load(0, NULL)`; a non-zero return triggers `plugin_unload(0, NULL)`.

A PS4 plugin running inside a **PS4 backwards-compatible (CUSA) game** can load,
because the BC game process is itself an Orbis/libkernel process: the module
links against the same `libkernel`/`libSceLibcInternal` the game uses, and
`sceKernelMprotect` / `sceKernelDlsym` detours work there. What it **cannot** do
is call the GoldHEN SDK's privileged helpers, because those are not a library —
they are `syscall(500 /*GOLDHEN_SDK_CMD_*/, ...)` handlers the GoldHEN kernel
payload installs (`sys_sdk_proc_info`, `sys_sdk_proc_rw`, `sys_sdk_jailbreak`,
`sys_sdk_proc_prx_load`). On a PS5, syscall 500 is FreeBSD `readlinkat`, so those
calls return garbage or fail. In practice:

- Plugins that only hook the game's own code (Detour/Patcher on `sceKernelOpen`,
  `fopen`, `sceVideoOutSetFlipRate`, game functions) — e.g. `afr`, `force_30_fps`,
  `fliprate_remover`, `no_share_watermark` — are self-contained and work.
- Plugins that need `sys_sdk_*` (the GoldHEN loader itself, anything using
  `sys_sdk_proc_info` for the title ID, or kernel R/W) do not; AniketosHEN
  already supplies the equivalents daemon-side (title ID, jailbreak, `mdbg`
  R/W), so the useful path is porting the plugin to call those instead.

A **PS5-native (PPSA) game** has no FreeBSD/libkernel userland of that shape, so
a PS4 PRX will not even link; PS5-native mod menus are instead delivered as ELF
payloads injected over the network (etaHEN/elfldr ports 9020/9021) or as cheats,
which AniketosHEN handles through its ELF plugin runtime and cheat engine, not
this loader.

A full GoldHEN compatibility layer would need a resident shim exporting the
`sys_sdk_*` ABI (version/klog/jailbreak/proc_info/proc_rw/prx load+unload) backed
by AniketosHEN's daemon, plus matching `GOLDHEN_PATH` data layout (`afr`,
`gamepad.ini`, the patch repo). That is a separate, larger piece of work and is
intentionally out of scope here.
