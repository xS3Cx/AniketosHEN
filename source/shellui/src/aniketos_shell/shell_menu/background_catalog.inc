void ensure_background_directory() {
  (void)mkdir(ONION_DIST_USER_DATA_ROOT "/ShellUI", 0777);
  (void)mkdir(kBackgroundDirectory, 0777);
}

/* Where a saved path is in its list, -1 when it is not there. */
int index_of(const std::vector<std::string> &paths, const std::string &chosen) {
  const auto found = std::find(paths.begin(), paths.end(), chosen);
  return found == paths.end() ? -1 : static_cast<int>(found - paths.begin());
}

/* The chosen Home video in HomeVideo::Videos(), or the first one. */
int home_video_index() {
  return std::max(0, index_of(HomeVideo::Videos(), g_settings.home_video_path));
}

int custom_font_index() {
  return std::max(0, index_of(AniketosTheme::CustomFont::Files(),
                              g_settings.custom_font_path));
}

int account_avatar_index() {
  return std::max(0, index_of(AccountAvatar::Files(),
                              g_settings.account_avatar_path));
}

int home_image_index() {
  return std::max(0, index_of(HomeVideo::Images(), g_settings.home_image_path));
}
