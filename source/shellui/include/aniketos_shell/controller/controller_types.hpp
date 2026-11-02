/* Copyright (C) 2026 AniketosHEN — normalized controller telemetry types.
 *
 * The PS5 pad interface reachable from ShellUI exposes far less than the
 * console UI shows, so "we do not know" has to be representable rather than
 * papered over with a plausible number.
 */
#pragma once

#include <cstdint>
#include <string>

namespace AniketosHEN::Controller {

/* Whether a field was read from a getter that returned success and whose value
 * was confirmed against the console, or no interface reachable from here
 * provides it. */
enum class Availability : uint8_t {
  Unavailable = 0,
  Verified,
};

/* Mirrors the deviceClass field of ScePadControllerInformation. The numeric
 * values are the system's own classification, not our invention. */
enum class ControllerClass : uint8_t {
  StandardPad = 0,
  Guitar = 1,
  Drum = 2,
  DjTurntable = 3,
  DanceMat = 4,
  Navigation = 5,
  SteeringWheel = 6,
  Stick = 7,
  FlightStick = 8,
  Gun = 9,
  Unknown = 0xff,
};

struct ControllerInfo {
  /* True once the backend resolved the pad interface and its returned layout
   * passed sanity checks. When false every field below is meaningless. */
  bool backend_ready = false;

  bool connected = false;
  /* Number of pads the system reports as connected. */
  uint8_t connected_count = 0;
  int handle = -1;

  int user_id = -1;
  std::string user_name;
  bool user_valid = false;

  ControllerClass device_class = ControllerClass::Unknown;

  /* Touchpad geometry, in touch units. Also the only reliable evidence that a
   * touchpad exists at all. */
  uint16_t touchpad_width = 0;
  uint16_t touchpad_height = 0;
  Availability touchpad_state = Availability::Unavailable;

  uint8_t deadzone_left = 0;
  uint8_t deadzone_right = 0;
  Availability deadzone_state = Availability::Unavailable;

  /* Raw connectionType byte. Its USB/Bluetooth mapping is not proven on this
   * console yet, so it stays in the log and out of the production UI. */
  uint8_t connection_type_raw = 0;
};

} // namespace AniketosHEN::Controller
