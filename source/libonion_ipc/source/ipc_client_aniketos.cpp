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

bool IPC_Client::RefreshPayloadCatalog(const std::string &source_id,
                                       std::string &out) {
  if (!require_util("RefreshPayloadCatalog"))
    return false;
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "source_id", source_id.c_str());
  if (!IPCSendCommand(BREW_UTIL_PAYLOAD_CATALOG_REFRESH, out,
                      onion_cjson::print_owned(j))) {
    LOG_ERROR("Failed to refresh payload catalog");
    return false;
  }
  return true;
}

bool IPC_Client::InstallPayload(const std::string &source_id,
                                const std::string &item_id, bool replace,
                                std::string &out) {
  if (!require_util("InstallPayload"))
    return false;
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "source_id", source_id.c_str());
  cJSON_AddStringToObject(j, "item_id", item_id.c_str());
  cJSON_AddBoolToObject(j, "replace", replace);
  if (!IPCSendCommand(BREW_UTIL_PAYLOAD_INSTALL, out, onion_cjson::print_owned(j))) {
    LOG_ERROR("Failed to install payload %s", item_id.c_str());
    return false;
  }
  return true;
}

bool IPC_Client::InstallPayloadFromUrl(const std::string &url, bool replace,
                                       std::string &out) {
  if (!require_util("InstallPayloadFromUrl"))
    return false;
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "url", url.c_str());
  cJSON_AddBoolToObject(j, "replace", replace);
  if (!IPCSendCommand(BREW_UTIL_PAYLOAD_INSTALL, out, onion_cjson::print_owned(j))) {
    LOG_ERROR("Failed to install payload from URL");
    return false;
  }
  return true;
}

bool IPC_Client::PayloadInstallStatus(std::string &out) {
  if (!require_util("PayloadInstallStatus"))
    return false;
  if (!IPCSendCommand(BREW_UTIL_PAYLOAD_STATUS, out,
                      onion_cjson::print_owned(cJSON_CreateObject()))) {
    LOG_ERROR("Failed to query payload install status");
    return false;
  }
  return true;
}

bool IPC_Client::CancelPayloadInstall(uint32_t task_id, std::string &out) {
  if (!require_util("CancelPayloadInstall"))
    return false;
  cJSON *j = cJSON_CreateObject();
  cJSON_AddNumberToObject(j, "task_id", task_id);
  if (!IPCSendCommand(BREW_UTIL_PAYLOAD_CANCEL, out,
                      onion_cjson::print_owned(j))) {
    LOG_ERROR("Failed to cancel payload install");
    return false;
  }
  return true;
}

bool IPC_Client::RemovePayload(const std::string &identity, std::string &out) {
  if (!require_util("RemovePayload"))
    return false;
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "identity", identity.c_str());
  if (!IPCSendCommand(BREW_UTIL_PAYLOAD_REMOVE, out,
                      onion_cjson::print_owned(j))) {
    LOG_ERROR("Failed to remove payload %s", identity.c_str());
    return false;
  }
  return true;
}

bool IPC_Client::UpdateCheck(const std::string &channel, std::string &out) {
  if (!require_util("UpdateCheck"))
    return false;
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "channel", channel.c_str());
  if (!IPCSendCommand(BREW_UTIL_UPDATE_CHECK, out,
                      onion_cjson::print_owned(j))) {
    LOG_ERROR("Failed to check for updates");
    return false;
  }
  return true;
}

bool IPC_Client::UpdateDownload(const std::string &channel, std::string &out) {
  if (!require_util("UpdateDownload"))
    return false;
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "channel", channel.c_str());
  if (!IPCSendCommand(BREW_UTIL_UPDATE_DOWNLOAD, out,
                      onion_cjson::print_owned(j))) {
    LOG_ERROR("Failed to download update");
    return false;
  }
  return true;
}

bool IPC_Client::UpdateStatus(std::string &out) {
  if (!require_util("UpdateStatus"))
    return false;
  if (!IPCSendCommand(BREW_UTIL_UPDATE_STATUS, out,
                      onion_cjson::print_owned(cJSON_CreateObject()))) {
    LOG_ERROR("Failed to query update status");
    return false;
  }
  return true;
}

bool IPC_Client::UpdateCancel(uint32_t task_id, std::string &out) {
  if (!require_util("UpdateCancel"))
    return false;
  cJSON *j = cJSON_CreateObject();
  cJSON_AddNumberToObject(j, "task_id", task_id);
  if (!IPCSendCommand(BREW_UTIL_UPDATE_CANCEL, out, onion_cjson::print_owned(j))) {
    LOG_ERROR("Failed to cancel update task");
    return false;
  }
  return true;
}
