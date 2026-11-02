#pragma once

/* The console's "Connect to the Internet" switch, as in Settings > Network. */
namespace InternetSwitch {
/* False when the flag cannot be read; *enabled is then left as it was. */
bool Read(bool *enabled);
/* Sets the flag and asks for the connection to change now. True when the flag
 * was set. */
bool Set(bool enabled);
}
