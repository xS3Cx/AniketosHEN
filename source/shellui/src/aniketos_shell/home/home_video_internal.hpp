#pragma once

#include "aniketos_ui/home_scene.hpp"
#include "aniketos_ui/mono_reflect.hpp"

#include <onion/distribution.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

/* What the parts of the background behind Home share. All of it runs on the UI
   thread, the server's own threads aside. */
namespace HomeVideo {

namespace HomeScene = AniketosUI::HomeScene;
namespace Reflect = AniketosUI::Reflect;

/* Never start the video or the picture, whatever else is there. */
inline constexpr const char *kKillSwitch =
    ONION_DIST_USER_DATA_ROOT "/no_home_video";

/* Frames since the start; Tick counts them. */
extern int frame_count;

/* home_video_server.cpp */
bool start_server();
void set_served(const std::string &path);
int server_port();
int server_requests();

/* home_video.cpp */
void scan_directory(const char *directory, std::vector<std::string> &found,
                    std::initializer_list<const char *> extensions);
void sort_by_file_name(std::vector<std::string> &files);

/* home_stage.cpp: managed calls */
MonoMethod *overload_of(MonoClass *klass, const char *name, int parameters,
                        const char *type_text);
bool invoke(MonoObject *object, MonoMethod *method, void **arguments,
            MonoObject **result = nullptr);
bool call(MonoObject *object, const char *name, int parameters,
          void **arguments = nullptr, MonoObject **result = nullptr);
MonoObject *object_property(MonoObject *object, const char *getter);
MonoObject *make(MonoClass *klass);

/* home_stage.cpp: full-screen layers */
struct ScreenSize {
  float width, height;
};
MonoClass *ui3_class(const char *name);
ScreenSize size_of(MonoObject *root, ScreenSize preset = {1920.0f, 1080.0f});
bool prepare_layer(MonoObject *widget, ScreenSize size, const char *name,
                   const char *scale = nullptr);
void set_scale(MonoObject *widget, const char *member);
bool put_first(MonoObject *root, MonoObject *widget);
void remove_widget(MonoObject *widget);

/* home_stage.cpp: the stage */
MonoObject *stage();
void tick_stage();
bool stage_wanted();

/* home_picture.cpp */
MonoObject *image_asset(const char *path);
void tick_image(bool video_shown);
MonoObject *picture_widget();

/* home_effects.cpp */
void tick_effects(MonoObject *base);
void raise_effects();

} // namespace HomeVideo
