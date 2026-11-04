#include "aniketos_shell/core/boot_uri.hpp"

#include "aniketos_shell/games/trophy_tools.hpp"
#include "aniketos_shell/home/hen_menu.hpp"
#include "aniketos_shell/home/hidden_apps.hpp"
#include "aniketos_shell/home/home_tabs_store.hpp"
#include "debug_settings_route_runtime.hpp"
#include "hooked_funcs.hpp"

/* How the top-bar icon last asked to be opened, so its menu can open the
   Toolbox the same way later. */
static int s_toolbox_boot_opt = 0;

bool shellui_open_toolbox() {
  /* prx.cpp hooks one overload only: the 3-argument one, else the other */
  if (boot_orig_2)
    return boot_orig_2(
        mono_string_new(Root_Domain,
                        shellui_debug_settings_toolbox_uri_simple()),
        s_toolbox_boot_opt);
  if (boot_orig)
    return boot_orig(
        mono_string_new(Root_Domain, shellui_debug_settings_toolbox_uri()),
        s_toolbox_boot_opt, nullptr);
  return false;
}

namespace BootUri {

bool Handle(const std::string &uri, bool is_top_nav, int opt) {
  if (HomeTabsStore::HandleUri(uri, g_ui.current_menu_tid))
    return true;
  if (uri == HiddenApps::kMenuUri) {
    HiddenApps::Hide(g_ui.current_menu_tid);
    return true;
  }
  if (uri == TrophyTools::kUnlockAllUri) {
    TrophyTools::RequestUnlockAll(g_ui.current_menu_tid);
    return true;
  }
  if (HenMenu::Available() && is_top_nav) {
    s_toolbox_boot_opt = opt;
    HenMenu::Request();
    return true;
  }
  return false;
}

} // namespace BootUri
