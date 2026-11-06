#pragma once

#include <string>
#include <cstdint>

#include "external_symbols.hpp"

/* ShellUI's own dialogs, opened with our text and buttons.
 *
 * Everything is found by name through Mono reflection at the time of the
 * call (classes, methods and fields of Sce.PlayStation.PUI), so nothing here
 * depends on firmware-specific addresses. A firmware that renames or drops
 * one of them makes the call fail with a message instead of misbehaving. Call
 * from the UI thread only. */
namespace SystemDialogs {

/* A plain PUI alert: up to four buttons and an optional check box. */
enum class AlertStyle { FullScreen, HalfScreen, Popup, TransparentPopup };
struct AlertOptions {
  AlertStyle style = AlertStyle::Popup;
  std::string title;
  std::string message;
  std::string positive;
  std::string negative;
  std::string neutral;
  std::string second_neutral;
  std::string check_box;
};
/* `scene` is the scene to open on; null means the one Home lives in. */
bool ShowAlert(const AlertOptions &options, std::string *failure = nullptr,
               MonoObject *scene = nullptr);

/* Which button closed the alert opened last: 0 positive, 1 negative,
 * 2 neutral, 3 second neutral, 4 dismissed. False while it is still open (or
 * none was opened). Reports each alert once. */
bool PollAlert(int *result);

/* A line of text from the user: PUI's prompt dialog (a message, a text field
 * that opens the system keyboard, OK and Cancel). */
struct PromptOptions {
  std::string title;
  std::string message;
  std::string text; /* what the field starts with */
  std::string positive;
  std::string negative;
};
bool ShowPrompt(const PromptOptions &options, std::string *failure = nullptr,
                MonoObject *scene = nullptr, uint64_t *token = nullptr);
/* The prompt opened last has closed: true once, with whether it was
 * confirmed and what its field held. False while it is open. */
bool PollPrompt(uint64_t token, bool *confirmed, std::string *text);

/* The scene that has the focus now: where a menu opened by the user should
 * appear, whatever is on screen. Null when it cannot be told. */
MonoObject *FocusedScene();

} // namespace SystemDialogs
