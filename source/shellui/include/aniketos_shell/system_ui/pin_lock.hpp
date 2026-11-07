#pragma once

#include "ps5_settings_ui.hpp"
#include "toolbox_route.hpp"

#include <string>

/* Optional PIN lock for the Toolbox, the Shell Menu and the HEN menu.
 *
 * Off until a PIN is set: the salted hash lives in pin_lock under the user
 * data root, never in config.ini. Deleting that file, or a no_pin_lock file
 * on the first USB drive, turns it off. The right PIN unlocks until rest
 * mode, another foreground user, or PinLockCore::kIdleSeconds without
 * opening a locked menu. The PIN is asked with PUI's own prompt
 * (SystemDialogs). The system's own Settings pages are never locked. UI
 * thread only. */
namespace PinLock {

/* Runs `then` now when no PIN is needed, else asks for it and runs `then`
 * after the right one. */
void Require(void (*then)());

/* The Toolbox page `page` (or an SDK plugin page) must be replaced by the
 * locked page now. While unlocked, opening it counts as use. */
bool Blocks(toolbox::Page page, bool dynamic_page);

/* The page served instead of `resource` while locked; asks for the PIN by
 * itself and opens `resource` after the right one. */
std::string LockedPageXml(const std::string &resource);

/* The Toolbox rows: set or change the PIN, remove it. */
void AppendRows(ps5ui::Group &g);

/* A press on one of our rows (locked page or Toolbox rows): true when it
 * was ours. */
bool HandlePress(const std::string &id);

void Tick();

} // namespace PinLock
