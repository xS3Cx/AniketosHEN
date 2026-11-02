/* The commands AniketosHEN adds to OnionHEN's IPC client. */

#include <onion/ipc_client.hpp>

#include "onion_cjson.hpp"

bool IPC_Client::IsAvailable() {
  std::lock_guard<std::mutex> lock(mu_);
  return IPCOpenIfNotConnected();
}

bool IPC_Client::RestartShellUI() {
  if (!require_crit("RestartShellUI"))
    return false;
  std::string ipc_msg;
  return IPCSendCommand(BREW_RESTART_SHELLUI, ipc_msg);
}

bool IPC_Client::WebTileIcon(const std::string &id, const std::string &url,
                             const std::string &title) {
  if (!require_util("WebTileIcon")) {
    return false;
  }
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "id", id.c_str());
  cJSON_AddStringToObject(j, "url", url.c_str());
  cJSON_AddStringToObject(j, "title", title.c_str());
  std::string out;
  if (!IPCSendCommand(BREW_UTIL_WEB_TILE_ICON, out, onion_cjson::print_owned(j))) {
    LOG_ERROR("Failed to ask for a web tile icon");
    return false;
  }
  return true;
}
