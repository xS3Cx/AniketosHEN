/* Copyright (C) 2026 AniketosHEN — controller telemetry backend + manager.
 *
 * Resolution strategy, highest tier first:
 *   1. The exported libScePad getters. These are the stable, documented entry
 *      points and are what we actually use.
 *   2. A runtime shape check of what those getters return. We never trust a
 *      byte offset because a previous firmware had it there; on startup the
 *      backend fills the output buffer with a guard pattern, calls the getter,
 *      and only accepts the layout if the written extent and every decoded
 *      field land in their plausible ranges. A firmware that changes the
 *      struct fails that check and the subsystem reports Unavailable instead
 *      of publishing a wrong number.
 *
 * There is deliberately no firmware-keyed offset table and no memory scanning.
 * Everything below is read-only with respect to the pad and the system.
 */
#include "aniketos_shell/controller/controller_manager.hpp"

#include "aniketos_shell/input/pad_reader.hpp"
#include "aniketos_ui/monotonic_clock.hpp"
#include "hooked_funcs.hpp"

#include <onion/log.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <pthread.h>

extern "C" {
int sceUserServiceGetUserName(int user_id, char *name, size_t size);
int scePadGetHandle(int user_id, int type, int index);
int scePadGetControllerInformation(int handle, void *info);
}

namespace AniketosHEN::Controller {
namespace {

const char *class_name(ControllerClass value) {
  switch (value) {
  case ControllerClass::StandardPad: return "StandardPad";
  case ControllerClass::Guitar: return "Guitar";
  case ControllerClass::Drum: return "Drum";
  case ControllerClass::DjTurntable: return "DjTurntable";
  case ControllerClass::DanceMat: return "DanceMat";
  case ControllerClass::Navigation: return "Navigation";
  case ControllerClass::SteeringWheel: return "SteeringWheel";
  case ControllerClass::Stick: return "Stick";
  case ControllerClass::FlightStick: return "FlightStick";
  case ControllerClass::Gun: return "Gun";
  default: return "Unknown";
  }
}

/* Offsets inside ScePadControllerInformation. They are only ever used after
 * validate_layout() has confirmed, on this running firmware, that the getter
 * writes exactly this many bytes and that every field decodes sanely. */
constexpr int kInfoSize = 28;
constexpr int kOffTouchDensity = 0x00;
constexpr int kOffTouchWidth = 0x04;
constexpr int kOffTouchHeight = 0x06;
constexpr int kOffDeadzoneLeft = 0x08;
constexpr int kOffDeadzoneRight = 0x09;
constexpr int kOffConnectionType = 0x0A;
constexpr int kOffConnectedCount = 0x0B;
constexpr int kOffConnected = 0x0C;
constexpr int kOffDeviceClass = 0x10;

constexpr unsigned char kGuard = 0xCC;
constexpr int kBufferBytes = 128;
constexpr int kPollMillis = 1000;

std::atomic<bool> s_started{false};

std::mutex s_mutex;
ControllerInfo s_snapshot;

struct InformationFields {
  float density = 0.0f;
  uint16_t width = 0;
  uint16_t height = 0;
  uint32_t device_class = 0;
};

/* Calls the getter over the guard pattern. */
int read_information(int handle, unsigned char *buffer) {
  memset(buffer, kGuard, kBufferBytes);
  return scePadGetControllerInformation(handle, buffer);
}

int written_extent(const unsigned char *buffer) {
  for (int i = kBufferBytes - 1; i >= 0; i--)
    if (buffer[i] != kGuard)
      return i + 1;
  return -1;
}

InformationFields decode(const unsigned char *buffer) {
  InformationFields fields;
  memcpy(&fields.density, buffer + kOffTouchDensity, sizeof(fields.density));
  memcpy(&fields.width, buffer + kOffTouchWidth, sizeof(fields.width));
  memcpy(&fields.height, buffer + kOffTouchHeight, sizeof(fields.height));
  memcpy(&fields.device_class, buffer + kOffDeviceClass,
         sizeof(fields.device_class));
  return fields;
}

bool plausible_touchpad(uint16_t width, uint16_t height, float density) {
  if (width == 0 && height == 0)
    return true; /* a pad without a touchpad is legitimate */
  if (width < 128 || height < 128 || width > 8192 || height > 8192)
    return false;
  return std::isfinite(density) && density > 0.0f && density < 4096.0f;
}

/* One-shot shape check. Returns true only if this firmware's getter behaves
 * exactly as the layout we validated against real hardware. */
bool validate_layout(int handle) {
  unsigned char buffer[kBufferBytes];
  const int ret = read_information(handle, buffer);
  if (ret != 0) {
    LOG_ERROR("[Controller] layout probe failed ret=0x%08x",
              static_cast<unsigned>(ret));
    return false;
  }
  const int extent = written_extent(buffer);
  if (extent != kInfoSize) {
    LOG_ERROR("[Controller] unexpected ScePadControllerInformation size %d "
              "(expected %d) — refusing to decode",
              extent, kInfoSize);
    return false;
  }

  const InformationFields fields = decode(buffer);
  if (!plausible_touchpad(fields.width, fields.height, fields.density)) {
    LOG_ERROR("[Controller] implausible touchpad %ux%u — layout rejected",
              fields.width, fields.height);
    return false;
  }
  if (buffer[kOffConnected] > 1 || buffer[kOffConnectedCount] > 16 ||
      fields.device_class > 0x0f) {
    LOG_ERROR("[Controller] implausible connected=%u count=%u class=%u — "
              "layout rejected",
              buffer[kOffConnected], buffer[kOffConnectedCount],
              fields.device_class);
    return false;
  }

  LOG_INFO("[Controller] layout validated: %d bytes, touchpad %ux%u, class %u",
           extent, fields.width, fields.height, fields.device_class);
  return true;
}

int acquire_handle() {
  /* The VSH reader already owns the foreground pad; opening the same
   * (user,type,index) twice returns BUSY on retail ShellUI. */
  const int borrowed = AniketosShell::Input::ActivePadHandle();
  if (borrowed >= 0)
    return borrowed;
  int user_id = -1;
  if (sceUserServiceGetForegroundUser(&user_id) != 0 || user_id < 0)
    return -1;
  const int handle = scePadGetHandle(user_id, 0, 0);
  return handle >= 0 ? handle : -1;
}

void fill_user(ControllerInfo &info) {
  int user_id = -1;
  if (sceUserServiceGetForegroundUser(&user_id) != 0 || user_id < 0)
    return;
  info.user_id = user_id;
  info.user_valid = true;
  char name[64]{};
  if (sceUserServiceGetUserName(user_id, name, sizeof(name)) == 0 && name[0])
    info.user_name = name;
}

/* Decode one sample. Only called after validate_layout() succeeded. */
bool sample(int handle, ControllerInfo &info) {
  unsigned char buffer[kBufferBytes];
  if (read_information(handle, buffer) != 0 ||
      written_extent(buffer) != kInfoSize)
    return false;

  const InformationFields fields = decode(buffer);
  info.backend_ready = true;
  info.handle = handle;
  info.connected = buffer[kOffConnected] != 0;
  info.connected_count = buffer[kOffConnectedCount];

  info.device_class = fields.device_class <= 9
                          ? static_cast<ControllerClass>(fields.device_class)
                          : ControllerClass::Unknown;

  info.touchpad_width = fields.width;
  info.touchpad_height = fields.height;
  info.touchpad_state = (fields.width && fields.height)
                            ? Availability::Verified
                            : Availability::Unavailable;

  info.deadzone_left = buffer[kOffDeadzoneLeft];
  info.deadzone_right = buffer[kOffDeadzoneRight];
  info.deadzone_state = Availability::Verified;

  info.connection_type_raw = buffer[kOffConnectionType];

  fill_user(info);
  return true;
}

/* Only compare what the user can actually perceive; timestamps churn. */
bool meaningfully_differs(const ControllerInfo &a, const ControllerInfo &b) {
  return a.connected != b.connected || a.connected_count != b.connected_count ||
         a.device_class != b.device_class || a.handle != b.handle ||
         a.user_id != b.user_id || a.backend_ready != b.backend_ready ||
         a.connection_type_raw != b.connection_type_raw ||
         a.touchpad_width != b.touchpad_width ||
         a.touchpad_height != b.touchpad_height;
}

void publish(const ControllerInfo &info) {
  std::lock_guard<std::mutex> lock(s_mutex);
  s_snapshot = info;
}

void publish_disconnected(bool backend_ready) {
  ControllerInfo empty;
  empty.backend_ready = backend_ready;
  publish(empty);
}

void *worker(void *) {
  LOG_INFO("[Controller] subsystem worker started");

  int validated_handle = -1;
  bool backend_ready = false;
  ControllerInfo previous;
  bool published_any = false;

  for (;;) {
    const int handle = acquire_handle();

    if (handle < 0) {
      if (previous.connected || !published_any) {
        LOG_INFO("[Controller] no controller available");
        publish_disconnected(backend_ready);
        previous = ControllerInfo{};
        published_any = true;
      }
      validated_handle = -1;
      backend_ready = false;
      AniketosUI::SleepMs(kPollMillis);
      continue;
    }

    /* Re-validate whenever the handle changes: a different pad may be a
     * different device class, and a firmware swap under us must not go
     * unnoticed. */
    if (handle != validated_handle) {
      if (!validate_layout(handle)) {
        backend_ready = false;
        publish_disconnected(backend_ready);
        previous = ControllerInfo{};
        published_any = true;
        AniketosUI::SleepMs(kPollMillis * 3);
        continue;
      }
      validated_handle = handle;
      backend_ready = true;
    }

    ControllerInfo next;
    if (!sample(handle, next)) {
      validated_handle = -1;
      AniketosUI::SleepMs(kPollMillis);
      continue;
    }

    if (!published_any || meaningfully_differs(previous, next)) {
      LOG_INFO("[Controller] state: connected=%d count=%u class=%s "
               "touchpad=%ux%u user=%d connType(raw,experimental)=%u",
               next.connected ? 1 : 0, next.connected_count,
               class_name(next.device_class), next.touchpad_width,
               next.touchpad_height, next.user_id, next.connection_type_raw);
      published_any = true;
    }
    publish(next);
    previous = next;

    AniketosUI::SleepMs(kPollMillis);
  }
}

} // namespace

bool Start() {
  bool expected = false;
  if (!s_started.compare_exchange_strong(expected, true))
    return true;
  pthread_t thread{};
  if (pthread_create(&thread, nullptr, worker, nullptr) != 0) {
    s_started.store(false, std::memory_order_release);
    LOG_ERROR("[Controller] worker start failed");
    return false;
  }
  pthread_detach(thread);
  return true;
}

ControllerInfo GetSnapshot() {
  std::lock_guard<std::mutex> lock(s_mutex);
  return s_snapshot;
}

} // namespace AniketosHEN::Controller
