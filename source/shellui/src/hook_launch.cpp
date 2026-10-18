/* Copyright (C) 2025 OnionHEN / LightningMods — P0 split. */


#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include "external_symbols.hpp"
#include "toolbox_i18n.hpp"
#include "aniketos_shell/trophy_tools.hpp"
#include "aniketos_shell/home_bookmark.hpp"
#include <onion/platform.h>
#include <onion/system_tmp.h>
#include <string>
#include <fstream>
#include <sys/stat.h>

#include "shellui_state.hpp"
#include <cctype>
#include <cstring>

void save_appid(int value, const char* filename) {
    mkdir(ONION_SYSTEM_TMP_ROOT, 0777);
    std::ofstream file(filename);
    file << value;
}
bool app_launched = false;
int LaunchApp(MonoString* titleId, uint64_t* args, int argsSize, LaunchAppParam *param){
  if (!shellui_hooks_are_ready())
    return LaunchApp_orig ? LaunchApp_orig(titleId, args, argsSize, param) : -1;

#if 1
   if(!if_exists(ONION_SYSTEM_TMP_PATCH_PLUGIN)) {
      #if SHELL_DEBUG == 1
      LOG_DEBUG("patch payload not running .. returning with orig");
      #endif
	  unsigned int ret = LaunchApp_orig(titleId, args, argsSize, param);
      if (ret < 0) {
         #if SHELL_DEBUG == 1
         notify("notify.app.launch_failed", ret);
         #endif
         return ret;
      }

      app_launched = true;
      return ret;

   }
#endif
#if SHELL_DEBUG == 1
  LOG_DEBUG("LaunchApp called with titleId: %s, argsSize: %d, param->size: %d", mono_string_to_utf8(titleId), argsSize, param->size);
#endif
  notify("notify.app.launching", mono_string_to_utf8(titleId));

  unsigned int ret = LaunchApp_orig(titleId, args, argsSize, param);
  if (ret < 0) {
    #if SHELL_DEBUG == 1
    notify("notify.app.launch_failed", ret);
    #endif
    return ret;
  }

  app_launched = true;

 #if SHELL_DEBUG == 1
  notify("notify.app.launch_returned", ret);
  #endif

  save_appid(ret, ONION_SYSTEM_TMP_APP_LAUNCHED);
  return ret;

}

int sceRegMgrGetInt_hook(long regid, int* out_val){
  if (!shellui_hooks_are_ready()) {
    int original_ret = 0;
    if (!__sys_regmgr_call ||
        __sys_regmgr_call(2, regid, &original_ret, out_val,
                          SCE_REGMGR_INT_SIZE))
      return SCE_REGMGR_ERROR_PRM_REGID;
    return original_ret;
  }

  bool dis_tids = g_settings.display_tids;

  if(dis_tids && regid == SCE_REGMGR_ENT_KEY_DEVENV_TOOL_SHELLUI_disp_titleid){
    if (out_val) {
       *out_val = 1;
    }
#if SHELL_DEBUG==1
    LOG_DEBUG("RegMGR lookup called for SHELLUI_disp_titleid, spoofing out_var to 1");
#endif
    return 0;
  }

  int ret = 0;
  if(__sys_regmgr_call(2, regid, &ret, out_val, SCE_REGMGR_INT_SIZE)){
#if SHELL_DEBUG==1
    LOG_ERROR("sceRegMgrGetInt_hook: Failed to get regid 0x%lx, ret %d", regid, ret);
#endif
    ret = SCE_REGMGR_ERROR_PRM_REGID;
  }

  return ret;
}
/* The title an Options menu entry is about, read from its URL: a
   "titleId=" parameter (any case, "npTitleId=" too) or the first of the
   "titleIds" list some dialogs carry, percent-encoded or not. */
static std::string menu_entry_title(const std::string &url) {
    std::string text;
    for (size_t i = 0; i < url.size(); ++i) {
        if (url[i] == '%' && i + 2 < url.size() &&
            std::isxdigit(static_cast<unsigned char>(url[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(url[i + 2]))) {
            text += static_cast<char>(std::stoi(url.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            text += url[i];
        }
    }
    std::string lower = text;
    for (char &c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char *key : {"titleid=", "\"titleids\":[\""}) {
        const size_t at = lower.find(key);
        if (at == std::string::npos)
            continue;
        const std::string id = text.substr(at + std::strlen(key), 9);
        bool clean = id.size() == 9;
        for (const char c : id)
            clean = clean && std::isalnum(static_cast<unsigned char>(c));
        if (clean)
            return id;
    }
    return std::string();
}

static std::string extractTIDFromURI(const std::string& url) {
    /* "titleId=" in most entries, "titleid=" in Check for Update's */
    const std::string prefix = "titleid=";
    std::string lower = url;
    for (char &c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    size_t pos = lower.find(prefix);
    
    if (pos != std::string::npos) {
        pos += prefix.length();
        size_t end = url.find('&', pos);
        if (end == std::string::npos) {
            return url.substr(pos);
        } else {
            return url.substr(pos, end - pos);
        }
    }
    return std::string(); // Not found
}

void createJson_hook(MonoObject* inst, MonoObject* array, MonoString* id, MonoString* label, MonoString* actionUrl, MonoString* actionId, MonoString* messageId, MonoObject* subMenu, bool enable) {

    if (!shellui_hooks_are_ready()) {
        if (createJson)
            createJson(inst, array, id, label, actionUrl, actionId, messageId,
                       subMenu, enable);
        return;
    }

    std::string id_str = Mono_to_String(id);

#if SHELL_DEBUG==1
    LOG_DEBUG("createJson_hook: %p id: %s, label: %s, actionUrl: %s, actionId: %s, messageId: %s", 
               static_cast<void *>(inst), id_str.c_str(), 
               Mono_to_String(label).c_str(), 
               Mono_to_String(actionUrl).c_str(), 
               Mono_to_String(actionId).c_str(), 
               Mono_to_String(messageId).c_str());
#endif

    /* The bookmark tab on Home: just above "Delete", which every game and
       app of the user's has and the system's own tiles do not. One entry,
       with AniketosHEN's star in front: it opens AniketosHEN's own menu
       (HomeBookmark) with Copy to, Move to and Remove from, each opening the
       tabs under the names they have now. Nothing else is added to Sony's
       menu. */
    if (id_str == "MENU_ID_APPLICATION_DELETE") {
        const std::string title = menu_entry_title(Mono_to_String(actionUrl));
        if (!title.empty())
            g_ui.current_menu_tid = title;
        if (HomeBookmark::Allowed(title)) {
            const std::string text = std::string(HomeBookmark::kGlyph) + " " +
                                     toolbox_i18n::tr("bookmark.menu_tabs");
            createJson(inst, array,
                       mono_string_new(Root_Domain, "MENU_ID_ANIKETOS_BOOKMARK"),
                       mono_string_new(Root_Domain, text.c_str()),
                       mono_string_new(Root_Domain, HomeBookmark::kMenuUri),
                       actionId, nullptr, subMenu, enable);
        }
    }

    if(!g_settings.onionhen_game_opts) {
        createJson(inst, array, id, label, actionUrl, actionId, messageId, subMenu, enable);
        return;
    }

    // Only extract and update titleId if one is found in the current URL
    std::string extracted_tid = extractTIDFromURI(Mono_to_String(actionUrl));
    if (!extracted_tid.empty() && extracted_tid != g_ui.current_menu_tid) {
        g_ui.current_menu_tid = extracted_tid;
#if SHELL_DEBUG==1
        //notify("Current menu titleId: %s", g_ui.current_menu_tid.c_str());
        LOG_DEBUG("Updated menu titleId: %s", g_ui.current_menu_tid.c_str());
#endif
    }
    if(id_str == "MENU_ID_CHECK_PATCH"){  
      //createJson_hook: 8815fec90 id: MENU_ID_CHECK_PATCH, label: , actionUrl: pspatchcheck:check-for-update?titleid=CUSA01127, actionId: , messageId: msgid_check_update
        createJson(inst, array,
                   mono_string_new(Root_Domain, "MENU_ID_CHEATS"),
                   mono_string_new(Root_Domain,
                                   toolbox_i18n::tr("cheats.game_menu")),
                   mono_string_new(Root_Domain, "OnionHEN?Cheats_not_open"),
                   actionId, nullptr, subMenu, enable);
        createJson(inst, array,
                   mono_string_new(Root_Domain, "MENU_ID_TROPHY_UNLOCK_ALL"),
                   mono_string_new(Root_Domain,
                                   toolbox_i18n::tr("trophy.game_menu")),
                   mono_string_new(Root_Domain, TrophyTools::kUnlockAllUri),
                   actionId, nullptr, subMenu, enable);
        return;
    }

    createJson(inst, array, id, label, actionUrl, actionId, messageId, subMenu, enable);
}
