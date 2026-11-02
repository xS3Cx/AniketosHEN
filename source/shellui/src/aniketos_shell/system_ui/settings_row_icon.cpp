#include "aniketos_shell/core/embedded_assets.hpp"
#include "aniketos_ui/mono_reflect.hpp"
#include "hooked_funcs.hpp"

#include <onion/log.h>
#include <onion/notify.h>

/*
 * The Settings list is drawn by React on firmware where its rows no longer
 * ask SettingsPlugin for their pictures, so the CxmlUri redirect never sees
 * the request. There the row is given its picture as a loaded asset; when
 * that row is ours (by its name, or by the stock picture it was handed), it
 * is told to show the distribution's icon instead. Nothing on disk changes:
 * the icon is this distribution's own file in its data folder.
 */
void (*MenuListItem_SetIconImage)(MonoObject *item, MonoObject *asset) = nullptr;

void MenuListItem_SetIconImage_Hook(MonoObject *item, MonoObject *asset) {
  if (MenuListItem_SetIconImage)
    MenuListItem_SetIconImage(item, asset);
  if (!shellui_hooks_are_ready() || !item || !asset || !mono_object_to_string)
    return;
  /* a coloured logo has a name of its own: the system keeps pictures by path */
  const std::string own_icon = g_settings.logo_color_enabled
                                   ? AniketosIconPath()
                                   : ONION_NOTIFY_ICON_PATH;
  MonoObject *exception = nullptr;
  MonoString *shown = mono_object_to_string(asset, &exception);
  if (exception || !shown)
    return;
  const std::string picture = Mono_to_String(shown);
  if (picture.find(own_icon) != std::string::npos)
    return; /* ours, arriving: done */
  bool mine = picture.find("/icon_setting.png") != std::string::npos;
  if (!mine) {
    MonoMethod *get_name = AniketosUI::Reflect::MethodOf(item, "get_Name", 0);
    exception = nullptr;
    MonoObject *name = get_name ? mono_runtime_invoke(get_name, item, nullptr,
                                                      &exception)
                                : nullptr;
    mine = name && !exception &&
           Mono_to_String(reinterpret_cast<MonoString *>(name)) ==
               "id_debug_settings";
  }
  if (!mine)
    return;
  MonoMethod *set_uri = AniketosUI::Reflect::MethodOf(item, "set_IconUri", 1);
  if (!set_uri)
    return;
  void *args[1] = {mono_string_new(mono_domain_get(), own_icon.c_str())};
  exception = nullptr;
  mono_runtime_invoke(set_uri, item, args, &exception);
  static bool logged = false;
  if (!logged) {
    logged = true;
    LOG_INFO("settings icon: row given %s (%s)", own_icon.c_str(),
             exception ? "refused" : "ok");
  }
}
