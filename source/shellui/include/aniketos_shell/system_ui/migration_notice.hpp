#pragma once

/* A one-time pop-up telling the user that an OnionHEN installation was found
 * and taken over: what was copied, that the old folder is untouched, and
 * that it can be deleted by hand. Shown once Home is up, until the user
 * acknowledges it. */
namespace MigrationNotice {
/* UI thread, once a frame. */
void Tick();
}
