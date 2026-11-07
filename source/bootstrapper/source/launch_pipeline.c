/* Copyright (C) 2026 OnionHEN / LightningMods */

#include <onion/distribution.h>
#include "launch_pipeline.h"

#include "bootstrap_notify.h"
#include "bootstrap_safety.h"
#include "kstuff_probe.h"

#include <elfldr_remote.h>
#include <onion/log.h>
#include <onion/payload.h>
#include <onion/platform.h>
#include <onion/proc_query.h>
#include <onion/ready.h>

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern uint8_t daemon_start[];
extern const unsigned int daemon_size;
extern uint8_t util_start[];
extern const unsigned int util_size;
extern uint8_t onion_elfldr_start[];
extern const unsigned int onion_elfldr_size;
extern uint8_t kstuff_start[];
extern const unsigned int kstuff_size;
extern uint8_t shadowmount_start[];
extern const unsigned int shadowmount_size;

int sceKernelGetProcessName(int pid, char *name);
int sceKernelMprotect(void *addr, size_t len, int prot);

typedef struct LaunchContext {
  uint16_t loader_port;
} LaunchContext;

/* The service's pid when its marker points at it running, else -1. */
static pid_t owned_service_pid(const char *ready_name,
                               const char *expected_process_name) {
  pid_t pid = -1;
  if (!onion_ready_read_pid(ready_name, &pid) || pid <= 1)
    return -1;

  char process_name[32] = {0};
  if (!onion_proc_is_alive(pid) ||
      sceKernelGetProcessName(pid, process_name) != 0 ||
      strcmp(process_name, expected_process_name) != 0) {
    LOG_WARN("Ignoring stale %s ownership marker pid=%d", ready_name,
             (int)pid);
    return -1;
  }
  return pid;
}

bool bootstrap_stack_running(void) {
  return owned_service_pid(ONION_READY_DAEMON, "onion_daemon.elf") > 0;
}

static void stop_owned_service(const char *ready_name,
                               const char *expected_process_name) {
  const pid_t pid = owned_service_pid(ready_name, expected_process_name);
  if (pid <= 0) {
    onion_ready_clear(ready_name);
    return;
  }

  LOG_INFO("Stopping owned %s pid=%d", ready_name, (int)pid);
  if (kill(pid, SIGKILL) != 0 && errno != ESRCH)
    LOG_ERROR("Failed to stop owned %s pid=%d: %s", ready_name, (int)pid,
              strerror(errno));
  onion_ready_clear(ready_name);
}

static bool loader_available(uint16_t port) {
  return port == ONION_ELFLDR_PORT ? elfldr_remote_onion_available()
                                   : elfldr_remote_available_on(port);
}

static bool wait_onion_loader(int timeout_ms) {
  const int poll_ms = 200;
  for (int waited = 0; waited < timeout_ms; waited += poll_ms) {
    if (elfldr_remote_onion_available())
      return true;
    usleep((useconds_t)poll_ms * 1000);
  }
  return false;
}

static bool ensure_private_loader(void) {
  if (elfldr_remote_onion_available()) {
    LOG_WARN("embedded elfldr :9020 already available");
    return true;
  }

  LOG_DEBUG("Starting embedded elfldr on %u via external %u ...",
            ONION_ELFLDR_PORT, ELFLDR_REMOTE_PORT);
  if (!elfldr_remote_send_bytes_to(ELFLDR_REMOTE_PORT, onion_elfldr_start,
                                   onion_elfldr_size)) {
    LOG_ERROR("  embedded elfldr launch send failed");
    return false;
  }

  if (wait_onion_loader(10000)) {
    LOG_DEBUG("  embedded elfldr :9020 ready");
    return true;
  }

  LOG_DEBUG("  embedded elfldr :9020 did not become ready");
  return false;
}

static bool launch_blob(LaunchContext *context, const uint8_t *elf, size_t size,
                        const char *label, const char *wait_name) {
  if (context->loader_port != ELFLDR_REMOTE_PORT &&
      !loader_available(context->loader_port)) {
    context->loader_port = ELFLDR_REMOTE_PORT;
  }

  LOG_DEBUG("%u memory ELF: %s (%zu bytes)", context->loader_port, label,
            size);
  if (!elfldr_remote_send_bytes_to(context->loader_port, elf, size)) {
    if (context->loader_port != ELFLDR_REMOTE_PORT &&
        elfldr_remote_send_bytes_to(ELFLDR_REMOTE_PORT, elf, size)) {
      LOG_ERROR("  send via %u failed; fallback %u accepted %s",
                context->loader_port, ELFLDR_REMOTE_PORT, label);
      context->loader_port = ELFLDR_REMOTE_PORT;
    } else {
      LOG_ERROR("  send FAILED %s", label);
      return false;
    }
  }

  for (int i = 0; i < 30; ++i) {
    if (wait_name && onion_find_pid_substr(wait_name) > 0) {
      LOG_DEBUG("  running: %s", wait_name);
      return true;
    }
    sleep(1);
  }
  LOG_DEBUG("  sent %s (process name not seen yet, continuing)", label);
  return true;
}

static void prepare_service_markers(void) {
  stop_owned_service(ONION_READY_DAEMON, "onion_daemon.elf");
  stop_owned_service(ONION_READY_UTIL, "onion_util.elf");
  onion_ready_clear(ONION_READY_KSTUFF);
  onion_ready_clear(ONION_FLAG_UTIL_BOOTED);
  onion_ready_clear(ONION_FLAG_FPS_OVERLAY);
  onion_ready_clear(ONION_FLAG_BOOTSTRAP_DONE);
}

static bool launch_util(LaunchContext *context) {
  LOG_DEBUG("Starting util via %u ...", context->loader_port);
  if (!launch_blob(context, util_start, util_size, "util", "onion_util.elf")) {
    bootstrap_notify("notify.elfldr.launch_util");
    return false;
  }
  if (!onion_ready_wait(ONION_READY_UTIL, 15000, 200))
    LOG_WARN("util ready timeout -- continuing (process may still be starting)");
  return true;
}

/* Set when ShadowMountPlus was wanted but kstuff was not up yet: a kstuff
   from the user's autostart (kstuff-lite) may bring it up later. */
static bool s_shadowmount_deferred = false;

static void launch_shadowmount(LaunchContext *context,
                               uint32_t firmware_version, bool autoload) {
  const bool disabled_by_usb = if_exists("/mnt/usb0/no_shadowmount");
  if (disabled_by_usb || !autoload) {
    LOG_DEBUG("shadowmount disabled (%s)",
              disabled_by_usb ? "usb no_shadowmount"
                              : "shadowmount.autoload=false");
    return;
  }
  /* A HEN reload leaves the earlier one running; a second copy would fight
     it over the same mounts. */
  if (onion_find_pid_substr("shadowmount") > 0) {
    LOG_INFO("ShadowMountPlus already running -- skip launch");
    return;
  }
  if (firmware_version < 0x3000000) {
    LOG_DEBUG("shadowmount skipped on firmware %x.%02x",
              (firmware_version >> 24) & 0xffu,
              (firmware_version >> 16) & 0xffu);
    return;
  }
  LOG_DEBUG("Loading ShadowMountPlus via %u (after kstuff) ...",
            context->loader_port);
  if (!launch_blob(context, shadowmount_start, (size_t)shadowmount_size,
                   "shadowmount", "shadowmountplus.elf")) {
    LOG_ERROR("ShadowMountPlus launch failed");
    bootstrap_notify("notify.payload.load_failed", "shadowmountplus.elf",
                     "shadowmount");
    return;
  }
  LOG_DEBUG("[SHADOWMOUNT] launched %zu bytes",
            (size_t)shadowmount_size);
}

static bool launch_kstuff(LaunchContext *context, uint32_t firmware_version,
                          bool autoload, bool user_override) {
  char probe[100] = {0};
  /* Whoever loaded it (the exploit chain, a previous start, a kstuff
     payload), a running kstuff is ready for what needs it. */
  if (firmware_version >= 0x3000000 && kstuff_already_running()) {
    LOG_WARN("kstuff already running / mprotect OK -- skip launch");
    onion_ready_signal(ONION_READY_KSTUFF);
    return true;
  }
  const bool disabled_by_usb = if_exists("/mnt/usb0/no_kstuff");
  if (disabled_by_usb || !autoload) {
    LOG_DEBUG("kstuff disabled (%s)",
              disabled_by_usb ? "usb no_kstuff" : "kstuff.autoload=false");
    onion_ready_signal(ONION_READY_KSTUFF);
    return false;
  }

  if (firmware_version < 0x3000000) {
    onion_ready_signal(ONION_READY_KSTUFF);
    return false;
  }

  if (kstuff_already_running()) {
    LOG_WARN("kstuff already running / mprotect OK -- skip launch");
    onion_ready_signal(ONION_READY_KSTUFF);
    return true;
  }

  LOG_DEBUG("Loading kstuff via %u (before daemon/toolbox) ...",
            context->loader_port);
  size_t override_size = 0;
  uint8_t *override_elf = NULL;
  if (!user_override && if_exists(ONION_DIST_DATA_ROOT "/kstuff.elf"))
    LOG_WARN("safe start: ignoring " ONION_DIST_DATA_ROOT
             "/kstuff.elf, using the built-in kstuff");
  else if (if_exists(ONION_DIST_DATA_ROOT "/kstuff.elf"))
    override_elf = onion_payload_read_file(ONION_DIST_DATA_ROOT "/kstuff.elf",
                                           &override_size);

  /* A kstuff.elf the user put in place is their choice for their firmware;
     the built-in one only runs where it is known to work. */
  if (!override_elf && bootstrap_kstuff_too_new(firmware_version)) {
    onion_ready_signal(ONION_READY_KSTUFF);
    return false;
  }

  const uint8_t *elf = override_elf ? override_elf : kstuff_start;
  const size_t size = override_elf ? override_size : (size_t)kstuff_size;
  const bool sent =
      launch_blob(context, elf, size, "kstuff", "kstuff.elf");
  free(override_elf);
  if (!sent) {
    bootstrap_notify("notify.kstuff.load_elfldr_failed");
    return false;
  }

  for (int waited = 0; waited <= 15; ++waited) {
    if (sceKernelMprotect(probe, sizeof(probe), 0x7) == 0) {
      LOG_DEBUG("kstuff mprotect OK -- signal ready");
      onion_ready_signal(ONION_READY_KSTUFF);
      sleep(1);
      return true;
    }
    sleep(1);
  }
  bootstrap_notify("notify.kstuff.load_failed");
  return false;
}

static bool launch_daemon(LaunchContext *context) {
  LOG_DEBUG("Starting daemon via %u (toolbox inject) ...",
            context->loader_port);
  if (!launch_blob(context, daemon_start, daemon_size, "daemon",
                   "onion_daemon.elf")) {
    bootstrap_notify("notify.elfldr.launch_daemon");
    return false;
  }
  if (!onion_ready_wait(ONION_READY_DAEMON, 20000, 200))
    LOG_WARN("daemon ready timeout -- continuing");
  return true;
}

int bootstrap_launch_services(uint32_t firmware_version,
                              const BootstrapLaunchPlan *plan) {
  const BootstrapLaunchPlan normal = {true, true, true, true};
  if (!plan)
    plan = &normal;
  if (!elfldr_remote_available()) {
    LOG_DEBUG("FATAL: no elfldr on 127.0.0.1:9021");
    bootstrap_notify("notify.elfldr.need_9021");
    return -2;
  }

  LaunchContext context = {
      .loader_port = ensure_private_loader() ? ONION_ELFLDR_PORT
                                             : ELFLDR_REMOTE_PORT,
  };
  LOG_DEBUG("payload loader port: %u", context.loader_port);
  LOG_DEBUG("launching embedded ELFs (serialized)");
  sleep(3);

  prepare_service_markers();
  if (!launch_util(&context))
    return -2;
  const bool kstuff_ready = launch_kstuff(
      &context, firmware_version, plan->kstuff_autoload,
      plan->kstuff_user_override);
  if (kstuff_ready && plan->shadowmount_autoload) {
    sleep(2);
    launch_shadowmount(&context, firmware_version,
                       plan->shadowmount_autoload);
  } else if (!kstuff_ready && plan->shadowmount_autoload) {
    s_shadowmount_deferred = true;
    LOG_WARN("ShadowMount waits: kstuff not ready, tried again after autostart");
  }
  if (!plan->launch_daemon) {
    LOG_WARN("recovery start: daemon not launched, ShellUI left untouched");
    return 0;
  }
  return launch_daemon(&context) ? 0 : -2;
}

void bootstrap_launch_shadowmount_late(uint32_t firmware_version) {
  if (!s_shadowmount_deferred)
    return;
  s_shadowmount_deferred = false;
  if (firmware_version < 0x3000000 || !kstuff_already_running()) {
    LOG_WARN("ShadowMount skipped: kstuff not running after autostart");
    bootstrap_notify("notify.shadowmount.needs_kstuff");
    return;
  }
  LaunchContext context = {
      .loader_port = elfldr_remote_onion_available() ? ONION_ELFLDR_PORT
                                                     : ELFLDR_REMOTE_PORT,
  };
  launch_shadowmount(&context, firmware_version, true);
}
