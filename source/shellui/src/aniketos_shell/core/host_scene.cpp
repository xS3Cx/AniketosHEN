#include "aniketos_shell/core/host_scene.hpp"

#include "hooked_funcs.hpp"

#include <onion/platform.h>

namespace AniketosShell {
namespace {

struct Candidate {
  const char *path;
  bool selectable;
};

/* Layer order observed in ShellUI lifecycle logs places SystemOverlay above
 * Game, PSMenu, SystemFG, and Notification. Only the persistent top layer and
 * the legacy Game layer are selectable until runtime evidence promotes another
 * candidate; every candidate is still probed read-only. */
constexpr Candidate kCandidates[] = {
    {"SystemOverlay", true}, {"Notification", false},
    {"SystemFG", false},     {"PSMenu", false},
    {"Impose", false},       {"Game", true},
};

MonoDomain *calling_domain() {
  MonoDomain *domain = mono_domain_get ? mono_domain_get() : nullptr;
  return domain ? domain : Root_Domain;
}

} // namespace

bool ShellContext::Initialize(MonoImage *app_system, MonoImage *pui) {
  app_system_ = app_system;
  pui_ = pui;
  if (!app_system_ || !pui_ || !Root_Domain) {
    LOG_ERROR("[ANIVSH:HOST] initialize missing app_system=%p pui=%p domain=%p",
              static_cast<void *>(app_system_), static_cast<void *>(pui_),
              static_cast<void *>(Root_Domain));
    return false;
  }

  layer_manager_ = mono_class_from_name(
      app_system_, "Sce.Vsh.ShellUI.AppSystem", "LayerManager");
  scene_class_ =
      mono_class_from_name(pui_, "Sce.PlayStation.PUI.UI2", "Scene");
  find_scene_ = layer_manager_
                    ? mono_class_get_method_from_name(
                          layer_manager_, "FindContainerSceneByPath", 1)
                    : nullptr;
  if (!layer_manager_ || !scene_class_ || !find_scene_) {
    LOG_ERROR("[ANIVSH:HOST] metadata missing layer=%p scene=%p find=%p",
              static_cast<void *>(layer_manager_),
              static_cast<void *>(scene_class_), static_cast<void *>(find_scene_));
    return false;
  }
  LOG_INFO("[ANIVSH:HOST] resolver ready app_system=%p pui_img=%p domain=%p",
           static_cast<void *>(app_system_), static_cast<void *>(pui_),
           static_cast<void *>(Root_Domain));
  return true;
}

MonoObject *ShellContext::FindScene(const char *path, MonoObject **exception) {
  if (!path || !find_scene_ || !mono_runtime_invoke || !mono_string_new)
    return nullptr;
  MonoDomain *domain = calling_domain();
  if (!domain)
    return nullptr;
  MonoString *path_arg = mono_string_new(domain, path);
  void *args[1] = {path_arg};
  MonoObject *local_exception = nullptr;
  MonoObject *scene =
      mono_runtime_invoke(find_scene_, nullptr, args, &local_exception);
  if (exception)
    *exception = local_exception;
  return local_exception ? nullptr : scene;
}

MonoObject *ShellContext::GetRoot(MonoObject *scene) {
  return scene && scene_class_
             ? Get_Property<MonoObject *>(scene_class_, scene, "RootWidget")
             : nullptr;
}

bool ShellContext::ResolvePath(const char *path, HostSnapshot &out,
                               bool log_result) {
  MonoObject *exception = nullptr;
  MonoObject *scene = FindScene(path, &exception);
  MonoObject *root = scene ? GetRoot(scene) : nullptr;
  MonoClass *actual =
      scene && mono_object_get_class ? mono_object_get_class(scene) : nullptr;
  const char *type = actual && mono_class_get_name
                         ? mono_class_get_name(actual)
                         : "unknown";
  const char *name_space = actual && mono_class_get_namespace
                               ? mono_class_get_namespace(actual)
                               : "unknown";

  if (log_result) {
    LOG_INFO("[ANIVSH:HOST] probe path=%s scene=%p root=%p type=%s.%s exc=%p",
             path, static_cast<void *>(scene), static_cast<void *>(root),
             name_space ? name_space : "", type ? type : "",
             static_cast<void *>(exception));
  }
  if (!scene || !root)
    return false;

  out.scene = scene;
  out.root = root;
  out.path = path;
  out.scene_namespace = name_space;
  out.scene_type = type;
  return true;
}

void ShellContext::ProbeCandidates() {
  if (probed_)
    return;
  probed_ = true;
  LOG_INFO("[ANIVSH:HOST] candidate probe begin");
  for (const Candidate &candidate : kCandidates) {
    HostSnapshot ignored{};
    (void)ResolvePath(candidate.path, ignored, true);
  }
  LOG_INFO("[ANIVSH:HOST] candidate probe end");
}

bool ShellContext::Resolve(HostSnapshot &out) {
  ProbeCandidates();
  for (const Candidate &candidate : kCandidates) {
    if (candidate.selectable && ResolvePath(candidate.path, out, false))
      return true;
  }
  out = {};
  return false;
}

} // namespace AniketosShell
