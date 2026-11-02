#include "aniketos_shell/games/cheats_list.hpp"

#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include "onion_cjson.hpp"

#include <onion/log.h>
#include <onion/proc_query.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <pthread.h>
#include <unistd.h>
#include <unordered_set>

namespace AniketosShell::CheatsList {
namespace {

std::string join_authors(const cJSON *object) {
  std::string joined;
  std::unordered_set<std::string> seen;
  const cJSON *authors = onion_cjson::item(object, "authors");
  if (!cJSON_IsArray(authors))
    return joined;
  const cJSON *author = nullptr;
  cJSON_ArrayForEach(author, authors) {
    const char *value = onion_cjson::string_value(author);
    if (!value || !*value || !seen.insert(value).second)
      continue;
    if (!joined.empty())
      joined += ", ";
    joined += value;
  }
  return joined;
}

void parse_cheats(const cJSON *array, Group &group) {
  if (!cJSON_IsArray(array))
    return;
  const cJSON *entry = nullptr;
  cJSON_ArrayForEach(entry, array) {
    Cheat cheat;
    cheat.key = onion_cjson::string_item(entry, "key", "");
    cheat.name = onion_cjson::string_item(entry, "name", "");
    cheat.enabled = onion_cjson::bool_item(entry, "enabled");
    if (!cheat.key.empty())
      group.cheats.push_back(std::move(cheat));
  }
}

bool parse_list(const std::string &json, List &out) {
  onion_cjson::Root root(json);
  if (!root || !onion_cjson::bool_item(root.get(), "canToggle"))
    return false;
  out.session_id = onion_cjson::string_item(root.get(), "sessionId", "");
  out.game = onion_cjson::string_item(root.get(), "name", "");
  out.authors = join_authors(root.get());
  const cJSON *groups = onion_cjson::item(root.get(), "groups");
  if (cJSON_IsArray(groups) && cJSON_GetArraySize(groups) > 0) {
    const cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, groups) {
      Group group;
      const std::string format = onion_cjson::string_item(entry, "format", "");
      const std::string authors = join_authors(entry);
      group.label = authors.empty() ? format : authors;
      if (!authors.empty() && !format.empty())
        group.label += " (" + format + ")";
      parse_cheats(onion_cjson::item(entry, "cheats"), group);
      if (!group.cheats.empty())
        out.groups.push_back(std::move(group));
    }
  } else {
    Group group;
    group.label = out.game;
    parse_cheats(onion_cjson::item(root.get(), "cheats"), group);
    if (!group.cheats.empty())
      out.groups.push_back(std::move(group));
  }
  return !out.session_id.empty() && !out.groups.empty();
}

std::atomic<bool> s_job_busy{false};
std::atomic<bool> s_job_done{false};
std::mutex s_job_mutex;
ToggleRequest s_toggle_request; /* written before the toggle thread starts */
JobResult s_job_result;         /* under s_job_mutex */

std::string read_and_remove(const std::string &path) {
  std::string text;
  FILE *file = std::fopen(path.c_str(), "rb");
  if (file) {
    char buffer[4096];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
      text.append(buffer, got);
    std::fclose(file);
  }
  unlink(path.c_str());
  return text;
}

void publish(JobResult &&result) {
  {
    std::lock_guard<std::mutex> lock(s_job_mutex);
    s_job_result = std::move(result);
  }
  s_job_done.store(true, std::memory_order_release);
  s_job_busy.store(false, std::memory_order_release);
}

void *fetch_job(void *) {
  JobResult result;
  result.kind = JobKind::Fetch;
  std::string title_id;
  int app_id = -1;
  onion_bigapp_process_t process{};
  if (Get_Running_App_TID(title_id, app_id)) {
    const int found = onion_resolve_running_bigapp(&process);
    const bool have_process = (found == 0 || found == -2) && process.pid > 0 &&
                              process.session_generation != 0 &&
                              process.process_name[0] != '\0';
    std::string path;
    if (have_process &&
        IPC_Client::getInstance(true).GetGameCheats(
            title_id, path, "runtime", process.pid, process.appid,
            process.process_name, process.session_generation,
            /*quiet=*/true) &&
        !path.empty())
      result.ok = parse_list(read_and_remove(path), result.list);
  }
  size_t count = 0;
  for (const Group &group : result.list.groups)
    count += group.cheats.size();
  LOG_INFO("[CHEATSMENU] list title=%s pid=%d ok=%d groups=%zu cheats=%zu",
           title_id.c_str(), static_cast<int>(process.pid), result.ok ? 1 : 0,
           result.list.groups.size(), count);
  publish(std::move(result));
  return nullptr;
}

void *toggle_job(void *) {
  const ToggleRequest request = s_toggle_request;
  JobResult result;
  result.kind = JobKind::Toggle;
  result.session_id = request.session_id;
  result.group = request.group;
  result.index = request.index;
  result.enabled = request.enabled;
  result.ok = IPC_Client::getInstance(true).ToggleGameCheat(
      request.session_id, request.key, request.enabled, result.reply);
  LOG_INFO("[CHEATSMENU] toggle enabled=%d ok=%d reply=%s",
           request.enabled ? 1 : 0, result.ok ? 1 : 0, result.reply.c_str());
  publish(std::move(result));
  return nullptr;
}

/* Busy is taken before done is read: publish() sets done first, and a job
   started over an unread result would overwrite it. */
bool start_job(void *(*job)(void *), const ToggleRequest *request) {
  if (s_job_busy.exchange(true, std::memory_order_acq_rel))
    return false;
  if (s_job_done.load(std::memory_order_acquire)) {
    s_job_busy.store(false, std::memory_order_release);
    return false;
  }
  if (request)
    s_toggle_request = *request;
  pthread_t thread{};
  if (pthread_create(&thread, nullptr, job, nullptr) != 0) {
    s_job_busy.store(false, std::memory_order_release);
    LOG_ERROR("[CHEATSMENU] failed to start a worker thread");
    return false;
  }
  pthread_detach(thread);
  return true;
}

} // namespace

bool StartFetch() { return start_job(fetch_job, nullptr); }

bool StartToggle(const ToggleRequest &request) {
  return start_job(toggle_job, &request);
}

bool TakeResult(JobResult &out) {
  if (!s_job_done.exchange(false, std::memory_order_acq_rel))
    return false;
  std::lock_guard<std::mutex> lock(s_job_mutex);
  out = std::move(s_job_result);
  return true;
}

} // namespace AniketosShell::CheatsList
