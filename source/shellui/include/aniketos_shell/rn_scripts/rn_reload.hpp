#pragma once

#include <atomic>

/* The same reload delay_ms after the last request, so a slider stepped
 * repeatedly reloads Home once it rests. */
void shellui_request_home_reload_after(int delay_ms);
/* Home's bundle is starting (script_injector): a reload asked for in the next
 * few seconds waits until Home is up. */
void shellui_note_home_start(void);
/* Turns a due delayed reload into `pending`. True when nothing can be applied
 * yet: none is pending, or Home is starting, and then `pending` stays set. */
bool shellui_home_reload_held(std::atomic<bool> &pending);
/* The presence dot (script_injector) is read by Home and Control Center when
 * they start: reload Control Center, and Home with it if asked, delay_ms
 * after the last request, so stepping through the choices reloads once. */
void shellui_request_presence_reload(int delay_ms, bool home_too);
/** UI thread only. */
void shellui_poll_presence_reload(void);
