#pragma once

#include <string>

/* Opens the Toolbox through whichever BootHelper.Boot overload this firmware
   has, with the option the top-bar icon last came with. */
bool shellui_open_toolbox();

namespace BootUri {
/* Our rows in the Options menu and the top-bar icon. True when the URI was
   ours, so the boot goes no further. */
bool Handle(const std::string &uri, bool is_top_nav, int opt);
}
