#include "aniketos_shell/home/home_video.hpp"
#include "home_video_internal.hpp"

#include "aniketos_theme/customization.hpp"
#include "hooked_funcs.hpp"
#include "shellui_types.hpp"

#include <onion/fs.h>
#include <onion/log.h>

#include <sys/stat.h>

/* A still image in the same place as the video, on the stage, with a PUI
   ImageBox instead of a player. Shown when no video plays: none is chosen, or
   the chosen one failed or was left off after a crash. */
namespace HomeVideo {
namespace {

/* The folder the older background-image option used, so pictures already
   copied there are found. */
constexpr const char *kImageDirectory =
    ONION_DIST_USER_DATA_ROOT "/ShellUI/background";
constexpr std::initializer_list<const char *> kImageExtensions = {
    ".png", ".jpg", ".jpeg"};
constexpr const char *kImageWidgetName = "id_aniketos_home_image";

uint32_t s_image_widget = 0; /* strong */
std::vector<std::string> s_images;
bool s_images_scanned = false;
int s_images_scanned_frame = 0;
std::string s_image_current; /* the file on screen */

void scan_images() {
  (void)mkdir(ONION_DIST_USER_DATA_ROOT "/ShellUI", 0777);
  (void)mkdir(kImageDirectory, 0777);
  std::vector<std::string> found;
  scan_directory(kImageDirectory, found, kImageExtensions);
  sort_by_file_name(found);
  if (found != s_images || !s_images_scanned)
    LOG_INFO("[HOMEIMAGE] catalog '%s' images=%zu", kImageDirectory,
             found.size());
  s_images = std::move(found);
  s_images_scanned = true;
  s_images_scanned_frame = frame_count;
}

/* The picture to show now, or nothing; `video_shown` when the video is on
   screen or about to be. */
std::string wanted_image(bool video_shown) {
  if (!(AniketosTheme::CustomizationOn() && g_settings.home_image_enabled) ||
      video_shown || if_exists(kKillSwitch))
    return {};
  if (!g_settings.home_image_path.empty() &&
      if_exists(g_settings.home_image_path.c_str()))
    return g_settings.home_image_path;
  if (!s_images_scanned ||
      (stage_wanted() && frame_count - s_images_scanned_frame >= 300))
    scan_images();
  for (const std::string &path : s_images)
    if (if_exists(path.c_str()))
      return path;
  return {};
}

void drop_image() {
  remove_widget(Reflect::Target(s_image_widget));
  Reflect::Release(s_image_widget);
  s_image_current.clear();
}

bool build_image(MonoObject *root, const std::string &path) {
  MonoObject *box = make(ui3_class("ImageBox"));
  if (!box) {
    LOG_ERROR("[HOMEIMAGE] no ImageBox class");
    return false;
  }
  s_image_widget = mono_gchandle_new(box, /*pinned=*/1);
  const ScreenSize size = size_of(root);
  /* fill the screen, cropping what does not fit */
  prepare_layer(box, size, kImageWidgetName, "AspectOutside");
  if (!load_image(box, path.c_str()) || !put_first(root, box))
    return false;
  LOG_INFO("[HOMEIMAGE] showing '%s' %.0fx%.0f", path.c_str(), size.width,
           size.height);
  return true;
}

} // namespace

/* The file at `path` into an image box, through the box's own loader. An
   ImageAsset made from the path stays empty on the stage ("[ImageAsset:
   null]"): pictures there never showed. */
bool load_image(MonoObject *box, const char *path) {
  void *arguments[1] = {mono_string_new(mono_domain_get(), path)};
  return call(box, "LoadAsync", 1, arguments);
}

/* Twice a second: the picture on screen is the one wanted, on the stage, under
   everything else. */
void tick_image(bool video_shown) {
  if (!s_images_scanned)
    scan_images(); /* also makes the folder */
  const std::string file = wanted_image(video_shown);
  MonoObject *widget = Reflect::Target(s_image_widget);
  const bool gone = widget && HomeScene::IsDisposed(widget);
  if (file != s_image_current || gone) {
    drop_image();
    if (file.empty())
      return;
    MonoObject *root = stage();
    if (!root)
      return;
    if (!build_image(root, file))
      drop_image();
    /* remembered even when it fails, so a bad file is not retried forever */
    s_image_current = file;
    return;
  }
  if (!widget)
    return;
  /* follows the screen it belongs to, as the video does */
  MonoObject *root = stage();
  if (root && (object_property(widget, "get_Parent") != root ||
               object_property(root, "get_FirstChild") != widget))
    put_first(root, widget);
}

MonoObject *picture_widget() { return Reflect::Target(s_image_widget); }

const std::vector<std::string> &Images(bool rescan) {
  if (rescan || !s_images_scanned)
    scan_images();
  return s_images;
}

void RestartImage() { drop_image(); }

} // namespace HomeVideo
