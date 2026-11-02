#include "aniketos_shell/rn_scripts/js_string.hpp"

#include <cstdio>

/* The executor sizes the source by characters, not bytes, so anything outside
   ASCII (the glyph, most translations) has to travel as \uXXXX escapes or the
   script arrives cut short. */
std::string JsString(const std::string &text) {
  std::string out = "\"";
  const auto escape = [&out](unsigned unit) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "\\u%04x", unit & 0xffffu);
    out += buffer;
  };
  for (size_t i = 0; i < text.size();) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    unsigned code = lead;
    size_t length = 1;
    if (lead >= 0xf0 && i + 3 < text.size()) {
      code = lead & 0x07;
      length = 4;
    } else if (lead >= 0xe0 && i + 2 < text.size()) {
      code = lead & 0x0f;
      length = 3;
    } else if (lead >= 0xc0 && i + 1 < text.size()) {
      code = lead & 0x1f;
      length = 2;
    }
    for (size_t k = 1; k < length; ++k)
      code = (code << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3f);
    i += length;
    if (code >= 0x10000) {
      code -= 0x10000;
      escape(0xd800 + (code >> 10));
      escape(0xdc00 + (code & 0x3ff));
    } else if (code < 0x20 || code > 0x7e || code == '"' || code == '\\') {
      escape(code);
    } else {
      out += static_cast<char>(code);
    }
  }
  return out + "\"";
}
