#pragma once

#include <cstddef>
#include <string>
#include <vector>

/* The running game's cheats, as the util payload's cheat service reports them,
 * and the util calls that read and toggle them. Both calls are synchronous IPC
 * round trips (the first list of a game also reads and decrypts its cheat
 * files), so they run on a short-lived thread and the UI thread picks the
 * result up on a later frame. One job at a time. */
namespace AniketosShell::CheatsList {

struct Cheat {
  std::string key;
  std::string name;
  bool enabled = false;
};

struct Group {
  std::string label;
  std::vector<Cheat> cheats;
};

struct List {
  std::string session_id;
  std::string game;
  std::string authors;
  std::vector<Group> groups;
};

struct ToggleRequest {
  std::string session_id;
  std::string key;
  bool enabled = false;
  size_t group = 0;
  size_t index = 0;
};

enum class JobKind { Fetch, Toggle };

struct JobResult {
  JobKind kind = JobKind::Fetch;
  bool ok = false;
  List list;
  std::string session_id; /* of the list a toggle was made on */
  std::string reply;
  size_t group = 0;
  size_t index = 0;
  bool enabled = false;
};

/* False while a job runs or its result is still unread. */
bool StartFetch();
bool StartToggle(const ToggleRequest &request);

/* The finished job's result, if there is one. */
bool TakeResult(JobResult &out);

} // namespace AniketosShell::CheatsList
