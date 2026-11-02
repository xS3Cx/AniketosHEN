#pragma once

#include "external_symbols.hpp"

namespace AniketosShell {

struct HostSnapshot {
  MonoObject *scene = nullptr;
  MonoObject *root = nullptr;
  const char *path = nullptr;
  const char *scene_namespace = nullptr;
  const char *scene_type = nullptr;
};

class HostScene {
public:
  bool Initialize(MonoImage *app_system, MonoImage *pui);
  bool Resolve(HostSnapshot &out);

private:
  void ProbeCandidates();
  MonoObject *FindScene(const char *path, MonoObject **exception = nullptr);
  MonoObject *GetRoot(MonoObject *scene);
  bool ResolvePath(const char *path, HostSnapshot &out, bool log_result);

  MonoImage *app_system_ = nullptr;
  MonoImage *pui_ = nullptr;
  MonoClass *layer_manager_ = nullptr;
  MonoClass *scene_class_ = nullptr;
  MonoMethod *find_scene_ = nullptr;
  bool probed_ = false;
};

/* Who has the screen: LayerManager.s_curFocusLayer, read by name. Other is a
 * layer that is none of these (the Control Center's, say); Unknown means the
 * field or one of these layers was not found on this firmware. UI thread. */
enum class FocusLayer { Unknown, Game, TopMenu, ShellApp, Other };
FocusLayer CurrentFocusLayer();
/* Whether the game has the screen. Needs only the field and Game, so it still
 * answers on a firmware without TopMenu or ShellApp; with neither, true. UI
 * thread. */
bool GameLayerFocused();

} // namespace AniketosShell
