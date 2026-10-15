void ensure_background_directory() {
  (void)mkdir("/user/data/" HEN_FOLDER "/ShellUI", 0777);
  (void)mkdir(kBackgroundDirectory, 0777);
}

bool supported_background_image(const char *name) {
  if (!name || name[0] == '.')
    return false;
  std::string lower(name);
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  constexpr const char *extensions[] = {".png", ".dds", ".jpg", ".jpeg"};
  for (const char *extension : extensions) {
    const size_t length = std::strlen(extension);
    if (lower.size() >= length &&
        lower.compare(lower.size() - length, length, extension) == 0)
      return true;
  }
  return false;
}

void scan_background_images(bool force = false) {
  if (s_background_catalog_scanned && !force)
    return;
  ensure_background_directory();
  s_background_images.clear();
  DIR *directory = opendir(kBackgroundDirectory);
  if (directory) {
    while (dirent *entry = readdir(directory)) {
      if (!supported_background_image(entry->d_name))
        continue;
      const std::string path = std::string(kBackgroundDirectory) + "/" +
                               entry->d_name;
      struct stat info {};
      if (stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) &&
          info.st_size > 0)
        s_background_images.push_back(path);
    }
    closedir(directory);
  }
  std::sort(s_background_images.begin(), s_background_images.end(),
            [](const std::string &left, const std::string &right) {
              std::string left_folded(left);
              std::string right_folded(right);
              std::transform(left_folded.begin(), left_folded.end(),
                             left_folded.begin(), [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                             });
              std::transform(right_folded.begin(), right_folded.end(),
                             right_folded.begin(), [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                             });
              return left_folded < right_folded;
            });
  s_background_catalog_scanned = true;
  LOG_INFO("[ANIVSH:BACKGROUND] catalog path='%s' images=%zu",
           kBackgroundDirectory, s_background_images.size());
}

/* The chosen Home video in HomeVideo::Videos(), or the first one. */
int home_video_index() {
  const std::vector<std::string> &videos = HomeVideo::Videos();
  const auto found = std::find(videos.begin(), videos.end(),
                               g_settings.home_video_path);
  return found == videos.end() ? 0 : static_cast<int>(found - videos.begin());
}

int home_image_index() {
  const std::vector<std::string> &images = HomeVideo::Images();
  const auto found = std::find(images.begin(), images.end(),
                               g_settings.home_image_path);
  return found == images.end() ? 0 : static_cast<int>(found - images.begin());
}

int current_background_index() {
  const auto found = std::find(s_background_images.begin(),
                               s_background_images.end(),
                               g_settings.shell_background_image_path);
  return found == s_background_images.end()
             ? -1
             : static_cast<int>(found - s_background_images.begin());
}

