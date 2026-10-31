/* Keyboard & mouse for games: profile JSON and the files under kbm/.
 *
 *   <root>/titles/<TITLEID>.json  one profile per game
 *   <root>/default.json           profile for games without their own
 *   <root>/settings.json          global switch and suspend key
 *
 * root is kStoreRoot in the daemon and kStoreRootUser in ShellUI (the same
 * storage seen from the SceShellUI sandbox). Needs cJSON at link time. */
#pragma once

#include <onion/distribution.h>
#include <onion/kbm_profile.hpp>

#include <stddef.h>
#include <string>
#include <vector>

namespace onion::kbm {

inline constexpr const char *kStoreRoot = ONION_DIST_DATA_ROOT "/kbm";
inline constexpr const char *kStoreRootUser = ONION_DIST_USER_DATA_ROOT "/kbm";
constexpr size_t kMaxProfileFileSize = 1024 * 1024;

struct Settings {
  bool enabled = false;
  Input suspend_key = kDefaultSuspendKey;
};

/* JSON. ProfileFromJson fails only when the text does not parse, is not an
 * object, or has a newer "schema" (error says which); everything else is
 * repaired by Validate() and reported in warnings. */
bool ProfileFromJson(const char *text, size_t length, Profile *out,
                     Warnings *warnings = nullptr, std::string *error = nullptr);
std::string ProfileToJson(const Profile &profile);
/* A refused suspend key (unknown, left/right click) keeps Pause and is
 * reported in warnings. */
bool SettingsFromJson(const char *text, size_t length, Settings *out,
                      Warnings *warnings = nullptr);
std::string SettingsToJson(const Settings &settings);

/* Title ids become file names: only [A-Z0-9_], 1..16 chars, pass. */
bool IsValidTitleId(const std::string &title_id);
std::string TitleProfilePath(const std::string &root, const std::string &title_id); /* "" if invalid */
std::string DefaultProfilePath(const std::string &root);
std::string SettingsPath(const std::string &root);

/* False when the file is missing, too large or fails ProfileFromJson. */
bool LoadProfile(const std::string &path, Profile *out,
                 Warnings *warnings = nullptr, std::string *error = nullptr);
/* Creates the folder; writes a temp file and renames it over the old one.
 * Refuses a profile whose JSON is over kMaxProfileFileSize. */
bool SaveProfile(const std::string &path, const Profile &profile);
bool DeleteProfile(const std::string &path);

/* The title's own profile, else default.json, else DefaultProfile(). */
Profile ResolveProfile(const std::string &root, const std::string &title_id,
                       Warnings *warnings = nullptr);

/* Opt-in per game: true only when the title's own profile, or else
 * default.json, loads and has enabled=true. DefaultProfile() alone never
 * applies. The global Settings::enabled switch is checked separately. */
bool TitleEnabled(const std::string &root, const std::string &title_id);

/* Title ids that have their own profile, sorted. */
std::vector<std::string> ListTitleProfiles(const std::string &root);

Settings LoadSettings(const std::string &root, Warnings *warnings = nullptr); /* defaults when missing */
bool SaveSettings(const std::string &root, const Settings &settings);

} // namespace onion::kbm
