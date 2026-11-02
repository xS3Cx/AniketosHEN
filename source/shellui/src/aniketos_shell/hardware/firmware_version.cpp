#include "aniketos_shell/hardware/firmware_version.hpp"

#include <ps5/kernel.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace AniketosShell::Firmware {
namespace {

/* The version number is written in decimal digits, one per hex digit:
   0x13420000 is 13.42. */
bool decimal_digits(unsigned byte) {
  return (byte >> 4) <= 9 && (byte & 0xf) <= 9;
}

bool valid_version(uint32_t version) {
  const unsigned major = (version >> 24) & 0xffu;
  const unsigned minor = (version >> 16) & 0xffu;
  return major > 0 && decimal_digits(major) && decimal_digits(minor);
}

bool valid_text(const char *text, size_t capacity) {
  if (!text || capacity == 0 || text[0] == '\0')
    return false;
  bool terminated = false;
  for (size_t i = 0; i < capacity; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == '\0') {
      terminated = true;
      break;
    }
    if (c < 0x20 || c > 0x7e)
      return false;
  }
  return terminated;
}

void format_version(uint32_t version, char *out, size_t capacity) {
  const unsigned major = (version >> 24) & 0xffu;
  const unsigned minor = (version >> 16) & 0xffu;
  std::snprintf(out, capacity, "%x.%02x", major, minor);
}

} // namespace

bool Read(OrbisKernelSwVersion &out) {
  std::memset(&out, 0, sizeof(out));

  /* kernel_get_fw_version is the same runtime SDK helper used by Elf Arsenal.
   * It avoids depending on an export that may fail on some system software. */
  const uint32_t kernel_version = kernel_get_fw_version();

  OrbisKernelSwVersion exported{};
  const bool export_read =
      sceKernelGetProsperoSystemSwVersion &&
      sceKernelGetProsperoSystemSwVersion(&exported) == 0 &&
      valid_version(exported.version);

  const bool kernel_read = valid_version(kernel_version);
  if (!kernel_read && !export_read)
    return false;

  out.version = kernel_read ? kernel_version : exported.version;
  if (export_read && exported.version == out.version &&
      valid_text(exported.version_str, sizeof(exported.version_str))) {
    std::memcpy(out.version_str, exported.version_str, sizeof(out.version_str));
    out.version_str[sizeof(out.version_str) - 1] = '\0';
  } else {
    format_version(out.version, out.version_str, sizeof(out.version_str));
  }
  return true;
}

std::string Short(const OrbisKernelSwVersion &firmware) {
  char text[16]{};
  if (valid_version(firmware.version)) {
    format_version(firmware.version, text, sizeof(text));
    return text;
  }
  /* No usable number: cut the system's own text, "13.420.000", down to the
     major part and the first two digits after it. */
  const char *full = firmware.version_str;
  if (!valid_text(full, sizeof(firmware.version_str)))
    return {};
  const char *dot = std::strchr(full, '.');
  if (!dot || dot == full)
    return full;
  std::string out(full, static_cast<size_t>(dot - full) + 1);
  for (const char *c = dot + 1; *c >= '0' && *c <= '9' && out.size() <
                                    static_cast<size_t>(dot - full) + 3;
       ++c)
    out += *c;
  return out;
}

} // namespace AniketosShell::Firmware
