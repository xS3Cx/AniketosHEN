#pragma once

#include "external_symbols.hpp"

#include <string>

/* Running JavaScript of our own inside a ShellUI React Native app (Home).
 *
 * ShellUI's React Native host is managed code; its executor has
 * RunScript(bundle), through which every app bundle is loaded on that app's
 * JS thread, and EvalScript(source, url). Hooking the first and calling the
 * second from inside the hook puts a script of ours into the app's runtime
 * just before its bundle runs. Both are found by name, so this does not
 * depend on a firmware's bundle layout. */
namespace HomeJs {
void Install(MonoImage *react_pui);

/* The HEN is about to reload this app (title id): its next start is not a
 * restart after a fall-over, so the restart guard leaves the script in. */
void NoteRequestedReload(const char *title_id);

/* `text` (UTF-8) as a JavaScript string literal in plain ASCII. */
std::string JsString(const std::string &text);
}
