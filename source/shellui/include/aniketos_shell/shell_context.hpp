#pragma once

#include "external_symbols.hpp"

#include <cstdint>

namespace AniketosShell {

struct HostSnapshot {
  MonoObject *scene = nullptr;
  MonoObject *root = nullptr;
  const char *path = nullptr;
  const char *scene_namespace = nullptr;
  const char *scene_type = nullptr;
};

class ShellContext {
public:
  bool Initialize(MonoImage *app_system, MonoImage *pui);
  bool Resolve(HostSnapshot &out);
  void ProbeCandidates();

private:
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

} // namespace AniketosShell
