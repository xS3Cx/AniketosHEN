/* Copyright (C) 2025 OnionHEN / LightningMods
 * Pure page ownership policy for global SettingPage hooks.
 */
#pragma once

#include "toolbox_route.hpp"

#include <string_view>

namespace toolbox {

/** Handler family allowed to observe presses on the active Settings page. */
enum class OnPressDomain : unsigned char {
  PassThrough = 0,
  Root,
  Payloads,
  PayloadConfig,
  Sprx,
  Plugins,
  PluginConfig,
  AutoPayloads,
  Cheats,
  Account,
  Progress,
  RemotePlay,
  DynamicPlugin,
  PayloadCenter,
  Update,
};

/**
 * SettingPage.OnPressed is a class-wide hook, so page ownership must be checked
 * before inspecting an element id. Stock pages always pass through untouched.
 */
constexpr OnPressDomain onpress_domain_for_page(Page page) {
  switch (page) {
  case Page::DebugSettings:
    return OnPressDomain::Root;
  case Page::PayloadCenter:
  case Page::Payloads:
    return OnPressDomain::PayloadCenter;
  case Page::PayloadConfig:
    return OnPressDomain::PayloadConfig;
  case Page::OverlayMetrics:
  case Page::OverlayMetricConfig:
    return OnPressDomain::Root;
  case Page::Sprx:
  case Page::SprxConfig:
    return OnPressDomain::Sprx;
  case Page::Plugins:
    return OnPressDomain::Plugins;
  case Page::PluginConfig:
    return OnPressDomain::PluginConfig;
  case Page::AutoPayloads:
    return OnPressDomain::AutoPayloads;
  case Page::Cheats:
    return OnPressDomain::Cheats;
  case Page::Account:
    return OnPressDomain::Account;
  case Page::CheatProgress:
  case Page::PluginProgress:
    return OnPressDomain::Progress;
  case Page::RemotePlay:
    return OnPressDomain::RemotePlay;
  case Page::DynamicPlugin:
    return OnPressDomain::DynamicPlugin;
  case Page::PayloadAutostart:
  case Page::PayloadAdd:
  case Page::PayloadOrder:
  case Page::PayloadBrowse:
  case Page::PayloadDetail:
  case Page::PayloadSettings:
  case Page::PayloadIncluded:
  case Page::PayloadPostInstall:
    return OnPressDomain::PayloadCenter;
  case Page::PayloadProgress:
    return OnPressDomain::Progress;
  case Page::HenUpdate:
  case Page::HenUpdateOptions:
  case Page::HenUpdateReleases:
  case Page::HenUpdateProgress:
    return OnPressDomain::Update;
  case Page::Kbm:
    return OnPressDomain::Root;
  case Page::None:
  case Page::SuperuserPass:
  case Page::RedirectOgDebug:
    return OnPressDomain::PassThrough;
  }
  return OnPressDomain::PassThrough;
}

constexpr bool toolbox_owns_settings_page(Page page) {
  return onpress_domain_for_page(page) != OnPressDomain::PassThrough;
}

inline bool is_settings_xml_resource(std::string_view resource) {
  constexpr std::string_view suffix = ".xml";
  return resource.size() >= suffix.size() &&
         resource.substr(resource.size() - suffix.size()) == suffix;
}

/**
 * RuntimeAssembly.GetManifestResourceStream is process-wide too. Recognized
 * Toolbox routes and Settings XML resources change page ownership; unrelated
 * assets must not clear the active Toolbox page.
 */
inline bool resource_updates_active_page(Page resolved_page,
                                         std::string_view resource) {
  return resolved_page != Page::None || is_settings_xml_resource(resource);
}

inline Page active_page_after_resource(Page current_page, Page resolved_page,
                                       std::string_view resource) {
  return resource_updates_active_page(resolved_page, resource) ? resolved_page
                                                               : current_page;
}

} // namespace toolbox
