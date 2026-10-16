#include "aniketos_shell/home_bookmark.hpp"

#include "aniketos_shell/system_dialogs.hpp"
#include "hooked_funcs.hpp"
#include "onion_cjson.hpp"
#include "toolbox_i18n.hpp"

#include <onion/detour.h>
#include <onion/distribution.h>
#include <onion/log.h>
#include <onion/settings.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>

extern onion::Settings g_settings;

namespace HomeBookmark {
namespace {

using toolbox_i18n::tr;

constexpr const char *kFile = ONION_DIST_USER_DATA_ROOT "/home_bookmark.json";
/* What the script asks Home's registry module for. */
constexpr const char *kRegistryKey = "aniketos.home_bookmark";
/* Bytes of a name, cut at a character boundary: a tab's caption. */
constexpr size_t kMaxNameBytes = 40;

std::mutex s_mutex; /* everything down to s_revision */
bool s_loaded = false;
std::string s_name; /* empty while there is no bookmark */
std::vector<std::string> s_titles;
/* Changes whenever the script has to fetch the rows again. */
unsigned s_revision = 1;

bool valid_title(const std::string &id) {
  if (id.size() != 9)
    return false;
  for (const char c : id)
    if (!std::isalnum(static_cast<unsigned char>(c)))
      return false;
  return true;
}

/* A name as typed: control characters out, spaces trimmed, cut short at a
   character boundary. */
std::string clean_name(const std::string &typed) {
  std::string name;
  for (const char c : typed)
    if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f)
      name += c;
  const size_t first = name.find_first_not_of(' ');
  if (first == std::string::npos)
    return {};
  name = name.substr(first, name.find_last_not_of(' ') - first + 1);
  if (name.size() > kMaxNameBytes) {
    size_t cut = kMaxNameBytes;
    while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xc0) == 0x80)
      --cut;
    name.resize(cut);
  }
  return name;
}

void load_locked() {
  if (s_loaded)
    return;
  s_loaded = true;
  FILE *file = std::fopen(kFile, "rb");
  if (!file)
    return;
  std::string text(64 * 1024, '\0');
  text.resize(std::fread(text.data(), 1, text.size(), file));
  std::fclose(file);
  onion_cjson::Root root(text);
  if (!root)
    return;
  const char *name = onion_cjson::string_item(root.get(), "name", "");
  const cJSON *titles = cJSON_GetObjectItemCaseSensitive(root.get(), "titles");
  const cJSON *title = nullptr;
  cJSON_ArrayForEach(title, titles) {
    if (cJSON_IsString(title) && valid_title(title->valuestring) &&
        std::find(s_titles.begin(), s_titles.end(), title->valuestring) ==
            s_titles.end())
      s_titles.push_back(title->valuestring);
  }
  s_name = s_titles.empty() ? std::string() : clean_name(name);
  if (s_name.empty())
    s_titles.clear();
  LOG_INFO("[BOOKMARK] loaded %zu titles", s_titles.size());
}

void save_locked() {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "name", s_name.c_str());
  cJSON *titles = cJSON_AddArrayToObject(root, "titles");
  for (const std::string &title : s_titles)
    cJSON_AddItemToArray(titles, cJSON_CreateString(title.c_str()));
  const std::string text = onion_cjson::print_owned(root, true);
  const std::string temporary = std::string(kFile) + ".tmp";
  FILE *file = text.empty() ? nullptr : std::fopen(temporary.c_str(), "wb");
  bool written = false;
  if (file) {
    written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    written = std::fclose(file) == 0 && written;
  }
  if (!written || std::rename(temporary.c_str(), kFile) != 0) {
    unlink(temporary.c_str());
    LOG_WARN("[BOOKMARK] could not save %s", kFile);
  }
}

/* Saved, and the script told to fetch the rows again. */
void changed_locked() {
  ++s_revision;
  save_locked();
}

/* "'ID','ID'" for an SQL list; the ids are letters and digits only. */
std::string sql_list(const std::vector<std::string> &titles) {
  std::string out;
  for (const std::string &title : titles) {
    if (!valid_title(title))
      continue;
    if (!out.empty())
      out += ",";
    out += "'" + title + "'";
  }
  return out.empty() ? "''" : out;
}

/* A text of the catalogue with the bookmark's name put in. */
std::string with_name(const char *key, const std::string &name) {
  const std::string text = tr(key);
  const size_t at = text.find("%s");
  return at == std::string::npos
             ? text
             : text.substr(0, at) + name + text.substr(at + 2);
}

/* The tab's caption comes in with Home's start: Home is started again when
   the tab comes, goes or is renamed. */
void reload_home() {
  if (g_settings.home_bookmark)
    shellui_request_home_reload();
}

/* ---- Home's registry module -------------------------------------------- */

std::string text_of(MonoObject *managed) {
  if (!managed)
    return {};
  const char *utf =
      mono_string_to_utf8(reinterpret_cast<MonoString *>(managed));
  std::string text = utf ? utf : "";
  if (utf && mono_free)
    mono_free(const_cast<char *>(utf));
  return text;
}

/* IPromise.Resolve(object) of the promise the module was handed. */
bool resolve(MonoObject *promise, const std::string &value) {
  MonoMethod *method = nullptr;
  for (MonoClass *k = mono_object_get_class(promise); k && !method;
       k = mono_class_get_parent(k)) {
    method = mono_class_get_method_from_name(k, "Resolve", 1);
    void *iter = nullptr;
    while (!method) {
      /* an explicit implementation is named after its interface */
      MonoMethod *candidate = mono_class_get_methods(k, &iter);
      if (!candidate)
        break;
      const char *name = mono_method_get_name(candidate);
      const size_t length = name ? std::strlen(name) : 0;
      MonoMethodSignature *signature = mono_method_signature(candidate);
      if (length > 8 && std::strcmp(name + length - 8, ".Resolve") == 0 &&
          signature && mono_signature_get_param_count(signature) == 1)
        method = candidate;
    }
  }
  if (!method)
    return false;
  void *args[1] = {mono_string_new(mono_domain_get(), value.c_str())};
  MonoObject *exception = nullptr;
  mono_runtime_invoke(method, promise, args, &exception);
  return exception == nullptr;
}

using RegistryGet = void (*)(MonoObject *, MonoObject *, MonoObject *);
RegistryGet s_registry_get_original = nullptr;

/* RegistryModule.Get(JObject param, IPromise promise): Home's reads of the
   system registry. The one asking for our key gets the revision instead. */
void registry_get_hook(MonoObject *module, MonoObject *param,
                       MonoObject *promise) {
  if (param && promise && Enabled()) {
    MonoObject *exception = nullptr;
    MonoString *shown = mono_object_to_string(param, &exception);
    const std::string asked =
        exception ? std::string()
                  : text_of(reinterpret_cast<MonoObject *>(shown));
    if (asked.find(kRegistryKey) != std::string::npos) {
      unsigned revision = 0;
      {
        std::lock_guard<std::mutex> lock(s_mutex);
        revision = s_revision;
      }
      if (resolve(promise, "{\"rev\":" + std::to_string(revision) + "}"))
        return;
      LOG_WARN("[BOOKMARK] could not answer the script");
    }
  }
  if (s_registry_get_original)
    s_registry_get_original(module, param, promise);
}

/* ---- requests from the menus ------------------------------------------ */

struct Request {
  enum class Kind { None, Add, Remove, Rename } kind = Kind::None;
  std::string title;
};
std::mutex s_request_mutex;
Request s_request;

/* The prompt on screen (UI thread only). */
enum class Step { Idle, Name, Rename };
Step s_step = Step::Idle;
std::string s_pending; /* the game the new bookmark is made for */

void open_prompt(Step step, const std::string &text) {
  SystemDialogs::PromptOptions options;
  options.title = step == Step::Rename ? tr("bookmark.rename_title")
                                       : tr("bookmark.new_title");
  options.message = tr("bookmark.name_message");
  options.text = text;
  options.positive =
      step == Step::Rename ? tr("bookmark.save") : tr("bookmark.create");
  options.negative = tr("bookmark.cancel");
  std::string failure;
  if (!SystemDialogs::ShowPrompt(options, &failure)) {
    LOG_WARN("[BOOKMARK] prompt: %s", failure.c_str());
    s_step = Step::Idle;
    return;
  }
  s_step = step;
}

void add_title(const std::string &title) {
  std::string name;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    if (s_name.empty()) {
      /* no bookmark yet: it is made, with its name, around this game */
      s_pending = title;
    } else {
      if (std::find(s_titles.begin(), s_titles.end(), title) ==
          s_titles.end()) {
        s_titles.insert(s_titles.begin(), title);
        changed_locked();
      }
      name = s_name;
    }
  }
  if (name.empty()) {
    open_prompt(Step::Name, tr("bookmark.default_name"));
    return;
  }
  LOG_INFO("[BOOKMARK] %s added", title.c_str());
  notify("%s", with_name("bookmark.added", name).c_str());
}

void remove_title(const std::string &title) {
  std::string name;
  bool emptied = false;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    const auto at = std::find(s_titles.begin(), s_titles.end(), title);
    if (at == s_titles.end())
      return;
    s_titles.erase(at);
    name = s_name;
    /* the last game takes the bookmark with it */
    emptied = s_titles.empty();
    if (emptied)
      s_name.clear();
    changed_locked();
  }
  LOG_INFO("[BOOKMARK] %s removed%s", title.c_str(),
           emptied ? ", bookmark gone" : "");
  notify("%s", with_name("bookmark.removed", name).c_str());
  if (emptied)
    reload_home();
}

void named(bool confirmed, const std::string &typed) {
  const Step step = s_step;
  s_step = Step::Idle;
  const std::string title = s_pending;
  s_pending.clear();
  if (!confirmed)
    return;
  std::string name = clean_name(typed);
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    if (step == Step::Rename) {
      if (name.empty() || s_name.empty() || name == s_name)
        return;
      s_name = name;
      changed_locked();
    } else {
      if (!valid_title(title))
        return;
      if (name.empty())
        name = tr("bookmark.default_name");
      s_name = name;
      s_titles = {title};
      changed_locked();
    }
  }
  LOG_INFO("[BOOKMARK] %s", step == Step::Rename ? "renamed" : "made");
  if (step != Step::Rename)
    notify("%s", with_name("bookmark.added", name).c_str());
  reload_home();
}

} // namespace

void Install() {
  if (s_registry_get_original)
    return;
  MonoImage *image = getDLLimage("ReactNative.Modules.Vsh.dll");
  MonoClass *klass =
      image ? mono_class_from_name(image, "ReactNative.Modules.Vsh",
                                   "RegistryModule")
            : nullptr;
  const uint64_t address =
      klass ? Get_Address_of_Method(image, klass, "Get", 2) : 0;
  const bool hooked =
      address &&
      InstallDetour(address, reinterpret_cast<void *>(&registry_get_hook),
                    reinterpret_cast<void **>(&s_registry_get_original));
  LOG_INFO("[BOOKMARK] registry hook target=0x%lx installed=%d",
           static_cast<unsigned long>(address), hooked ? 1 : 0);
  if (!hooked)
    s_registry_get_original = nullptr;
}

bool Enabled() {
  return g_settings.home_bookmark && s_registry_get_original != nullptr;
}

bool Allowed(const std::string &title_id) {
  return Enabled() && valid_title(title_id) &&
         title_id.compare(0, 4, "NPXS") != 0;
}

bool Contains(const std::string &title_id) {
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  return std::find(s_titles.begin(), s_titles.end(), title_id) !=
         s_titles.end();
}

std::string Name() {
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  return s_name;
}

bool Shown() {
  if (!Enabled())
    return false;
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  return !s_name.empty() && !s_titles.empty();
}

std::string Where(const std::string &where) {
  std::lock_guard<std::mutex> lock(s_mutex);
  load_locked();
  const std::string ids = "(" + sql_list(s_titles) + ")";
  /* every game in it, kept on Home or not, as the Emulators tab does */
  if (where.find('?') == std::string::npos)
    return " WHERE (icon.visible = 1 )  AND (icon.titleId IN " + ids + " ) ";
  return where + " AND (icon.titleId IN " + ids + " ) ";
}

bool HandleUri(const std::string &uri, const std::string &menu_title) {
  Request request;
  if (uri == kAddUri)
    request.kind = Request::Kind::Add;
  else if (uri == kRemoveUri)
    request.kind = Request::Kind::Remove;
  else
    return false;
  if (!valid_title(menu_title)) {
    LOG_WARN("[BOOKMARK] no title for '%s' ('%s')", uri.c_str(),
             menu_title.c_str());
    return true;
  }
  request.title = menu_title;
  std::lock_guard<std::mutex> lock(s_request_mutex);
  s_request = request;
  return true;
}

void RequestRename() {
  Request request;
  request.kind = Request::Kind::Rename;
  std::lock_guard<std::mutex> lock(s_request_mutex);
  s_request = request;
}

void Tick() {
  if (s_step != Step::Idle) {
    bool confirmed = false;
    std::string typed;
    if (SystemDialogs::PollPrompt(&confirmed, &typed))
      named(confirmed, typed);
    return;
  }
  Request request;
  {
    std::lock_guard<std::mutex> lock(s_request_mutex);
    std::swap(request, s_request);
  }
  switch (request.kind) {
  case Request::Kind::Add:
    add_title(request.title);
    break;
  case Request::Kind::Remove:
    remove_title(request.title);
    break;
  case Request::Kind::Rename: {
    const std::string name = Name();
    if (name.empty())
      notify("%s", tr("bookmark.none"));
    else
      open_prompt(Step::Rename, name);
    break;
  }
  case Request::Kind::None:
    break;
  }
}

} // namespace HomeBookmark
