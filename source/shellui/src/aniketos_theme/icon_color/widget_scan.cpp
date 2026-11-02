#include "icon_color_internal.hpp"

#include "aniketos_ui/home_scene.hpp"

#include <onion/log.h>

namespace AniketosTheme::IconColor {
namespace {

namespace HomeScene = AniketosUI::HomeScene;

constexpr size_t kMaxMenuNodes = 64;
constexpr int kMaxMenuDepth = 6;
constexpr size_t kMaxSceneNodes = 1200;
constexpr int kMaxSceneDepth = 40;

struct SceneScanState {
  size_t nodes = 0;
  size_t images = 0;
  size_t applied = 0;
  std::vector<MonoObject *> visited;
};

bool s_scene_scan_active = false;

/* The next sibling is read first: `visit` can run managed code that changes
   the tree. */
template <typename Visit>
void visit_siblings(MonoObject *first, const size_t &nodes, size_t max_nodes,
                    Visit visit) {
  MonoObject *current = first;
  while (current && nodes < max_nodes) {
    if (object_disposed(current))
      break;
    MonoObject *next = HomeScene::PropertyObject(current, "NextSibling");
    visit(current);
    current = next;
  }
}

void scan_menu_node(MonoObject *node, size_t &nodes, int depth) {
  if (!node || depth > kMaxMenuDepth || nodes >= kMaxMenuNodes ||
      object_disposed(node))
    return;
  ++nodes;
  if (is_image_box_descendant(mono_object_get_class(node)))
    tint_image(node);
  visit_siblings(HomeScene::PropertyObject(node, "FirstChild"), nodes,
                 kMaxMenuNodes, [&](MonoObject *child) {
                   scan_menu_node(child, nodes, depth + 1);
                 });
}

bool scene_seen(SceneScanState &state, MonoObject *node) {
  for (MonoObject *visited : state.visited) {
    if (visited == node)
      return true;
  }
  if (state.visited.size() >= kMaxSceneNodes)
    return true;
  state.visited.push_back(node);
  return false;
}

void scan_scene_node(MonoObject *node, SceneScanState &state, int depth) {
  if (!node || depth > kMaxSceneDepth || state.nodes >= kMaxSceneNodes ||
      object_disposed(node) || scene_seen(state, node))
    return;
  ++state.nodes;
  if (is_image_box_descendant(mono_object_get_class(node))) {
    ++state.images;
    if (widget_should_tint(node) && tint_image(node))
      ++state.applied;
  }
  visit_siblings(HomeScene::PropertyObject(node, "FirstChild"), state.nodes,
                 kMaxSceneNodes, [&](MonoObject *child) {
                   scan_scene_node(child, state, depth + 1);
                 });
}

void scan_scene_root(MonoObject *root, void *context) {
  SceneScanState &state = *static_cast<SceneScanState *>(context);
  visit_siblings(root, state.nodes, kMaxSceneNodes, [&](MonoObject *node) {
    scan_scene_node(node, state, 0);
  });
}

} // namespace

void scan_menu_icons(MonoObject *row) {
  size_t nodes = 0;
  scan_menu_node(row, nodes, 0);
}

void run_scene_scan() {
  if (s_scene_scan_active || !feature_on() || !s_image_filter.setter)
    return;
  s_scene_scan_active = true;
  SceneScanState state;
  HomeScene::VisitSceneRoots(&scan_scene_root, &state);
  s_scene_scan_active = false;
  LOG_INFO("[ICONCOLOR] scene scan nodes=%zu images=%zu applied=%zu",
           state.nodes, state.images, state.applied);
}

} // namespace AniketosTheme::IconColor
