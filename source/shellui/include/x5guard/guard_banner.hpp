#pragma once

#include <string>

namespace X5Guard {

/* The top-centre banner (notification overlay template PersistentToast, as
   Remote Play's), `text` in UTF-8 with console glyphs allowed. A new banner
   replaces the one on screen. False when the system did not take it. */
bool ShowBanner(const std::string &text);

/* Looks the send function up ahead of the first banner (loading
   libSceNotification costs a moment), so that one shows at once. */
void PrepareBanner();

/* "<red no-entry sign> x5Guard™", the start of every guard banner. */
std::string BannerPrefix();

/* The warning sound that goes with a banner: the system's own UI sound for a
   pressed item that is not allowed (PUI Application.PlaySystemSound,
   DisabledWidgetPressed).
   Queue from any thread; it plays on the next PlayPendingAlertSound, which
   runs on the UI thread. */
void QueueAlertSound();
void PlayPendingAlertSound();

} // namespace X5Guard
