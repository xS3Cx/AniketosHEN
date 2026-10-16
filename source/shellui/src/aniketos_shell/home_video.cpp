#include "aniketos_shell/home_video.hpp"
#include "aniketos_shell/home_menu.hpp"
#include "aniketos_theme/customization.hpp"
#include "aniketos_ui/pui_primitives.hpp"

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "shell_clock.hpp"
#include "ps5/kernel.h"

#include <onion/distribution.h>
#include <onion/log.h>
#include <onion/settings.hpp>

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <initializer_list>
#include <netinet/in.h>
#include <pthread.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <vector>
#include <unistd.h>

extern int g_libmono_handle;
extern onion::Settings g_settings;

namespace HomeVideo {
std::string FileName(const std::string &path);
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
/* Still images for the same place, shown when no video is. The folder is the
   one the older background-image option used, so pictures already copied
   there are found. */
constexpr const char *kImageDirectory =
    ONION_DIST_USER_DATA_ROOT "/ShellUI/background";
constexpr std::initializer_list<const char *> kImageExtensions = {
    ".png", ".jpg", ".jpeg"};
constexpr const char *kImageWidgetName = "id_aniketos_home_image";
/* Never start it, whatever else is there. */
constexpr const char *kKillSwitch = ONION_DIST_USER_DATA_ROOT "/no_home_video";
/* Counts starts that never got as far as playing: written before the player
   is built and removed once it plays (see pending_starts()). */
constexpr const char *kPending = ONION_DIST_USER_DATA_ROOT "/home_video_pending";
constexpr const char *kWidgetName = "id_aniketos_home_video";

/* ---- loopback HTTP server for the one file --------------------------- */

std::atomic<int> s_port{0};
std::atomic<int> s_requests{0};
std::atomic<int> s_connections{0};
int s_listen_fd = -1;
/* The one file the server hands out; set by the UI thread. */
pthread_mutex_t s_served_lock = PTHREAD_MUTEX_INITIALIZER;
char s_served[512] = "";

void set_served(const std::string &path) {
  pthread_mutex_lock(&s_served_lock);
  std::snprintf(s_served, sizeof(s_served), "%s", path.c_str());
  pthread_mutex_unlock(&s_served_lock);
}

bool send_all(int fd, const char *data, size_t size) {
  while (size > 0) {
#ifdef MSG_NOSIGNAL
    const ssize_t sent = send(fd, data, size, MSG_NOSIGNAL);
#else
    const ssize_t sent = send(fd, data, size, 0);
#endif
    if (sent <= 0)
      return false;
    data += sent;
    size -= static_cast<size_t>(sent);
  }
  return true;
}

/* "bytes=first-last", "bytes=first-" or "bytes=-suffix" */
bool parse_range(const char *header, off_t size, off_t *first, off_t *last) {
  const char *value = std::strstr(header, "bytes=");
  if (!value)
    return false;
  value += 6;
  char *end = nullptr;
  if (*value == '-') {
    const long long suffix = std::strtoll(value + 1, &end, 10);
    if (suffix <= 0)
      return false;
    *first = suffix >= size ? 0 : size - suffix;
    *last = size - 1;
    return true;
  }
  const long long from = std::strtoll(value, &end, 10);
  if (!end || *end != '-' || from < 0 || from >= size)
    return false;
  *first = from;
  *last = size - 1;
  if (end[1] >= '0' && end[1] <= '9') {
    const long long to = std::strtoll(end + 1, nullptr, 10);
    if (to >= from && to < size)
      *last = to;
  }
  return true;
}

void serve(int client) {
  /* The request head; nothing past it matters. */
  char request[2048];
  size_t used = 0;
  while (used < sizeof(request) - 1) {
    const ssize_t got = recv(client, request + used,
                             sizeof(request) - 1 - used, 0);
    if (got <= 0)
      return;
    used += static_cast<size_t>(got);
    request[used] = '\0';
    if (std::strstr(request, "\r\n\r\n"))
      break;
  }
  request[used] = '\0';
  const bool head = !std::strncmp(request, "HEAD ", 5);
  if (!head && std::strncmp(request, "GET ", 4)) {
    static const char kBad[] =
        "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n"
        "Connection: close\r\n\r\n";
    send_all(client, kBad, sizeof(kBad) - 1);
    return;
  }
  ++s_requests;

  char served[sizeof(s_served)];
  pthread_mutex_lock(&s_served_lock);
  std::memcpy(served, s_served, sizeof(served));
  pthread_mutex_unlock(&s_served_lock);
  const int file = served[0] ? open(served, O_RDONLY) : -1;
  struct stat st {};
  if (file < 0 || fstat(file, &st) != 0 || st.st_size <= 0) {
    static const char kMissing[] =
        "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
        "Connection: close\r\n\r\n";
    send_all(client, kMissing, sizeof(kMissing) - 1);
    if (file >= 0)
      close(file);
    return;
  }

  off_t first = 0;
  off_t last = st.st_size - 1;
  bool partial = false;
  /* header names are case-insensitive */
  for (char *line = request; (line = std::strstr(line, "\r\n")) != nullptr;) {
    line += 2;
    if (!strncasecmp(line, "Range:", 6)) {
      char *line_end = std::strstr(line, "\r\n");
      if (line_end)
        *line_end = '\0';
      partial = parse_range(line + 6, st.st_size, &first, &last);
      if (line_end)
        *line_end = '\r';
      break;
    }
  }

  char reply[512];
  int length;
  if (partial)
    length = std::snprintf(
        reply, sizeof(reply),
        "HTTP/1.1 206 Partial Content\r\nContent-Type: video/mp4\r\n"
        "Accept-Ranges: bytes\r\nContent-Range: bytes %lld-%lld/%lld\r\n"
        "Content-Length: %lld\r\nConnection: close\r\n\r\n",
        static_cast<long long>(first), static_cast<long long>(last),
        static_cast<long long>(st.st_size),
        static_cast<long long>(last - first + 1));
  else
    length = std::snprintf(
        reply, sizeof(reply),
        "HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\n"
        "Accept-Ranges: bytes\r\nContent-Length: %lld\r\n"
        "Connection: close\r\n\r\n",
        static_cast<long long>(st.st_size));
  bool ok = length > 0 && send_all(client, reply, static_cast<size_t>(length));

  if (ok && !head && lseek(file, first, SEEK_SET) == first) {
    static constexpr size_t kChunk = 64 * 1024;
    char *buffer = static_cast<char *>(std::malloc(kChunk));
    off_t left = last - first + 1;
    while (buffer && ok && left > 0) {
      const size_t want =
          left > static_cast<off_t>(kChunk) ? kChunk : static_cast<size_t>(left);
      const ssize_t got = read(file, buffer, want);
      if (got <= 0)
        break;
      ok = send_all(client, buffer, static_cast<size_t>(got));
      left -= got;
    }
    std::free(buffer);
  }
  close(file);
}

void *client_thread(void *argument) {
  const int client = static_cast<int>(reinterpret_cast<intptr_t>(argument));
  serve(client);
  shutdown(client, SHUT_RDWR);
  close(client);
  --s_connections;
  return nullptr;
}

void *accept_thread(void *) {
  for (;;) {
    const int client = accept(s_listen_fd, nullptr, nullptr);
    if (client < 0) {
      usleep(100 * 1000);
      continue;
    }
#ifdef SO_NOSIGPIPE
    int on = 1;
    setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
    /* The player holds a stream open and seeks with a second request. */
    if (s_connections.load() >= 8) {
      close(client);
      continue;
    }
    ++s_connections;
    pthread_t thread{};
    if (pthread_create(&thread, nullptr, client_thread,
                       reinterpret_cast<void *>(
                           static_cast<intptr_t>(client))) != 0) {
      --s_connections;
      close(client);
      continue;
    }
    pthread_detach(thread);
  }
  return nullptr;
}

/* Bound to 127.0.0.1 on a port the system picks; serves nothing but the
   video file, whatever path is asked for. */
bool start_server() {
  if (s_port.load() > 0)
    return true;
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return false;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  socklen_t size = sizeof(address);
  if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
      listen(fd, 8) != 0 ||
      getsockname(fd, reinterpret_cast<sockaddr *>(&address), &size) != 0) {
    close(fd);
    return false;
  }
  s_listen_fd = fd;
  pthread_t thread{};
  if (pthread_create(&thread, nullptr, accept_thread, nullptr) != 0) {
    close(fd);
    s_listen_fd = -1;
    return false;
  }
  pthread_detach(thread);
  s_port = ntohs(address.sin_port);
  return true;
}

/* ---- managed side ---------------------------------------------------- */

bool exists(const char *path) {
  struct stat st {};
  return stat(path, &st) == 0;
}

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

MonoClass *nested(MonoClass *outer, const char *name) {
  static MonoClass *(*nested_types)(MonoClass *, void **) = nullptr;
  if (!nested_types)
    nested_types = reinterpret_cast<decltype(nested_types)>(
        kernel_dynlib_dlsym(-1, g_libmono_handle,
                            "mono_class_get_nested_types"));
  if (!outer || !nested_types)
    return nullptr;
  void *iter = nullptr;
  while (MonoClass *inner = nested_types(outer, &iter))
    if (!std::strcmp(mono_class_get_name(inner), name))
      return inner;
  return nullptr;
}

MonoMethod *method_of(MonoClass *klass, const char *name, int parameters) {
  for (; klass; klass = mono_class_get_parent(klass))
    if (MonoMethod *method =
            mono_class_get_method_from_name(klass, name, parameters))
      return method;
  return nullptr;
}

/* The overload whose parameter types mention `type_text`. */
MonoMethod *overload_of(MonoClass *klass, const char *name, int parameters,
                        const char *type_text) {
  void *iter = nullptr;
  while (MonoMethod *method = mono_class_get_methods(klass, &iter)) {
    if (std::strcmp(mono_method_get_name(method), name))
      continue;
    MonoMethodSignature *signature = mono_method_signature(method);
    if (!signature ||
        static_cast<int>(mono_signature_get_param_count(signature)) !=
            parameters)
      continue;
    void *param_iter = nullptr;
    while (MonoType *type = mono_signature_get_params(signature, &param_iter)) {
      char *text = mono_type_get_name(type);
      if (text && std::strstr(text, type_text))
        return method;
    }
  }
  return nullptr;
}

bool invoke(MonoObject *object, MonoMethod *method, void **arguments,
            MonoObject **result = nullptr) {
  if (!object || !method)
    return false;
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(method, object, arguments, &exception);
  if (result)
    *result = value;
  return !exception;
}

bool call(MonoObject *object, const char *name, int parameters,
          void **arguments = nullptr, MonoObject **result = nullptr) {
  if (!object)
    return false;
  MonoMethod *method =
      method_of(mono_object_get_class(object), name, parameters);
  if (!method) {
    LOG_ERROR("[HOMEVIDEO] no method %s/%d", name, parameters);
    return false;
  }
  if (!invoke(object, method, arguments, result)) {
    LOG_ERROR("[HOMEVIDEO] %s threw", name);
    return false;
  }
  return true;
}

MonoObject *object_property(MonoObject *object, const char *getter) {
  MonoObject *value = nullptr;
  MonoMethod *method =
      object ? method_of(mono_object_get_class(object), getter, 0) : nullptr;
  return method && invoke(object, method, nullptr, &value) ? value : nullptr;
}

bool float_property(MonoObject *object, const char *getter, float *out) {
  MonoObject *value = nullptr;
  MonoMethod *method =
      object ? method_of(mono_object_get_class(object), getter, 0) : nullptr;
  if (!method || !invoke(object, method, nullptr, &value) || !value)
    return false;
  *out = *static_cast<float *>(mono_object_unbox(value));
  return true;
}

/* An object with a parameterless constructor. */
MonoObject *make(MonoClass *klass) {
  if (!klass || !mono_class_get_method_from_name(klass, ".ctor", 0))
    return nullptr;
  MonoObject *object = mono_object_new(mono_domain_get(), klass);
  if (object)
    mono_runtime_object_init(object);
  return object;
}

/* The value of an enum member, by its name. */
bool enum_value(MonoClass *type, const char *member, int32_t *value) {
  MonoClassField *field =
      type ? mono_class_get_field_from_name(type, member) : nullptr;
  if (!field)
    return false;
  mono_field_static_get_value(mono_class_vtable(mono_domain_get(), type), field,
                              value);
  return true;
}

enum class State { Idle, Waiting, Playing, Failed, Off, Hidden };
State s_state = State::Idle;
uint32_t s_player = 0; /* strong handles */
uint32_t s_view = 0;
int s_frame = 0;
int s_retry_at = 0;
int s_failures = 0;
int s_played_checks = 0;
int s_hidden_checks = 0;
int32_t s_last_status = -1;
std::string s_current;      /* the file being played */
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

MonoObject *target(uint32_t handle) {
  return handle ? mono_gchandle_get_target(handle) : nullptr;
}

void release(uint32_t &handle) {
  if (handle)
    mono_gchandle_free(handle);
  handle = 0;
}

void teardown() {
  MonoObject *view = target(s_view);
  MonoObject *player = target(s_player);
  if (view && !ShellClock::IsWidgetDisposed(view)) {
    bool dispose_player = false;
    void *arguments[1] = {&dispose_player};
    call(view, "ReleasePlayer", 1, arguments);
    call(view, "RemoveFromParent", 0);
    call(view, "Dispose", 0);
  }
  if (player) {
    /* already gone when its view went with the scene; nothing to report */
    MonoMethod *dispose =
        method_of(mono_object_get_class(player), "Dispose", 0);
    invoke(player, dispose, nullptr);
  }
  release(s_view);
  release(s_player);
  s_last_status = -1;
  s_hidden_checks = 0;
}

int32_t player_status(MonoObject *player) {
  MonoObject *value = nullptr;
  if (!call(player, "GetPlayerStatus", 0, nullptr, &value) || !value)
    return -1;
  return *static_cast<int32_t *>(mono_object_unbox(value));
}

/* The video goes under everything else in Home's root. */
bool put_first(MonoObject *root, MonoObject *view) {
  MonoObject *first = object_property(root, "get_FirstChild");
  if (first == view)
    return true;
  if (object_property(view, "get_Parent"))
    call(view, "RemoveFromParent", 0);
  first = object_property(root, "get_FirstChild");
  if (!first)
    return call(root, "AppendChild", 1,
                reinterpret_cast<void **>(&view));
  void *arguments[2] = {view, first};
  return call(root, "InsertChildBelow", 2, arguments);
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
  if (field && enum_value(nested(player_class, type_name), member, &value))
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
  MonoImage *pui = getDLLimage("Sce.PlayStation.PUI.dll");
  MonoClass *player_class =
      video_image
          ? mono_class_from_name(video_image, "Sce.Vsh.Video", "VideoPlayer")
          : nullptr;
  MonoClass *view_class =
      pui ? mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "VideoView")
          : nullptr;
  MonoClass *create_class = nested(player_class, "CreateOptionParam");
  MonoClass *play_class = nested(view_class, "PlayOptionParam");
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
  MonoClass *status_class = nested(player_class, "PlayerStateType");
  enum_value(status_class, "ErrorOccured", &s_status_error);
  enum_value(status_class, "Playing", &s_status_playing);
  enum_value(status_class, "PausedAtEnd", &s_status_at_end);
  enum_value(status_class, "Stopped", &s_status_stopped);

  /* Everything but the address is left as Sony's defaults. */
  char uri[96];
  std::snprintf(uri, sizeof(uri), "http://127.0.0.1:%d/home_bg.mp4",
                s_port.load());
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
  const bool sized = mp4_video_size(s_served, &video_width, &video_height);
  const bool uhd = sized && (video_width > 1920 || video_height > 1080);
  set_option(create, player_class, "coreResourceType", "CoreResourceType",
             uhd ? "UHD" : "FullHD");
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

  float width = 1920.0f;
  float height = 1080.0f;
  float_property(root, "get_Width", &width);
  float_property(root, "get_Height", &height);
  void *width_arguments[1] = {&width};
  void *height_arguments[1] = {&height};
  call(view, "set_Width", 1, width_arguments);
  call(view, "set_Height", 1, height_arguments);
  void *name_arguments[1] = {mono_string_new(mono_domain_get(), kWidgetName)};
  call(view, "set_Name", 1, name_arguments);
  /* fill the screen, cropping what does not fit */
  int32_t cover = 0;
  if (enum_value(mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3",
                                      "ImageScaleType"),
                 "AspectOutside", &cover)) {
    void *scale_arguments[1] = {&cover};
    call(view, "set_ImageScaleType", 1, scale_arguments);
  }
  bool no = false;
  void *focus_arguments[1] = {&no};
  if (method_of(view_class, "set_Focusable", 1))
    call(view, "set_Focusable", 1, focus_arguments);

  bool yes = true;
  void *set_player_arguments[2] = {player, &yes};
  void *yes_arguments[1] = {&yes};
  if (!call(view, "SetPlayer", 2, set_player_arguments))
    return false;
  call(player, "SetLoop", 1, yes_arguments);
  apply_sound(player, g_settings.home_video_sound);
  if (!put_first(root, view) || !call(view, "Play", 0))
    return false;
  LOG_INFO("[HOMEVIDEO] started uri=%s size=%.0fx%.0f", uri, width, height);
  return true;
}

void fail(const char *why) {
  teardown();
  ++s_failures;
  LOG_ERROR("[HOMEVIDEO] %s (failure %d)", why, s_failures);
  /* it failed without taking ShellUI down */
  unlink(kPending);
  if (s_failures >= 5) {
    s_state = State::Failed;
    return;
  }
  s_state = State::Waiting;
  s_retry_at = s_frame + 300 * s_failures;
}

/* Home's scene fades out when something else takes the screen. */
bool home_shown(MonoObject *root) {
  float opacity = 1.0f;
  return !float_property(root, "get_FinalOpacity", &opacity) ||
         opacity > 0.05f;
}

void watch(MonoObject *root) {
  MonoObject *view = target(s_view);
  MonoObject *player = target(s_player);
  if (!view || !player || ShellClock::IsWidgetDisposed(view)) {
    /* Home rebuilt its scene and took the widget with it */
    LOG_INFO("[HOMEVIDEO] the view is gone; building it again");
    teardown();
    s_state = State::Waiting;
    s_retry_at = s_frame + 30;
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
             s_requests.load());
    s_last_status = status;
  }
  if (status >= 0 && status == s_status_error) {
    fail("the player reported an error");
    return;
  }

  /* Not while Home is out of sight. The decoder is a shared resource, and a
     view that sat through its scene being hidden (Settings, a game) comes
     back playing but without a picture; so the player is dropped when Home
     goes away and built anew when it returns, which is the start-up path. */
  const bool shown = home_shown(root);
  s_hidden_checks = shown ? 0 : s_hidden_checks + 1;
  if (!shown && s_hidden_checks >= 3) {
    LOG_INFO("[HOMEVIDEO] Home is hidden; player released");
    teardown();
    unlink(kPending); /* an interrupted start, not a failed one */
    s_state = State::Hidden;
    return;
  }
  if (shown && status >= 0 &&
             (status == s_status_at_end || status == s_status_stopped)) {
    /* the loop flag should make this unreachable */
    uint64_t start = 0;
    void *arguments[1] = {&start};
    call(player, "Play", 1, arguments);
  }

  if (status == s_status_playing && s_played_checks >= 0 &&
      ++s_played_checks >= 4) {
    unlink(kPending);
    s_played_checks = -1;
    s_failures = 0;
  }
}

bool has_extension(const char *name,
                   std::initializer_list<const char *> extensions) {
  if (!name || name[0] == '.')
    return false;
  std::string lower(name);
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  for (const char *extension : extensions) {
    const size_t length = std::strlen(extension);
    if (lower.size() > length &&
        lower.compare(lower.size() - length, length, extension) == 0)
      return true;
  }
  return false;
}

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
  const auto folded = [](std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) {
                     return static_cast<char>(std::tolower(ch));
                   });
    return text;
  };
  std::sort(found.begin(), found.end(),
            [&](const std::string &left, const std::string &right) {
              return folded(FileName(left)) < folded(FileName(right));
            });
  if (found != s_videos || !s_videos_scanned)
    LOG_INFO("[HOMEVIDEO] catalog '%s' videos=%zu", kVideoDirectory,
             found.size());
  s_videos = std::move(found);
  s_videos_scanned = true;
  s_scanned_frame = s_frame;
}

/* The file to play now, or nothing when the feature is off or has nothing
   to show. The chosen file wins; without one (or when it has gone, a USB
   drive pulled out for instance) the first one in the list is used. */
std::string wanted() {
  if (!(AniketosTheme::CustomizationOn() && g_settings.home_video_enabled) || exists(kKillSwitch))
    return {};
  if (!g_settings.home_video_path.empty() &&
      exists(g_settings.home_video_path.c_str()))
    return g_settings.home_video_path;
  /* look for files now and then, so dropping one in is enough */
  if (!s_videos_scanned || s_frame - s_scanned_frame >= 300)
    scan();
  for (const std::string &path : s_videos)
    if (exists(path.c_str()))
      return path;
  return {};
}

/* ---- a still image ----------------------------------------------------
 * The same place as the video (under Home's React tree, in Home's scene),
 * with a PUI ImageBox instead of a player. Shown when no video is. */
std::vector<std::string> s_images;
bool s_images_scanned = false;
int s_images_scanned_frame = 0;
std::string s_image_current; /* the file on screen */
uint32_t s_image_widget = 0; /* strong handle */

void scan_images() {
  (void)mkdir(ONION_DIST_USER_DATA_ROOT "/ShellUI", 0777);
  (void)mkdir(kImageDirectory, 0777);
  std::vector<std::string> found;
  scan_directory(kImageDirectory, found, kImageExtensions);
  const auto folded = [](std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) {
                     return static_cast<char>(std::tolower(ch));
                   });
    return text;
  };
  std::sort(found.begin(), found.end(),
            [&](const std::string &left, const std::string &right) {
              return folded(FileName(left)) < folded(FileName(right));
            });
  if (found != s_images || !s_images_scanned)
    LOG_INFO("[HOMEIMAGE] catalog '%s' images=%zu", kImageDirectory,
             found.size());
  s_images = std::move(found);
  s_images_scanned = true;
  s_images_scanned_frame = s_frame;
}

/* The picture to show now, or nothing. */
std::string wanted_image(bool video_shown) {
  if (!(AniketosTheme::CustomizationOn() && g_settings.home_image_enabled) || video_shown || exists(kKillSwitch))
    return {};
  if (!g_settings.home_image_path.empty() &&
      exists(g_settings.home_image_path.c_str()))
    return g_settings.home_image_path;
  if (!s_images_scanned || s_frame - s_images_scanned_frame >= 300)
    scan_images();
  for (const std::string &path : s_images)
    if (exists(path.c_str()))
      return path;
  return {};
}

void drop_image() {
  MonoObject *widget = target(s_image_widget);
  if (widget && !ShellClock::IsWidgetDisposed(widget)) {
    call(widget, "RemoveFromParent", 0);
    call(widget, "Dispose", 0);
  }
  release(s_image_widget);
  s_image_current.clear();
}

/* A PUI picture of the file at `path`, or null. */
MonoObject *image_asset(const char *path) {
  MonoImage *pui = getDLLimage("Sce.PlayStation.PUI.dll");
  MonoClass *asset_class =
      pui ? mono_class_from_name(pui, "Sce.PlayStation.PUI", "ImageAsset")
          : nullptr;
  MonoMethod *asset_ctor =
      asset_class ? overload_of(asset_class, ".ctor", 1, "String") : nullptr;
  MonoObject *asset =
      asset_ctor ? mono_object_new(mono_domain_get(), asset_class) : nullptr;
  if (!asset) {
    LOG_ERROR("[HOMEIMAGE] no ImageAsset class=%d ctor=%d",
              asset_class ? 1 : 0, asset_ctor ? 1 : 0);
    return nullptr;
  }
  void *asset_arguments[1] = {mono_string_new(mono_domain_get(), path)};
  if (!invoke(asset, asset_ctor, asset_arguments)) {
    LOG_ERROR("[HOMEIMAGE] '%s' could not be opened", path);
    return nullptr;
  }
  return asset;
}

bool build_image(MonoObject *root, const std::string &path) {
  MonoImage *pui = getDLLimage("Sce.PlayStation.PUI.dll");
  MonoClass *box_class =
      pui ? mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "ImageBox")
          : nullptr;
  MonoObject *box = make(box_class);
  if (!box) {
    LOG_ERROR("[HOMEIMAGE] no ImageBox class");
    return false;
  }
  s_image_widget = mono_gchandle_new(box, /*pinned=*/1);
  MonoObject *asset = image_asset(path.c_str());
  if (!asset)
    return false;
  float width = 1920.0f;
  float height = 1080.0f;
  float_property(root, "get_Width", &width);
  float_property(root, "get_Height", &height);
  void *width_arguments[1] = {&width};
  void *height_arguments[1] = {&height};
  void *name_arguments[1] = {
      mono_string_new(mono_domain_get(), kImageWidgetName)};
  void *image_arguments[1] = {asset};
  call(box, "set_Width", 1, width_arguments);
  call(box, "set_Height", 1, height_arguments);
  call(box, "set_Name", 1, name_arguments);
  /* fill the screen, cropping what does not fit */
  int32_t cover = 0;
  if (enum_value(mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3",
                                      "ImageScaleType"),
                 "AspectOutside", &cover)) {
    void *scale_arguments[1] = {&cover};
    call(box, "set_ImageScaleType", 1, scale_arguments);
  }
  bool no = false;
  void *focus_arguments[1] = {&no};
  if (method_of(box_class, "set_Focusable", 1))
    call(box, "set_Focusable", 1, focus_arguments);
  if (!call(box, "set_Image", 1, image_arguments) || !put_first(root, box))
    return false;
  LOG_INFO("[HOMEIMAGE] showing '%s' %.0fx%.0f", path.c_str(), width, height);
  return true;
}

/* Twice a second: the picture on screen is the one wanted, in Home's root,
   under everything else. */
void tick_image(bool video_shown) {
  if (!s_images_scanned)
    scan_images(); /* also makes the folder */
  const std::string file = wanted_image(video_shown);
  MonoObject *widget = target(s_image_widget);
  const bool gone = widget && ShellClock::IsWidgetDisposed(widget);
  if (file != s_image_current || gone) {
    drop_image();
    if (file.empty())
      return;
    MonoObject *root = ShellClock::GetHomeRoot();
    if (!root || ShellClock::IsWidgetDisposed(root))
      return;
    /* remembered even when it fails, so a bad file is not retried forever */
    const bool shown = build_image(root, file);
    s_image_current = file;
    if (!shown) {
      MonoObject *failed = target(s_image_widget);
      if (failed) {
        call(failed, "RemoveFromParent", 0);
        call(failed, "Dispose", 0);
      }
      release(s_image_widget);
    }
    return;
  }
  if (!widget)
    return;
  MonoObject *root = ShellClock::GetHomeRoot();
  if (root && !ShellClock::IsWidgetDisposed(root) &&
      (object_property(widget, "get_Parent") != root ||
       object_property(root, "get_FirstChild") != widget))
    put_first(root, widget);
}


/* ---- effects over the video or picture --------------------------------
 * Full-screen layers right above the video or picture and under the rest of
 * Home (the particles, then Home's React tree): blur, a PUI
 * BackgroundBlurEffect on a clear panel, which blurs whatever lies beneath
 * it; dimming and haze, black panels (the panels are counted as ours, or the
 * system-panel theme would paint them its colour); a vignette and shading at
 * the top and bottom, embedded black gradients stretched over the screen.
 * Each follows its Shell Menu slider live, from 0 (no layer at all) to 100. */
enum Effect { kBlur, kDim, kHaze, kVignette, kEdges, kEffectCount };
constexpr const char *kEffectNames[kEffectCount] = {"blur", "dim", "haze",
                                                    "vignette", "edges"};
/* What each gets at 100. */
constexpr float kBlurRadius = 40.0f;
constexpr float kDimOpacity = 0.85f;
constexpr float kHazeOpacity = 0.5f;

struct UIColor {
  float r, g, b, a;
  int32_t color_space;
};
static_assert(sizeof(UIColor) == 20);

struct EffectLayer {
  uint32_t widget = 0; /* strong handle */
  int applied = -1;    /* the strength on screen */
  int failed = -1;     /* a strength the layer could not be made at */
};
EffectLayer s_effect_layers[kEffectCount];

int effect_strength(int effect) {
  int strength = 0;
  switch (effect) {
  case kBlur: strength = g_settings.home_bg_blur; break;
  case kDim: strength = g_settings.home_bg_dim; break;
  case kHaze: strength = g_settings.home_bg_haze; break;
  case kVignette: strength = g_settings.home_bg_vignette; break;
  case kEdges: strength = g_settings.home_bg_edges; break;
  default: break;
  }
  return std::clamp(strength, 0, 100);
}

void drop_effect(int effect) {
  EffectLayer &layer = s_effect_layers[effect];
  MonoObject *widget = target(layer.widget);
  if (widget && !ShellClock::IsWidgetDisposed(widget)) {
    if (effect == kBlur) {
      /* the blur holds offscreen textures; they go with it */
      MonoObject *blur = object_property(widget, "get_PostEffect");
      void *none[1] = {nullptr};
      call(widget, "set_PostEffect", 1, none);
      if (blur)
        call(blur, "Dispose", 0);
    }
    call(widget, "RemoveFromParent", 0);
    call(widget, "Dispose", 0);
  }
  AniketosUI::Pui::Disown(widget);
  release(layer.widget);
  layer.applied = -1;
}

MonoObject *build_effect(MonoObject *root, int effect) {
  MonoImage *pui = getDLLimage("Sce.PlayStation.PUI.dll");
  const bool picture = effect == kVignette || effect == kEdges;
  MonoClass *klass =
      pui ? mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3",
                                 picture ? "ImageBox" : "Panel")
          : nullptr;
  MonoObject *widget = make(klass);
  if (!widget)
    return nullptr;
  s_effect_layers[effect].widget = mono_gchandle_new(widget, /*pinned=*/1);
  float width = 1920.0f;
  float height = 1080.0f;
  float_property(root, "get_Width", &width);
  float_property(root, "get_Height", &height);
  void *width_arguments[1] = {&width};
  void *height_arguments[1] = {&height};
  char name[48];
  std::snprintf(name, sizeof(name), "id_aniketos_home_%s",
                kEffectNames[effect]);
  void *name_arguments[1] = {mono_string_new(mono_domain_get(), name)};
  call(widget, "set_Width", 1, width_arguments);
  call(widget, "set_Height", 1, height_arguments);
  call(widget, "set_Name", 1, name_arguments);
  bool no = false;
  void *focus_arguments[1] = {&no};
  if (method_of(klass, "set_Focusable", 1))
    call(widget, "set_Focusable", 1, focus_arguments);

  if (picture) {
    const char *path = HomeEffectImagePath(effect == kVignette
                                               ? HomeEffectImage::Vignette
                                               : HomeEffectImage::Edges);
    MonoObject *asset = path && path[0] ? image_asset(path) : nullptr;
    void *image_arguments[1] = {asset};
    if (!asset || !call(widget, "set_Image", 1, image_arguments))
      return nullptr;
    /* stretched over the whole screen, whatever its shape */
    int32_t stretch = 0;
    if (enum_value(mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3",
                                        "ImageScaleType"),
                   "Stretch", &stretch)) {
      void *scale_arguments[1] = {&stretch};
      call(widget, "set_ImageScaleType", 1, scale_arguments);
    }
    return widget;
  }

  AniketosUI::Pui::Adopt(widget);
  UIColor color{0.0f, 0.0f, 0.0f, effect == kBlur ? 0.0f : 1.0f, 0};
  void *color_arguments[1] = {&color};
  if (!call(widget, "set_BackgroundColor", 1, color_arguments))
    return nullptr;
  if (effect == kBlur) {
    MonoObject *blur = make(mono_class_from_name(
        pui, "Sce.PlayStation.PUI.UI3", "BackgroundBlurEffect"));
    void *blur_arguments[1] = {blur};
    if (!blur || !call(widget, "set_PostEffect", 1, blur_arguments))
      return nullptr;
  }
  return widget;
}

void apply_effect(MonoObject *widget, int effect, int strength) {
  const float amount = strength / 100.0f;
  if (effect == kBlur) {
    float radius = amount * kBlurRadius;
    void *radius_arguments[1] = {&radius};
    call(object_property(widget, "get_PostEffect"), "set_Radius", 1,
         radius_arguments);
    return;
  }
  float opacity = amount * (effect == kDim    ? kDimOpacity
                            : effect == kHaze ? kHazeOpacity
                                              : 1.0f);
  void *opacity_arguments[1] = {&opacity};
  call(widget, "set_Opacity", 1, opacity_arguments);
}

/* `widget` right above `below` in Home's root. */
void place_above(MonoObject *root, MonoObject *below, MonoObject *widget) {
  if (object_property(widget, "get_Parent") == root &&
      object_property(below, "get_NextSibling") == widget)
    return;
  if (object_property(widget, "get_Parent"))
    call(widget, "RemoveFromParent", 0);
  MonoObject *next = object_property(below, "get_NextSibling");
  if (!next) {
    call(root, "AppendChild", 1, reinterpret_cast<void **>(&widget));
    return;
  }
  void *arguments[2] = {widget, next};
  call(root, "InsertChildBelow", 2, arguments);
}

/* Twice a second: a layer for each effect turned up, in order right above
   `base`, the video or the picture; none without one. */
void tick_effects(MonoObject *base) {
  MonoObject *root = ShellClock::GetHomeRoot();
  const bool shown = base && root && !ShellClock::IsWidgetDisposed(root) &&
                     !ShellClock::IsWidgetDisposed(base) &&
                     object_property(base, "get_Parent") == root;
  MonoObject *below = base;
  for (int effect = 0; effect < kEffectCount; ++effect) {
    EffectLayer &layer = s_effect_layers[effect];
    const int strength = shown ? effect_strength(effect) : 0;
    MonoObject *widget = target(layer.widget);
    if (widget && ShellClock::IsWidgetDisposed(widget)) {
      /* went with Home's scene */
      AniketosUI::Pui::Disown(widget);
      release(layer.widget);
      layer.applied = -1;
      widget = nullptr;
    }
    if (strength == 0) {
      if (layer.widget)
        drop_effect(effect);
      layer.failed = -1;
      continue;
    }
    if (strength == layer.failed)
      continue;
    if (!widget) {
      widget = build_effect(root, effect);
      if (!widget) {
        LOG_ERROR("[HOMEFX] the %s layer could not be made",
                  kEffectNames[effect]);
        drop_effect(effect);
        layer.failed = strength;
        continue;
      }
    }
    if (layer.applied != strength) {
      apply_effect(widget, effect, strength);
      LOG_INFO("[HOMEFX] %s %d%%", kEffectNames[effect], strength);
      layer.applied = strength;
    }
    place_above(root, below, widget);
    below = widget;
  }
}

} // namespace

std::string FileName(const std::string &path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

const char *Directory() { return kVideoDirectory; }

const std::vector<std::string> &Videos(bool rescan) {
  if (rescan || !s_videos_scanned)
    scan();
  return s_videos;
}

std::string Current() { return s_current; }

const char *ImageDirectory() { return kImageDirectory; }

const std::vector<std::string> &Images(bool rescan) {
  if (rescan || !s_images_scanned)
    scan_images();
  return s_images;
}

void RestartImage() { drop_image(); }

void Restart() {
  teardown();
  s_failures = 0;
  s_played_checks = 0;
  s_current.clear();
  unlink(kPending);
  s_state = State::Idle;
}

std::string Status() {
  char text[400];
  MonoObject *player = target(s_player);
  std::snprintf(text, sizeof(text),
                "state=%d port=%d requests=%d connections=%d failures=%d "
                "status=%d hidden=%d enabled=%d sound=%d volume=%d "
                "videos=%zu file='%s'",
                static_cast<int>(s_state), s_port.load(), s_requests.load(),
                s_connections.load(), s_failures,
                player ? player_status(player) : -1,
                s_state == State::Hidden ? 1 : 0,
                (AniketosTheme::CustomizationOn() && g_settings.home_video_enabled) ? 1 : 0,
                g_settings.home_video_sound ? 1 : 0,
                g_settings.home_video_volume, s_videos.size(),
                s_current.c_str());
  return text;
}

namespace {

void tick_playback() {
  if (!s_videos_scanned)
    scan(); /* also makes the folder, so there is a place to put files */

  const std::string file = wanted();
  tick_image(!file.empty());
  if (file != s_current && s_state != State::Idle) {
    /* switched off, another file chosen, or the file has gone */
    LOG_INFO("[HOMEVIDEO] '%s' -> '%s'", s_current.c_str(), file.c_str());
    teardown();
    s_failures = 0;
    s_current.clear();
    s_state = State::Idle;
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
             s_port.load());
    s_state = State::Waiting;
    s_retry_at = s_frame;
    break;
  case State::Waiting: {
    MonoObject *root = ShellClock::GetHomeRoot();
    if (!root || s_frame < s_retry_at || ShellClock::IsWidgetDisposed(root) ||
        !home_shown(root))
      return;
    mark_pending();
    if (build(root)) {
      s_state = State::Playing;
      s_played_checks = 0;
    } else {
      fail("the player could not be built");
    }
    break;
  }
  case State::Hidden: {
    MonoObject *root = ShellClock::GetHomeRoot();
    if (root && !ShellClock::IsWidgetDisposed(root) && home_shown(root)) {
      LOG_INFO("[HOMEVIDEO] Home is back");
      s_state = State::Waiting;
      s_retry_at = s_frame;
    }
    break;
  }
  case State::Playing: {
    MonoObject *root = ShellClock::GetHomeRoot();
    if (root && !ShellClock::IsWidgetDisposed(root))
      watch(root);
    break;
  }
  default:
    break;
  }
}

} // namespace

void Tick() {
  ++s_frame;
  /* twice a second is plenty for everything below */
  if (s_frame % 30 != 0)
    return;
  tick_playback();
  MonoObject *base = s_state == State::Playing ? target(s_view) : nullptr;
  tick_effects(base ? base : target(s_image_widget));
}

} // namespace HomeVideo
