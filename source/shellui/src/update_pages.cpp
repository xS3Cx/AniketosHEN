#include "update_pages.hpp"

#include "ps5_settings_ui.hpp"
#include "settings_page_refresh.hpp"
#include "shellui_state.hpp"
#include "toolbox_i18n.hpp"

#include <onion/distribution.h>
#include <onion/update_manifest.hpp>
#include <onion/update_store.hpp>

#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>
#include <sys/stat.h>

namespace {

std::string alias_data(std::string_view path) {
  constexpr std::string_view kData = "/data/";
  constexpr std::string_view kUser = "/user/data/";
  if (path.starts_with(kData))
    return std::string(kUser) + std::string(path.substr(kData.size()));
  return std::string(path);
}

bool read_update_text(const std::string &path, std::string *out,
                      std::size_t maximum) {
  if (onion::update::read_text_limited(path, out, maximum))
    return true;
  return onion::update::read_text_limited(alias_data(path), out, maximum);
}

std::string selected_channel() {
  return g_settings.update_channel == onion::kUpdateChannelBeta
             ? std::string(onion::update::kBetaChannel)
             : std::string(onion::update::kStableChannel);
}

bool load_cached_manifest(std::string_view channel,
                          onion::update::Manifest *manifest) {
  std::string body;
  if (!read_update_text(onion::update::manifest_cache_path(channel), &body,
                        onion::update::kMaxManifestBytes))
    return false;
  const onion::update::ParseResult parsed = onion::update::parse_manifest(body);
  if (!parsed.ok || parsed.manifest.channel != channel)
    return false;
  *manifest = parsed.manifest;
  return true;
}

bool read_check_info_alias(onion::update::CheckInfo *info) {
  std::string body;
  if (read_update_text(onion::update::kUpdateCheckPath, &body,
                       static_cast<std::size_t>(64 * 1024))) {
    onion_cjson::Root root(body);
    if (!root || !cJSON_IsObject(root.get()))
      return false;
    const char *channel = onion_cjson::string_item(root.get(), "channel", "");
    const char *error = onion_cjson::string_item(root.get(), "error", "");
    info->channel = channel ? channel : "";
    info->error = error ? error : "";
    info->fetched_at = static_cast<long long>(
        onion_cjson::int_item(root.get(), "fetched_at", 0));
    return true;
  }
  return false;
}

bool read_ready(onion::update::ReadyInfo *info) {
  std::string body;
  if (read_update_text(onion::update::kUpdateReadyPath, &body,
                       static_cast<std::size_t>(64 * 1024))) {
    onion_cjson::Root root(body);
    if (!root || !cJSON_IsObject(root.get()))
      return false;
    const char *version = onion_cjson::string_item(root.get(), "version", "");
    const char *sha256 = onion_cjson::string_item(root.get(), "sha256", "");
    const char *summary = onion_cjson::string_item(root.get(), "summary", "");
    const int size = onion_cjson::int_item(root.get(), "size", 0);
    if (!version || !version[0] || !sha256 || !sha256[0] || size <= 0)
      return false;
    info->version = version;
    info->sha256 = sha256;
    info->summary = summary ? summary : "";
    info->size = static_cast<std::uint64_t>(size);
    info->downloaded_at = static_cast<long long>(
        onion_cjson::int_item(root.get(), "downloaded_at", 0));
    return true;
  }
  const std::string elf = onion::update::kUpdateElfPath;
  struct stat st {};
  return stat(elf.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

std::string format_time(long long stamp) {
  if (stamp <= 0)
    return {};
  struct tm value {};
  const time_t raw = static_cast<time_t>(stamp);
  localtime_r(&raw, &value);
  char text[32] = {};
  std::snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d",
                value.tm_year + 1900, value.tm_mon + 1, value.tm_mday,
                value.tm_hour, value.tm_min);
  return text;
}

std::string format_size(std::uint64_t size) {
  char text[32] = {};
  const double megabytes = static_cast<double>(size) / (1024.0 * 1024.0);
  std::snprintf(text, sizeof(text), "%.1f MB", megabytes);
  return text;
}

std::string channel_title(std::string_view channel) {
  return channel == onion::update::kBetaChannel
             ? toolbox_i18n::tr("hen_update.channel.beta")
             : toolbox_i18n::tr("hen_update.channel.stable");
}

bool hen_update_model(ps5ui::Node &model) {
  ps5ui::Page page("id_hen_update", toolbox_i18n::tr("hen_update.title"));
  const std::string channel = selected_channel();
  const std::string installed = onion::update::installed_version();
  const bool development = onion::update::development_version(installed);
  const std::string installed_text =
      development ? toolbox_i18n::tr("hen_update.development") : installed;
  onion::update::Manifest manifest;
  const bool have_manifest = load_cached_manifest(channel, &manifest);
  const bool newer =
      have_manifest && !development &&
      onion::update::manifest_newer(installed, manifest);
  onion::update::CheckInfo check;
  const bool have_check = read_check_info_alias(&check);
  const std::string checked_at =
      have_check ? format_time(check.fetched_at) : std::string{};
  onion::update::ReadyInfo ready;
  const bool have_ready = read_ready(&ready);

  page.label("id_update_installed",
             toolbox_i18n::format("hen_update.installed_fmt",
                                  installed_text.c_str()),
             ps5ui::Style::Center)
      .label("id_update_last_check",
             checked_at.empty()
                 ? toolbox_i18n::tr("hen_update.last_check_never")
                 : toolbox_i18n::format("hen_update.last_check_fmt",
                                        checked_at.c_str()),
             ps5ui::Style::Center);
  if (newer && !have_ready) {
    page.label("id_update_state",
               toolbox_i18n::format("hen_update.update_available_fmt",
                                    manifest.version.c_str()),
               ps5ui::Style::Center);
  }
  if (have_ready) {
    page.label("id_update_ready",
               toolbox_i18n::format("hen_update.ready_fmt",
                                    ready.version.c_str()),
               ps5ui::Style::Center)
        .label("id_update_ready_path",
               toolbox_i18n::format("hen_update.ready_path_fmt",
                                    onion::update::kUpdateElfPath),
               ps5ui::Style::Center)
        .label("id_update_ready_ftp",
               toolbox_i18n::tr("hen_update.ready_ftp"),
               ps5ui::Style::Center)
        .button("id_update_reboot",
                toolbox_i18n::tr("hen_update.reboot"),
                toolbox_i18n::tr("hen_update.reboot.sub"), std::nullopt,
                std::nullopt, ps5ui::Style::None,
                toolbox_i18n::tr("hen_update.reboot.confirm"),
                toolbox_i18n::confirm_phrase())
        .button("id_update_delete",
                toolbox_i18n::tr("hen_update.delete"),
                toolbox_i18n::tr("hen_update.delete.sub"), std::nullopt,
                std::nullopt, ps5ui::Style::None,
                toolbox_i18n::tr("hen_update.delete.confirm"),
                toolbox_i18n::confirm_phrase());
  } else if (newer) {
    page.button("id_update_download",
                toolbox_i18n::format("hen_update.download_fmt",
                                     manifest.version.c_str()),
                toolbox_i18n::tr("hen_update.download_action.sub"));
  } else {
    page.button("id_update_check", toolbox_i18n::tr("hen_update.check"),
                toolbox_i18n::tr("hen_update.check.sub"));
  }
  page.link("id_update_download_page",
            toolbox_i18n::tr("hen_update.download"),
            "hen_update_releases.xml",
            toolbox_i18n::tr("hen_update.download.sub"))
      .link("id_update_options", toolbox_i18n::tr("hen_update.options"),
            "hen_update_options.xml",
            toolbox_i18n::tr("hen_update.options.sub"));
  model = page.root();
  return true;
}

bool hen_update_options_model(ps5ui::Node &model) {
  ps5ui::Page page("id_hen_update_options",
                   toolbox_i18n::tr("hen_update.options"));
  page.toggle("id_update_auto_check",
              toolbox_i18n::tr("hen_update.auto_check"),
              g_settings.update_auto_check,
              toolbox_i18n::tr("hen_update.auto_check.sub"))
      .toggle("id_update_beta", toolbox_i18n::tr("hen_update.beta"),
              g_settings.update_channel == onion::kUpdateChannelBeta,
              toolbox_i18n::tr("hen_update.beta.sub"));
  model = page.root();
  return true;
}

bool hen_update_releases_model(ps5ui::Node &model) {
  ps5ui::Page page("id_hen_update_releases",
                   toolbox_i18n::tr("hen_update.releases.title"));
  const std::string channel = selected_channel();
  onion::update::Manifest manifest;
  const bool have_manifest = load_cached_manifest(channel, &manifest);
  page.label("id_update_release_channel",
             toolbox_i18n::format("hen_update.channel_fmt",
                                  channel_title(channel).c_str()),
             ps5ui::Style::Center);
  if (have_manifest) {
    page.label("id_update_release_version",
               toolbox_i18n::format("hen_update.releases.version_fmt",
                                    manifest.version.c_str()),
               ps5ui::Style::Center)
        .label("id_update_release_date",
               toolbox_i18n::format("hen_update.releases.date_fmt",
                                    manifest.published_at.c_str()),
               ps5ui::Style::Center)
        .label("id_update_release_size",
               toolbox_i18n::format("hen_update.releases.size_fmt",
                                    format_size(manifest.size).c_str()),
               ps5ui::Style::Center)
        .label("id_update_release_summary", manifest.summary,
               ps5ui::Style::Center)
        .button("id_update_download",
                toolbox_i18n::tr("hen_update.releases.download"),
                toolbox_i18n::tr("hen_update.download_action.sub"))
        .button("id_update_check", toolbox_i18n::tr("hen_update.check"),
                toolbox_i18n::tr("hen_update.check.sub"));
  } else {
    page.label("id_update_release_none",
               toolbox_i18n::tr("hen_update.releases.no_cache"),
               ps5ui::Style::Center)
        .button("id_update_check", toolbox_i18n::tr("hen_update.check"),
                toolbox_i18n::tr("hen_update.check.sub"));
  }
  model = page.root();
  return true;
}

} // namespace

void generate_hen_update_xml(std::string &xml_buffer) {
  ps5ui::Node model;
  hen_update_model(model);
  xml_buffer = onion::shellui::settings::publish(model, hen_update_model);
}

void generate_hen_update_options_xml(std::string &xml_buffer) {
  ps5ui::Node model;
  hen_update_options_model(model);
  xml_buffer =
      onion::shellui::settings::publish(model, hen_update_options_model);
}

void generate_hen_update_releases_xml(std::string &xml_buffer) {
  ps5ui::Node model;
  hen_update_releases_model(model);
  xml_buffer =
      onion::shellui::settings::publish(model, hen_update_releases_model);
}
