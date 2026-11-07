#pragma once

/* Fallback for non-CMake tooling; PS5 builds shadow this with a generated file. */
#ifndef ONIONHEN_VERSION
#define ONIONHEN_VERSION "dirty-unknown"
#endif

#ifndef ONIONHEN_AUTHOR
/* Maintainer of this distribution, shown as "made by" in the welcome toast.
 * The authors of the code it is built from are credited on the About page
 * (Lineage) and in NOTICE.md. */
#define ONIONHEN_AUTHOR "AlexZero & Andriushka.exe"
#endif
