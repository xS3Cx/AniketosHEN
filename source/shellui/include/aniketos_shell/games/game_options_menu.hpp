#pragma once

#include "external_symbols.hpp"

#include <string>

/* Our rows in a title's Options menu. createJson_hook calls one of these for
 * every entry it is given, before forwarding that entry, so the rows land
 * above it. */
namespace GameOptionsMenu {
struct Entry {
  MonoObject *inst;
  MonoObject *array;
  const std::string &id;
  MonoString *action_url;
  MonoString *action_id;
  MonoObject *sub_menu;
  bool enable;
};

/* The HEN's game options are on: the block of rows, once per menu. */
void OnEntry(const Entry &entry);
/* They are off: the bookmark row alone. */
void OnEntryOptionsOff(const Entry &entry);
}
