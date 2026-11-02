/* Copyright (C) 2026 AniketosHEN
 *
 * Keyboard and Mouse paths: what a resource or control id says about the item
 * it belongs to, and back. The grammar is in kbm_toolbox.hpp.
 */
#include "kbm_toolbox_internal.hpp"

#include <algorithm>
#include <utility>

namespace kbm_toolbox {
namespace {

bool is_word(std::string_view s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
  });
}

/* "l3" → 3 for the index letters; -1 otherwise. */
int index_token(std::string_view s, char letter) {
  if (s.size() < 2 || s.size() > 4 || s[0] != letter)
    return -1;
  int n = 0;
  for (char c : s.substr(1)) {
    if (c < '0' || c > '9')
      return -1;
    n = n * 10 + (c - '0');
  }
  return n;
}

} // namespace

/* "<T>[-l0][-b1]...[-word]": the word, if any, comes last. */
bool parse_tokens(std::string_view text, Path *out) {
  Path p;
  size_t at = 0;
  for (int n = 0; at <= text.size(); ++n) {
    const size_t dash = text.find('-', at);
    const std::string_view tok = text.substr(
        at, dash == std::string_view::npos ? std::string_view::npos : dash - at);
    at = dash == std::string_view::npos ? text.size() + 1 : dash + 1;
    if (n == 0) {
      p.title = std::string(tok);
      if (p.title != "all" && p.title != "default" && p.title != "help" &&
          !kb::IsValidTitleId(p.title))
        return false;
      continue;
    }
    if (!p.word.empty())
      return false;
    int *slot = nullptr;
    int value = -1;
    const std::pair<char, int *> slots[] = {
        {'l', &p.layer}, {'b', &p.binding}, {'o', &p.output}, {'m', &p.macro}, {'s', &p.step}};
    for (const auto &[letter, target] : slots)
      if ((value = index_token(tok, letter)) >= 0) {
        slot = target;
        break;
      }
    if (slot) {
      if (*slot >= 0)
        return false;
      *slot = value;
    } else if (is_word(tok)) {
      p.word = std::string(tok);
    } else {
      return false;
    }
  }
  *out = std::move(p);
  return true;
}

std::string path_text(const Path &p) {
  std::string k = p.title;
  const std::pair<char, int> parts[] = {
      {'l', p.layer}, {'b', p.binding}, {'o', p.output}, {'m', p.macro}, {'s', p.step}};
  for (const auto &[letter, v] : parts)
    if (v >= 0)
      k += std::string("-") + letter + std::to_string(v);
  return k;
}

std::string element_id(const Path &p, const char *field) {
  return "id_kbm-" + path_text(p) + "-" + field;
}

Path with(Path p, char letter, int v) {
  (letter == 'l' ? p.layer : letter == 'b' ? p.binding : letter == 'o' ? p.output
                       : letter == 'm' ? p.macro : p.step) = v;
  p.word.clear();
  return p;
}

Path field(Path p, const char *f) {
  p.word = f;
  return p;
}

bool parse_id(std::string_view id, Path *out) {
  constexpr std::string_view kPrefix = "id_kbm-";
  return id.starts_with(kPrefix) && parse_tokens(id.substr(kPrefix.size()), out) &&
         !out->word.empty();
}

std::string resource(const Path &p, const char *page) {
  return "kbm-" + path_text(p) + (page ? std::string("-") + page : "") + ".xml";
}

} // namespace kbm_toolbox
