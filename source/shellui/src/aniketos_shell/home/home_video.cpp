#include "aniketos_shell/home/home_video.hpp"
#include "home_video_internal.hpp"

#include "aniketos_theme/customization.hpp"
#include "aniketos_ui/small_file.hpp"
#include "hooked_funcs.hpp"
#include "shellui_types.hpp"

#include <onion/fs.h>
#include <onion/log.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace HomeVideo {

int frame_count = 0;

namespace {

/* Where users put their videos. */
constexpr const char *kVideoDirectory =
    ONION_DIST_USER_DATA_ROOT "/ShellUI/videos";
/* The same folder on a USB drive is listed too, so nothing has to be copied. */
constexpr const char *kUsbVideoDirectory = "/mnt/usb%d/" ONION_DIST_DIR_NAME
                                           "/videos";
constexpr int kUsbSlots = 8;
constexpr std::initializer_list<const char *> kVideoExtensions = {".mp4",
                                                                  ".m4v"};
/* Counts starts that never got as far as playing: written before the player
   is built and removed once it plays (see pending_starts()). */
constexpr const char *kPending = ONION_DIST_USER_DATA_ROOT "/home_video_pending";
constexpr const char *kWidgetName = "id_aniketos_home_video";
/* Checks (twice a second) out of sight before the player is let go. */
constexpr int kReleaseAfterChecks = 40;

enum class State { Idle, Waiting, Playing, Failed, Off, Hidden };
State s_state = State::Idle;
uint32_t s_player = 0; /* strong handles */
uint32_t s_view = 0;
int s_retry_at = 0;
int s_failures = 0;
int s_played_checks = 0;
int s_start_checks = 0; /* checks in Playing before the player played */
int s_hidden_checks = 0;
int32_t s_last_status = -1;
std::string s_current;      /* the file being played */
/* Where the video was when Home went out of sight, in the player's own
   units; the player built when Home returns starts there, so the video goes
   on instead of starting over. 0 to start from the beginning. */
uint64_t s_resume_at = 0;
/* The last position read while playing, kept for when the player goes with
   its scene before it can be asked (Settings closed). */
uint64_t s_last_position = 0;
bool s_sound_applied = false;
int s_volume_applied = -1;
std::vector<std::string> s_videos;
bool s_videos_scanned = false;
int s_scanned_frame = 0;
/* PlayerStateType members, by name */
int32_t s_status_error = -1;
int32_t s_status_playing = -1;
int32_t s_status_at_end = -1;
int32_t s_status_stopped = -1;
int32_t s_status_paused = -1;
/* checks the player has stayed paused while the stage is in sight */
int s_paused_checks = 0;

/* How many starts in a row never got as far as playing. One can be a
   console switched off or a HEN unloaded at the wrong moment; two in a row
   is taken as the video bringing ShellUI down. */
int pending_starts() {
  char text[8] = "";
  const int fd = open(kPending, O_RDONLY);
  if (fd < 0)
    return 0;
  const ssize_t got = read(fd, text, sizeof(text) - 1);
  close(fd);
  return got > 0 ? std::max(1, std::atoi(text)) : 1;
}

void mark_pending() {
  char text[8];
  const int length =
      std::snprintf(text, sizeof(text), "%d", pending_starts() + 1);
  const int fd = open(kPending, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    return;
  (void)!write(fd, text, static_cast<size_t>(length));
  close(fd);
}

void teardown() {
  MonoObject *view = Reflect::Target(s_view);
  MonoObject *player = Reflect::Target(s_player);
  if (view && !HomeScene::IsDisposed(view)) {
    bool dispose_player = false;
    void *arguments[1] = {&dispose_player};
    call(view, "ReleasePlayer", 1, arguments);
    remove_widget(view);
  }
  /* already gone when its view went with the scene; nothing to report */
  invoke(player, Reflect::MethodOf(player, "Dispose", 0), nullptr);
  Reflect::Release(s_view);
  Reflect::Release(s_player);
  s_last_status = -1;
  s_hidden_checks = 0;
}

/* Back to nothing playing and nothing to go on from. */
void reset_playback() {
  teardown();
  s_failures = 0;
  s_current.clear();
  s_resume_at = 0;
  s_last_position = 0;
  s_state = State::Idle;
}

/* The player went with its scene: built anew in a moment, from where it was. */
void rebuild_soon() {
  s_resume_at = s_last_position;
  teardown();
  s_state = State::Waiting;
  s_retry_at = frame_count + 30;
}

/* The playing position, or 0 when the player does not say. */
uint64_t player_position(MonoObject *player) {
  MonoObject *value = nullptr;
  if (!call(player, "GetPosition", 0, nullptr, &value) || !value)
    return 0;
  return *static_cast<uint64_t *>(mono_object_unbox(value));
}

int32_t player_status(MonoObject *player) {
  MonoObject *value = nullptr;
  if (!call(player, "GetPlayerStatus", 0, nullptr, &value) || !value)
    return -1;
  return *static_cast<int32_t *>(mono_object_unbox(value));
}

/* The picture size in an MP4's first video sample entry (avc1, hvc1 or
   hev1), found through the top-level boxes to the movie header without
   reading the media itself. False when it cannot be told. */
bool mp4_video_size(const char *path, int *width, int *height) {
  const int fd = open(path, O_RDONLY);
  if (fd < 0)
    return false;
  bool found = false;
  off_t at = 0;
  for (int boxes = 0; boxes < 64 && !found; ++boxes) {
    unsigned char header[16];
    if (pread(fd, header, 8, at) != 8)
      break;
    uint64_t size = (static_cast<uint64_t>(header[0]) << 24) |
                    (static_cast<uint64_t>(header[1]) << 16) |
                    (static_cast<uint64_t>(header[2]) << 8) | header[3];
    off_t body = at + 8;
    if (size == 1) {
      if (pread(fd, header + 8, 8, at + 8) != 8)
        break;
      size = 0;
      for (int i = 8; i < 16; ++i)
        size = (size << 8) | header[i];
      body = at + 16;
    }
    if (size < 8)
      break;
    if (std::memcmp(header + 4, "moov", 4) == 0) {
      /* the movie header is small next to the media; cap it all the same */
      const size_t length = static_cast<size_t>(
          std::min<uint64_t>(size - static_cast<uint64_t>(body - at),
                             16u << 20));
      std::vector<unsigned char> moov(length);
      if (pread(fd, moov.data(), length, body) == static_cast<ssize_t>(length)) {
        for (size_t i = 0; i + 32 <= length && !found; ++i) {
          if (std::memcmp(&moov[i], "avc1", 4) != 0 &&
              std::memcmp(&moov[i], "hvc1", 4) != 0 &&
              std::memcmp(&moov[i], "hev1", 4) != 0)
            continue;
          /* type, then 6 reserved, data reference (2), 16 more, width, height */
          *width = (moov[i + 28] << 8) | moov[i + 29];
          *height = (moov[i + 30] << 8) | moov[i + 31];
          found = *width > 0 && *height > 0;
        }
      }
      break;
    }
    at += static_cast<off_t>(size);
  }
  close(fd);
  return found;
}

/* Sets an enum field of the player's creation options by member name. */
void set_option(MonoObject *options, MonoClass *player_class,
                const char *field_name, const char *type_name,
                const char *member) {
  MonoClassField *field = mono_class_get_field_from_name(
      mono_object_get_class(options), field_name);
  int32_t value = 0;
  if (field && Reflect::EnumValue(Reflect::NestedClass(player_class, type_name),
                                  member, &value))
    mono_field_set_value(options, field, &value);
  else
    LOG_WARN("[HOMEVIDEO] option %s=%s not set", field_name, member);
}

/* Silent by default, and then the system music keeps playing over it; with
   sound on, the console ducks its music as it does for any other video. A
   volume of 0 counts as sound off, so the music is not ducked for nothing.
   The player's volume runs to 100: React Native's video view drives the same
   player with a volume of 0 to 1, 1 by default, and the player reads back 0
   until something sets it. */
void apply_sound(MonoObject *player, bool sound) {
  int32_t volume = std::clamp(g_settings.home_video_volume, 0, 100);
  bool muted = !sound || volume == 0;
  void *arguments[1] = {&muted};
  call(player, "SetIsMuted", 1, arguments);
  call(player, "SetBGMSuppressionDisabled", 1, arguments);
  if (!muted) {
    void *volume_arguments[1] = {&volume};
    MonoObject *reported = nullptr;
    if (call(player, "SetVolume", 1, volume_arguments) &&
        call(player, "GetVolume", 0, nullptr, &reported) && reported)
      LOG_INFO("[HOMEVIDEO] volume %d, player reports %d", volume,
               *static_cast<int32_t *>(mono_object_unbox(reported)));
  }
  s_sound_applied = sound;
  s_volume_applied = volume;
}

bool build(MonoObject *root) {
  MonoImage *video_image = getDLLimage("Sce.Vsh.VideoPlayer.dll");
  MonoClass *player_class =
      video_image
          ? mono_class_from_name(video_image, "Sce.Vsh.Video", "VideoPlayer")
          : nullptr;
  MonoClass *view_class = ui3_class("VideoView");
  MonoClass *create_class =
      Reflect::NestedClass(player_class, "CreateOptionParam");
  MonoClass *play_class = Reflect::NestedClass(view_class, "PlayOptionParam");
  if (!create_class || !play_class) {
    LOG_ERROR("[HOMEVIDEO] classes player=%d view=%d create=%d play=%d",
              player_class ? 1 : 0, view_class ? 1 : 0, create_class ? 1 : 0,
              play_class ? 1 : 0);
    return false;
  }
  MonoMethod *player_ctor =
      overload_of(player_class, ".ctor", 1, "CreateOptionParam");
  MonoMethod *view_ctor = overload_of(view_class, ".ctor", 1, "PlayOptionParam");
  MonoClassField *uri_field =
      mono_class_get_field_from_name(create_class, "uri");
  if (!player_ctor || !view_ctor || !uri_field) {
    LOG_ERROR("[HOMEVIDEO] members player_ctor=%d view_ctor=%d uri=%d",
              player_ctor ? 1 : 0, view_ctor ? 1 : 0, uri_field ? 1 : 0);
    return false;
  }
  MonoClass *status_class =
      Reflect::NestedClass(player_class, "PlayerStateType");
  Reflect::EnumValue(status_class, "ErrorOccured", &s_status_error);
  Reflect::EnumValue(status_class, "Playing", &s_status_playing);
  Reflect::EnumValue(status_class, "PausedAtEnd", &s_status_at_end);
  Reflect::EnumValue(status_class, "Stopped", &s_status_stopped);
  Reflect::EnumValue(status_class, "Paused", &s_status_paused);

  /* Everything but the address is left as Sony's defaults. */
  char uri[96];
  std::snprintf(uri, sizeof(uri), "http://127.0.0.1:%d/home_bg.mp4",
                server_port());
  MonoObject *create = make(create_class);
  MonoObject *play = make(play_class);
  if (!create || !play)
    return false;
  mono_field_set_value(create, uri_field,
                       mono_string_new(mono_domain_get(), uri));
  /* Made empty, the options asked for a load-only player decoding into a
     1280x720 frame with the default decoder share, and the decoder refused
     anything above Full HD (a 3840x2160 file only ever reported an error).
     Ask for playback into a Full HD frame, and for the UHD decoder share
     when the file is larger than Full HD. */
  set_option(create, player_class, "createMode", "CreateMode",
             "CreateModeForPlayback");
  set_option(create, player_class, "frameBufferSize", "FrameBufferSizeType",
             "Size1920x1080");
  int video_width = 0, video_height = 0;
  const bool sized =
      mp4_video_size(s_current.c_str(), &video_width, &video_height);
  const bool uhd = sized && (video_width > 1920 || video_height > 1080);
  set_option(create, player_class, "coreResourceType", "CoreResourceType",
             uhd ? "UHD" : "FullHD");
  /* Paused while its scene is out of sight and going on when it is back,
     so the player can stay through a visit to Settings or a game instead of
     being built anew (which takes a moment, with Home's own background
     showing meanwhile). */
  set_option(create, player_class, "behaviorTypeOnInactive",
             "BehaviorTypeOnInactive", "Pause");
  set_option(create, player_class, "behaviorTypeOnActive",
             "BehaviorTypeOnActive", "Play");
  LOG_INFO("[HOMEVIDEO] video %dx%d%s; decoder share %s", video_width,
           video_height, sized ? "" : " (size unknown)", uhd ? "UHD" : "FullHD");

  MonoObject *player = mono_object_new(mono_domain_get(), player_class);
  if (!player)
    return false;
  s_player = mono_gchandle_new(player, /*pinned=*/1);
  void *create_arguments[1] = {create};
  bool start = true;
  void *init_arguments[1] = {&start};
  if (!invoke(player, player_ctor, create_arguments) ||
      !call(player, "Init", 1, init_arguments)) {
    LOG_ERROR("[HOMEVIDEO] the player could not be made");
    return false;
  }

  MonoObject *view = mono_object_new(mono_domain_get(), view_class);
  if (!view)
    return false;
  s_view = mono_gchandle_new(view, /*pinned=*/1);
  void *view_arguments[1] = {play};
  if (!invoke(view, view_ctor, view_arguments)) {
    LOG_ERROR("[HOMEVIDEO] the view could not be made");
    return false;
  }

  const ScreenSize size = size_of(root);
  /* fill the screen, cropping what does not fit */
  prepare_layer(view, size, kWidgetName, "AspectOutside");

  bool yes = true;
  void *set_player_arguments[2] = {player, &yes};
  void *yes_arguments[1] = {&yes};
  if (!call(view, "SetPlayer", 2, set_player_arguments))
    return false;
  call(player, "SetLoop", 1, yes_arguments);
  apply_sound(player, g_settings.home_video_sound);
  /* from where it was when Home went away, if it did */
  uint64_t position = s_resume_at;
  s_resume_at = 0;
  s_last_position = position;
  void *position_arguments[1] = {&position};
  if (!put_first(root, view) ||
      !(position ? call(view, "Play", 1, position_arguments)
                 : call(view, "Play", 0)))
    return false;
  LOG_INFO("[HOMEVIDEO] started uri=%s size=%.0fx%.0f at %llu", uri,
           size.width, size.height, static_cast<unsigned long long>(position));
  return true;
}

void fail(const char *why) {
  teardown();
  s_resume_at = 0; /* perhaps the position it failed at */
  s_last_position = 0;
  ++s_failures;
  LOG_ERROR("[HOMEVIDEO] %s (failure %d)", why, s_failures);
  /* it failed without taking ShellUI down */
  unlink(kPending);
  if (s_failures >= 5) {
    s_state = State::Failed;
    return;
  }
  s_state = State::Waiting;
  s_retry_at = frame_count + 300 * s_failures;
}

void watch(MonoObject *root) {
  MonoObject *view = Reflect::Target(s_view);
  MonoObject *player = Reflect::Target(s_player);
  if (!view || !player || HomeScene::IsDisposed(view)) {
    /* Home rebuilt its scene and took the widget with it */
    LOG_INFO("[HOMEVIDEO] the view is gone; building it again at %llu",
             static_cast<unsigned long long>(s_last_position));
    rebuild_soon();
    return;
  }
  if (object_property(view, "get_Parent") != root ||
      object_property(root, "get_FirstChild") != view)
    put_first(root, view);

  if (s_sound_applied != g_settings.home_video_sound ||
      s_volume_applied != std::clamp(g_settings.home_video_volume, 0, 100))
    apply_sound(player, g_settings.home_video_sound);

  const int32_t status = player_status(player);
  if (status != s_last_status) {
    LOG_INFO("[HOMEVIDEO] status %d -> %d requests=%d", s_last_status, status,
             server_requests());
    if (status == s_status_playing)
      raise_effects();
    s_last_status = status;
  }
  if (status >= 0 && status == s_status_error) {
    fail("the player reported an error");
    return;
  }

  if (status == s_status_playing)
    s_last_position = player_position(player);
  const bool shown = stage_wanted();
  /* Hidden (a game in front), it pauses with the stage and goes on when
     that shows again. Hidden for long, it is let go, as the decoder is a
     shared resource, and built anew from where it was when the stage shows. */
  if (shown && s_hidden_checks > 0)
    LOG_INFO("[HOMEVIDEO] in sight again (status %d)", status);
  s_hidden_checks = shown ? 0 : s_hidden_checks + 1;
  if (s_hidden_checks == 1)
    LOG_INFO("[HOMEVIDEO] out of sight at %llu (status %d)",
             static_cast<unsigned long long>(s_last_position), status);
  if (s_hidden_checks >= kReleaseAfterChecks) {
    s_resume_at = s_last_position;
    LOG_INFO("[HOMEVIDEO] out of sight for a while; player released at %llu",
             static_cast<unsigned long long>(s_resume_at));
    teardown();
    unlink(kPending); /* an interrupted start, not a failed one */
    s_state = State::Hidden;
    return;
  }
  /* A player that never gets as far as playing (a file its decoder will not
     take, a decoder another app holds, a stream it never asks for) has failed
     too, without an error, so the picture can show. */
  if (shown && s_played_checks >= 0 && s_status_playing >= 0 &&
      status != s_status_playing && ++s_start_checks >= 60) {
    char why[96];
    std::snprintf(why, sizeof(why),
                  "the player did not start playing in 30 s (status %d, "
                  "requests %d)",
                  status, server_requests());
    fail(why);
    return;
  }
  if (shown && status >= 0 &&
      (status == s_status_at_end || status == s_status_stopped)) {
    /* the loop flag should make this unreachable */
    uint64_t start = 0;
    void *arguments[1] = {&start};
    call(player, "Play", 1, arguments);
  }

  /* In sight but paused by the system: the user selection screen makes
     ShellUI's players inactive (BehaviorTypeOnInactive has no "go on").
     Play on from where it stopped, after a second, then every 4 s. */
  if (shown && status >= 0 && status == s_status_paused) {
    if (++s_paused_checks % 8 == 2) {
      uint64_t position = s_last_position;
      void *arguments[1] = {&position};
      LOG_INFO("[HOMEVIDEO] paused while in sight; playing on at %llu",
               static_cast<unsigned long long>(position));
      call(view, "Play", 1, arguments);
    }
  } else {
    s_paused_checks = 0;
  }

  if (status == s_status_playing && s_played_checks >= 0 &&
      ++s_played_checks >= 4) {
    unlink(kPending);
    s_played_checks = -1;
    s_failures = 0;
  }
}

std::string lowercase(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return text;
}

/* Dotfiles do not count, whatever their extension. */
bool has_extension(const char *name,
                   std::initializer_list<const char *> extensions) {
  if (!name || name[0] == '.')
    return false;
  const std::string lower = lowercase(name);
  for (const char *extension : extensions) {
    const size_t length = std::strlen(extension);
    if (lower.size() > length &&
        lower.compare(lower.size() - length, length, extension) == 0)
      return true;
  }
  return false;
}

void scan() {
  (void)mkdir(ONION_DIST_USER_DATA_ROOT "/ShellUI", 0777);
  (void)mkdir(kVideoDirectory, 0777);
  std::vector<std::string> found;
  scan_directory(kVideoDirectory, found, kVideoExtensions);
  for (int slot = 0; slot < kUsbSlots; ++slot) {
    char directory[96];
    std::snprintf(directory, sizeof(directory), kUsbVideoDirectory, slot);
    scan_directory(directory, found, kVideoExtensions);
  }
  sort_by_file_name(found);
  if (found != s_videos || !s_videos_scanned)
    LOG_INFO("[HOMEVIDEO] catalog '%s' videos=%zu", kVideoDirectory,
             found.size());
  s_videos = std::move(found);
  s_videos_scanned = true;
  s_scanned_frame = frame_count;
}

/* The file to play now, or nothing when the feature is off or has nothing
   to show. The chosen file wins; without one (or when it has gone, a USB
   drive pulled out for instance) the first one in the list is used. */
std::string wanted() {
  if (!(AniketosTheme::CustomizationOn() && g_settings.home_video_enabled) ||
      if_exists(kKillSwitch))
    return {};
  if (!g_settings.home_video_path.empty() &&
      if_exists(g_settings.home_video_path.c_str()))
    return g_settings.home_video_path;
  /* look for files now and then, so dropping one in is enough */
  if (!s_videos_scanned || frame_count - s_scanned_frame >= 300)
    scan();
  for (const std::string &path : s_videos)
    if (if_exists(path.c_str()))
      return path;
  return {};
}

/* Whether the video is on screen or on its way there. Not once it failed for
   good, was left off after a crash, or waits to retry after a failure: the
   picture shows instead. */
bool video_on_screen(const std::string &file) {
  if (file.empty())
    return false;
  switch (s_state) {
  case State::Failed:
  case State::Off:
    return false;
  case State::Waiting:
    return s_failures == 0;
  default:
    return true;
  }
}

void tick_playback() {
  if (!s_videos_scanned)
    scan(); /* also makes the folder, so there is a place to put files */

  const std::string file = wanted();
  tick_image(video_on_screen(file));
  if (file != s_current && s_state != State::Idle) {
    /* switched off, another file chosen, or the file has gone */
    LOG_INFO("[HOMEVIDEO] '%s' -> '%s'", s_current.c_str(), file.c_str());
    reset_playback();
  }
  if (file.empty())
    return;

  switch (s_state) {
  case State::Idle:
    if (pending_starts() >= 2) {
      LOG_ERROR("[HOMEVIDEO] the last start never finished; left off until "
                "it is switched on again in the Shell Menu");
      s_current = file;
      s_state = State::Off;
      return;
    }
    if (!start_server()) {
      LOG_ERROR("[HOMEVIDEO] the loopback server did not start");
      s_current = file;
      s_state = State::Off;
      return;
    }
    s_current = file;
    set_served(file);
    LOG_INFO("[HOMEVIDEO] serving '%s' on 127.0.0.1:%d", file.c_str(),
             server_port());
    s_state = State::Waiting;
    s_retry_at = frame_count;
    break;
  case State::Waiting: {
    MonoObject *root = stage();
    if (!root || frame_count < s_retry_at || !stage_wanted())
      return;
    mark_pending();
    if (build(root)) {
      s_state = State::Playing;
      s_played_checks = 0;
      s_start_checks = 0;
    } else {
      fail("the player could not be built");
    }
    break;
  }
  case State::Hidden:
    if (stage_wanted() && stage()) {
      LOG_INFO("[HOMEVIDEO] the stage shows again");
      s_state = State::Waiting;
      s_retry_at = frame_count;
    }
    break;
  case State::Playing:
    if (MonoObject *root = stage()) {
      watch(root);
    } else {
      /* the layer went away with the stage and the player in it */
      LOG_INFO("[HOMEVIDEO] the stage is gone; at %llu",
               static_cast<unsigned long long>(s_last_position));
      rebuild_soon();
    }
    break;
  default:
    break;
  }
}

} // namespace

void scan_directory(const char *directory, std::vector<std::string> &found,
                    std::initializer_list<const char *> extensions) {
  DIR *handle = opendir(directory);
  if (!handle)
    return;
  while (dirent *entry = readdir(handle)) {
    if (!has_extension(entry->d_name, extensions))
      continue;
    const std::string path = std::string(directory) + "/" + entry->d_name;
    struct stat info {};
    if (stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) &&
        info.st_size > 0)
      found.push_back(path);
  }
  closedir(handle);
}

void sort_by_file_name(std::vector<std::string> &files) {
  std::sort(files.begin(), files.end(),
            [](const std::string &left, const std::string &right) {
              return lowercase(AniketosUI::FileName(left)) <
                     lowercase(AniketosUI::FileName(right));
            });
}

const std::vector<std::string> &Videos(bool rescan) {
  if (rescan || !s_videos_scanned)
    scan();
  return s_videos;
}

void Restart() {
  reset_playback();
  s_played_checks = 0;
  s_start_checks = 0;
  unlink(kPending);
}

void Tick() {
  ++frame_count;
  tick_stage(); /* the fades run every frame */
  /* twice a second is plenty for everything below */
  if (frame_count % 30 != 0)
    return;
  tick_playback();
  MonoObject *base =
      s_state == State::Playing ? Reflect::Target(s_view) : nullptr;
  tick_effects(base ? base : picture_widget());
}

} // namespace HomeVideo
