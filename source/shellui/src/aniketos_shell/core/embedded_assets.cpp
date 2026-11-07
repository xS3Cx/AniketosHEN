#include "aniketos_shell/core/embedded_assets.hpp"

#include "aniketos_shell/core/rco_names.hpp"
#include "aniketos_theme/logo_tint.hpp"
#include "aniketos_ui/monotonic_clock.hpp"
#include "png_encode.hpp"
#include "shellui_types.hpp"

#include <onion/distribution.h>
#include <onion/fs.h>
#include <onion/log.h>
#include <onion/notify.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern "C" {
extern const unsigned char aniketos_icon_png_start[];
extern const unsigned char aniketos_icon_png_end[];
extern const unsigned char aniketos_icon_rgba_start[];
extern const unsigned char aniketos_icon_rgba_end[];
extern const unsigned char color_track_png_start[];
extern const unsigned char color_track_png_end[];
extern const unsigned char color_track_gray_png_start[];
extern const unsigned char color_track_gray_png_end[];
extern const unsigned char color_track_solid_png_start[];
extern const unsigned char color_track_solid_png_end[];
extern const unsigned char color_track_fade_png_start[];
extern const unsigned char color_track_fade_png_end[];
extern const unsigned char home_menu_internet_on_png_start[];
extern const unsigned char home_menu_internet_on_png_end[];
extern const unsigned char home_menu_internet_off_png_start[];
extern const unsigned char home_menu_internet_off_png_end[];
extern const unsigned char home_background_vignette_png_start[];
extern const unsigned char home_background_vignette_png_end[];
extern const unsigned char home_background_edges_png_start[];
extern const unsigned char home_background_edges_png_end[];
}

namespace {

constexpr int kLogoSide = 128;
/* Shorter than the Home reload's wait (option_change), which reads the icon. */
constexpr uint64_t kLogoRestMs = 1000;

/* The colour the logo file is in, -1 when there is no such file. */
std::atomic<int> s_logo_colour{-1};
std::atomic<uint64_t> s_logo_held_until_ms{0};
std::mutex s_logo_mutex;
char s_logo_path[160];

struct EmbeddedImage {
  const char *file; /* its name in our data folder */
  const unsigned char *start;
  const unsigned char *end;
  char path[160];
  bool tried;
};

/* A failed write leaves no partial file behind. */
bool write_file(const char *path, const void *data, size_t size) {
  const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    return false;
  const bool written = write(fd, data, size) == static_cast<ssize_t>(size);
  close(fd);
  if (!written)
    unlink(path);
  return written;
}

/* Writes the image into our own data folder, once, and gives its path (empty
   if it could not be written). */
const char *written_path(EmbeddedImage &image) {
  if (image.tried)
    return image.path;
  image.tried = true;
  const size_t size = static_cast<size_t>(image.end - image.start);
  char candidate[160];
  std::snprintf(candidate, sizeof(candidate), "%s/%s",
                ONION_DIST_USER_DATA_ROOT, image.file);
  struct stat st {};
  if ((stat(candidate, &st) != 0 || static_cast<size_t>(st.st_size) != size) &&
      !write_file(candidate, image.start, size))
    return image.path;
  std::snprintf(image.path, sizeof(image.path), "%s", candidate);
  return image.path;
}

/* Back to no coloured logo; false when there was none. */
bool clear_logo_folder() {
  DIR *folder = opendir(ONION_LOGO_DIR);
  if (!folder)
    return false;
  bool any = false;
  while (const dirent *entry = readdir(folder))
    if (entry->d_name[0] != '.') {
      unlink((std::string(ONION_LOGO_DIR) + "/" + entry->d_name).c_str());
      any = true;
    }
  closedir(folder);
  return any;
}

void write_notification_logo(const void *data, size_t size) {
  if (!write_file_atomic_if_changed(ONION_NOTIFY_ICON_PATH, data, size))
    LOG_WARN("logo: the notifications' picture stays as it was");
}

/* The logo in the chosen colour, under a name of its own: the system keeps
   pictures by their path. Empty when it cannot be made. The Control Center
   button asks every frame, so while the colour is held this is the last
   picture: a PNG and two file writes per slider step stalled the UI thread. */
const char *coloured_logo() {
  char (&path)[sizeof(s_logo_path)] = s_logo_path;
  const auto channel = [](int value) { return std::clamp(value, 0, 255); };
  const int red = channel(g_settings.logo_color_r);
  const int green = channel(g_settings.logo_color_g);
  const int blue = channel(g_settings.logo_color_b);
  const int colour = red << 16 | green << 8 | blue;
  std::lock_guard<std::mutex> lock(s_logo_mutex);
  if (colour == s_logo_colour.load(std::memory_order_relaxed) ||
      (*path && AniketosUI::MonotonicMs() <
                    s_logo_held_until_ms.load(std::memory_order_relaxed)))
    return path;
  s_logo_colour.store(colour, std::memory_order_relaxed);
  path[0] = '\0';
  std::vector<uint8_t> pixels(aniketos_icon_rgba_start, aniketos_icon_rgba_end);
  if (pixels.size() != kLogoSide * kLogoSide * 4)
    return path;
  LogoTint::Apply(pixels.data(), pixels.size() / 4, red, green, blue);
  const std::vector<uint8_t> png = Png::Encode(kLogoSide, kLogoSide, pixels);
  (void)mkdir(ONION_LOGO_DIR, 0777);
  clear_logo_folder();
  char candidate[160];
  std::snprintf(candidate, sizeof(candidate), "%s/%06x.png", ONION_LOGO_DIR,
                colour);
  if (write_file(candidate, png.data(), png.size())) {
    std::snprintf(path, sizeof(path), "%s", candidate);
    write_notification_logo(png.data(), png.size());
  }
  return path;
}

} // namespace

void HoldAniketosIcon() {
  s_logo_held_until_ms.store(AniketosUI::MonotonicMs() + kLogoRestMs,
                             std::memory_order_relaxed);
}

const char *AniketosIconPath() {
  static EmbeddedImage stock = {"aniketos_icon.png", aniketos_icon_png_start,
                                aniketos_icon_png_end};
  static std::atomic<bool> stock_checked{false};
  if (g_settings.logo_color_enabled) {
    stock_checked.store(false, std::memory_order_relaxed);
    const char *coloured = coloured_logo();
    if (*coloured)
      return coloured;
  } else if (!stock_checked.exchange(true, std::memory_order_relaxed) &&
             clear_logo_folder()) {
    std::lock_guard<std::mutex> lock(s_logo_mutex);
    s_logo_colour.store(-1, std::memory_order_relaxed);
    s_logo_path[0] = '\0'; /* its file is gone */
    write_notification_logo(
        aniketos_icon_png_start,
        static_cast<size_t>(aniketos_icon_png_end - aniketos_icon_png_start));
  }
  return written_path(stock);
}

/* The firmware's own icon set, looked up by name: an id it lacks would leave
   the button blank, so then the logo stays. */
const char *HenButtonGlyph() {
  static constexpr const char *kIconSet =
      "/system_ex/vsh_asset/Sce.PlayStation.PUI_UI3.rco";
  static constexpr const char *kGlyph = "troubleshoot"; /* a toolbox */
  if (g_settings.hen_button_icon != 1)
    return "";
  static const bool present = [] {
    bool found = false;
    const int fd = open(kIconSet, O_RDONLY);
    unsigned char head[RcoNames::kHeaderSize];
    if (fd >= 0 && read(fd, head, sizeof(head)) == (ssize_t)sizeof(head)) {
      const size_t end = RcoNames::TableEnd(head, sizeof(head));
      if (end && end <= (8u << 20)) {
        std::vector<unsigned char> names(end);
        found = pread(fd, names.data(), end, 0) == (ssize_t)end &&
                RcoNames::Has(names.data(), end,
                              (std::string("iconid_") + kGlyph).c_str());
      }
    }
    if (fd >= 0)
      close(fd);
    if (found)
      LOG_INFO("HEN button: system icon iconid_%s", kGlyph);
    else
      LOG_WARN("HEN button: no iconid_%s in %s; the logo stays", kGlyph,
               kIconSet);
    return found;
  }();
  return present ? kGlyph : "";
}

const char *ColorTrackPath(ColorTrack track) {
  static EmbeddedImage images[] = {
      {"color_track.png", color_track_png_start, color_track_png_end},
      {"color_track_gray.png", color_track_gray_png_start,
       color_track_gray_png_end},
      {"color_track_solid.png", color_track_solid_png_start,
       color_track_solid_png_end},
      {"color_track_fade.png", color_track_fade_png_start,
       color_track_fade_png_end}};
  return written_path(images[static_cast<int>(track)]);
}

const char *HomeEffectImagePath(HomeEffectImage image) {
  static EmbeddedImage images[] = {
      {"home_vignette.png", home_background_vignette_png_start,
       home_background_vignette_png_end},
      {"home_edges.png", home_background_edges_png_start,
       home_background_edges_png_end}};
  return written_path(images[static_cast<int>(image)]);
}

const char *InternetIconPath(bool enabled) {
  static EmbeddedImage on = {"internet_on.png", home_menu_internet_on_png_start,
                             home_menu_internet_on_png_end};
  static EmbeddedImage off = {"internet_off.png",
                              home_menu_internet_off_png_start,
                              home_menu_internet_off_png_end};
  return written_path(enabled ? on : off);
}
