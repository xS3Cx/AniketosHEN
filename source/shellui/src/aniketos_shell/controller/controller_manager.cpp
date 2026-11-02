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

#include "hooked_funcs.hpp"
#include "aniketos_shell/input/pad_reader.hpp"

#include <onion/platform.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <time.h>

extern "C" {
int sceUserServiceGetForegroundUser(int *user_id);
int sceUserServiceGetUserName(int user_id, char *name, size_t size);
int scePadGetHandle(int user_id, int type, int index);
int scePadGetControllerInformation(int handle, void *info);
}

namespace AniketosHEN::Controller {

const char *ToString(ControllerClass value) {
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

namespace {

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
std::atomic<bool> s_stop{false};
std::atomic<bool> s_ready{false};
std::atomic<bool> s_worker_running{false};
pthread_t s_worker{};

std::mutex s_mutex;
ControllerInfo s_snapshot;
uint32_t s_generation = 0;

uint64_t monotonic_millis() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<uint64_t>(now.tv_sec) * 1000u +
         static_cast<uint64_t>(now.tv_nsec) / 1000000u;
}

int written_extent(const unsigned char *buffer, int length) {
  for (int i = length - 1; i >= 0; i--)
    if (buffer[i] != kGuard)
      return i + 1;
  return -1;
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
  memset(buffer, kGuard, sizeof(buffer));
  const int ret = scePadGetControllerInformation(handle, buffer);
  if (ret != 0) {
    LOG_ERROR("[Controller] layout probe failed ret=0x%08x",
              static_cast<unsigned>(ret));
    return false;
  }
  const int extent = written_extent(buffer, kBufferBytes);
  if (extent != kInfoSize) {
    LOG_ERROR("[Controller] unexpected ScePadControllerInformation size %d "
              "(expected %d) — refusing to decode",
              extent, kInfoSize);
    return false;
  }

  float density = 0.0f;
  uint16_t width = 0, height = 0;
  uint32_t device_class = 0;
  memcpy(&density, buffer + kOffTouchDensity, sizeof(density));
  memcpy(&width, buffer + kOffTouchWidth, sizeof(width));
  memcpy(&height, buffer + kOffTouchHeight, sizeof(height));
  memcpy(&device_class, buffer + kOffDeviceClass, sizeof(device_class));

  if (!plausible_touchpad(width, height, density)) {
    LOG_ERROR("[Controller] implausible touchpad %ux%u — layout rejected",
              width, height);
    return false;
  }
  if (buffer[kOffConnected] > 1 || buffer[kOffConnectedCount] > 16 ||
      device_class > 0x0f) {
    LOG_ERROR("[Controller] implausible connected=%u count=%u class=%u — "
              "layout rejected",
              buffer[kOffConnected], buffer[kOffConnectedCount], device_class);
    return false;
  }

  LOG_INFO("[Controller] layout validated: %d bytes, touchpad %ux%u, class %u",
           extent, width, height, device_class);
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
  memset(buffer, kGuard, sizeof(buffer));
  if (scePadGetControllerInformation(handle, buffer) != 0)
    return false;
  if (written_extent(buffer, kBufferBytes) != kInfoSize)
    return false;

  float density = 0.0f;
  uint16_t width = 0, height = 0;
  uint32_t device_class = 0;
  memcpy(&density, buffer + kOffTouchDensity, sizeof(density));
  memcpy(&width, buffer + kOffTouchWidth, sizeof(width));
  memcpy(&height, buffer + kOffTouchHeight, sizeof(height));
  memcpy(&device_class, buffer + kOffDeviceClass, sizeof(device_class));

  info.backend_ready = true;
  info.handle = handle;
  info.connected = buffer[kOffConnected] != 0;
  info.connected_count = buffer[kOffConnectedCount];

  info.device_class = device_class <= 9
                          ? static_cast<ControllerClass>(device_class)
                          : ControllerClass::Unknown;
  info.device_class_state = Availability::Verified;

  info.touchpad_width = width;
  info.touchpad_height = height;
  info.touchpad_density = density;
  info.touchpad_state =
      (width && height) ? Availability::Verified : Availability::Unavailable;

  info.deadzone_left = buffer[kOffDeadzoneLeft];
  info.deadzone_right = buffer[kOffDeadzoneRight];
  info.deadzone_state = Availability::Verified;

  /* Read successfully, but no controlled USB/Bluetooth transition has proven
   * what the value means yet, so it stays out of the production UI. */
  info.connection_type_raw = buffer[kOffConnectionType];
  info.connection_state = Availability::Experimental;

  fill_user(info);
  info.sampled_at_ms = monotonic_millis();
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
  s_snapshot.generation = ++s_generation;
}

void publish_disconnected() {
  ControllerInfo empty;
  empty.backend_ready = s_ready.load(std::memory_order_acquire);
  publish(empty);
}

void sleep_millis(long millis) {
  const timespec delay{millis / 1000, (millis % 1000) * 1000000L};
  nanosleep(&delay, nullptr);
}

void *worker(void *) {
  s_worker_running.store(true, std::memory_order_release);
  LOG_INFO("[Controller] subsystem worker started");

  int validated_handle = -1;
  ControllerInfo previous;
  bool published_any = false;

  while (!s_stop.load(std::memory_order_acquire)) {
    const int handle = acquire_handle();

    if (handle < 0) {
      if (previous.connected || !published_any) {
        LOG_INFO("[Controller] no controller available");
        publish_disconnected();
        previous = ControllerInfo{};
        published_any = true;
      }
      validated_handle = -1;
      s_ready.store(false, std::memory_order_release);
      sleep_millis(kPollMillis);
      continue;
    }

    /* Re-validate whenever the handle changes: a different pad may be a
     * different device class, and a firmware swap under us must not go
     * unnoticed. */
    if (handle != validated_handle) {
      if (!validate_layout(handle)) {
        s_ready.store(false, std::memory_order_release);
        publish_disconnected();
        previous = ControllerInfo{};
        published_any = true;
        sleep_millis(kPollMillis * 3);
        continue;
      }
      validated_handle = handle;
      s_ready.store(true, std::memory_order_release);
    }

    ControllerInfo next;
    if (!sample(handle, next)) {
      validated_handle = -1;
      sleep_millis(kPollMillis);
      continue;
    }

    if (!published_any || meaningfully_differs(previous, next)) {
      LOG_INFO("[Controller] state: connected=%d count=%u class=%s "
               "touchpad=%ux%u user=%d connType(raw,experimental)=%u",
               next.connected ? 1 : 0, next.connected_count,
               ToString(next.device_class), next.touchpad_width,
               next.touchpad_height, next.user_id, next.connection_type_raw);
      published_any = true;
    }
    publish(next);
    previous = next;

    sleep_millis(kPollMillis);
  }

  publish_disconnected();
  s_ready.store(false, std::memory_order_release);
  LOG_INFO("[Controller] subsystem worker stopped");
  s_worker_running.store(false, std::memory_order_release);
  return nullptr;
}

} // namespace

bool Start() {
  bool expected = false;
  if (!s_started.compare_exchange_strong(expected, true))
    return true;
  s_stop.store(false, std::memory_order_release);
  if (pthread_create(&s_worker, nullptr, worker, nullptr) != 0) {
    s_started.store(false, std::memory_order_release);
    LOG_ERROR("[Controller] worker start failed");
    return false;
  }
  return true;
}

void Stop() {
  if (!s_started.load(std::memory_order_acquire))
    return;
  s_stop.store(true, std::memory_order_release);
  /* Join rather than detach: the Toolbox and the VSH renderer must not be able
   * to observe a half-torn-down subsystem, and nothing may call back into this
   * module after Stop() returns. */
  pthread_join(s_worker, nullptr);
  s_started.store(false, std::memory_order_release);
  s_ready.store(false, std::memory_order_release);
  publish_disconnected();
  LOG_INFO("[Controller] subsystem stopped and state cleared");
}

ControllerInfo GetSnapshot() {
  std::lock_guard<std::mutex> lock(s_mutex);
  return s_snapshot;
}

bool IsReady() { return s_ready.load(std::memory_order_acquire); }

} // namespace AniketosHEN::Controller
