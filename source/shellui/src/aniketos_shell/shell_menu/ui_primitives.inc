const char *tr(const char *key);
const char *page_title(Page page);

MonoObject *create_pinned_font(int size, int weight, uint32_t &handle) {
  MonoObject *font = CreateUIFont(size, 0, weight);
  if (!font || !mono_gchandle_new)
    return nullptr;
  handle = mono_gchandle_new(font, /*pinned=*/1);
  return handle ? font : nullptr;
}

bool retain_pui_widget(MonoObject *object, void *) {
  if (!object || !mono_gchandle_new)
    return false;
  const uint32_t handle = mono_gchandle_new(object, /*pinned=*/1);
  if (!handle) {
    LOG_ERROR("[ANIVSH:MENU] failed to pin managed widget=%p",
              static_cast<void *>(object));
    return false;
  }
  s_widget_handles.push_back(handle);
  return true;
}

bool light_menu_appearance() {
  return g_settings.vsh_menu_appearance == 1;
}

/* Secondary text (hints and other notes beside an option): light on the
   dark menu, dark on the light one, so it stays readable in both. Use it
   for any such text added later. Button glyphs keep their own look. */
Color hint_text_color() {
  return light_menu_appearance() ? Color{0.16f, 0.17f, 0.19f, 0.80f}
                                 : Color{1.0f, 1.0f, 1.0f, 0.55f};
}

/* The picture on each colour slider's track, so that it loads only on a change. */
std::string s_color_track_image[kMaxVisibleOptions];

void release_widgets() {
  if (mono_gchandle_free) {
    for (uint32_t handle : s_widget_handles) {
      AniketosUI::Pui::Disown(AniketosUI::Reflect::Target(handle));
      mono_gchandle_free(handle);
    }
  }
  s_widget_handles.clear();
  AniketosUI::Pui::ForgetAll();
  s_submenu_marker_active.fill(false);
  /* new widgets start on their default picture */
  for (std::string &image : s_color_track_image)
    image.clear();
}

/* The Font Weight setting (Light, Regular, Bold) as a Core.Imaging.FontWeight.
   libScePsm opens SST-Light for Light (2), SST-Roman for UI3Medium (6) and
   SST-Bold for UI3Bold (5); any value it does not know, such as a CSS 400,
   falls back to SST-Light. */
int menu_font_weight() {
  constexpr int kWeights[] = {2, 6, 5};
  return kWeights[std::clamp(g_settings.vsh_font_weight, 0, 2)];
}

void replace_font(int size) {
  const int weight = menu_font_weight();
  if (s_font && s_font_handle && s_font_size == size &&
      s_font_weight == weight)
    return;
  if (s_font_handle && mono_gchandle_free)
    mono_gchandle_free(s_font_handle);
  s_font_handle = 0;
  s_font = CreateUIFont(size, 0, weight);
  if (s_font && mono_gchandle_new) {
    s_font_handle = mono_gchandle_new(s_font, /*pinned=*/1);
    if (!s_font_handle) {
      LOG_ERROR("[ANIVSH:MENU] failed to pin font=%p",
                static_cast<void *>(s_font));
      s_font = nullptr;
    }
  }
  s_font_size = s_font ? size : 0;
  s_font_weight = s_font ? weight : 0;
  s_pui.font = s_font;
}

/* The real class of a widget. Nearly everything here is UI2, but the widgets
 * that live in Home's scene are UI3; properties have the same names on both
 * and must be looked up on the right one. */
MonoClass *cls(MonoObject *object) {
  return object && mono_object_get_class ? mono_object_get_class(object)
                                         : nullptr;
}

const AniketosUI::Pui::Runtime &pui_for(MonoObject *object) {
  MonoClass *klass = cls(object);
  if (!klass || klass == s_pui.label || klass == s_pui.panel ||
      klass == s_pui.image_box)
    return s_pui;
  static AniketosUI::Pui::Runtime other;
  other = s_pui;
  other.label = klass;
  other.panel = klass;
  other.image_box = klass;
  return other;
}

void remove_named(MonoObject *root, const char *name) {
  AniketosUI::Pui::RemoveNamed(s_pui, root, name);
}

void set_color(MonoClass *klass, MonoObject *object, const char *property,
               Color color) {
  AniketosUI::Pui::SetColor(klass, object, property, color);
}

void panel_geometry(MonoObject *panel, float x, float y, float width,
                    float height) {
  AniketosUI::Pui::PanelGeometry(pui_for(panel), panel, x, y, width, height);
}

void panel_color(MonoObject *panel, Color color) {
  if (light_menu_appearance()) {
    /* the surfaces keep the alpha they were given: the Background Opacity */
    if (panel == s_widgets.header || panel == s_widgets.footer) {
      color = {1.0f, 1.0f, 1.0f, color.a};
    } else if (panel == s_widgets.body) {
      color = {0.84f, 0.85f, 0.87f, color.a};
    } else if (panel == s_widgets.highlight ||
               std::find(std::begin(s_widgets.highlight_slices),
                         std::end(s_widgets.highlight_slices),
                         panel) != std::end(s_widgets.highlight_slices)) {
      color = {1.0f, 1.0f, 1.0f, 1.0f};
    }
  }
  AniketosUI::Pui::PanelColor(pui_for(panel), panel, color);
}

void panel_hide(MonoObject *panel) {
  AniketosUI::Pui::PanelHide(pui_for(panel), panel);
}

void collapse_panel(MonoObject *panel) {
  panel_geometry(panel, 0.0f, 0.0f, 0.0f, 0.0f);
  panel_hide(panel);
}

void label_angle(MonoObject *label, float angle) {
  if (!label || !s_ui_node_class || !s_transform_class)
    return;
  MonoObject *transform =
      Get_Property<MonoObject *>(s_ui_node_class, label, "Transform");
  if (transform)
    Set_Property(s_transform_class, transform, "AngleZ", angle);
}

MonoObject *create_panel(MonoObject *parent, const char *name) {
  return AniketosUI::Pui::CreatePanel(s_pui, parent, name);
}

MonoObject *create_label(MonoObject *parent, const char *name, int horizontal,
                         int lines = 1) {
  s_pui.font = s_font;
  return AniketosUI::Pui::CreateLabel(s_pui, parent, name, horizontal, lines);
}

MonoObject *create_system_image(MonoObject *parent, const char *name,
                                const char *uri) {
  return AniketosUI::Pui::CreateSystemImage(s_pui, parent, name, uri);
}

void label_geometry(MonoObject *label, float x, float y, float width,
                    float height) {
  AniketosUI::Pui::LabelGeometry(pui_for(label), label, x, y, width, height);
}

void image_geometry(MonoObject *image, float x, float y, float width,
                    float height, bool visible = true) {
  AniketosUI::Pui::ImageGeometry(pui_for(image), image, x, y, width, height, visible);
}

void collapse_image(MonoObject *image) {
  image_geometry(image, 0.0f, 0.0f, 0.0f, 0.0f, false);
}

void label_text(MonoObject *label, const std::string &text) {
  AniketosUI::Pui::LabelText(pui_for(label), label, text);
}

void clear_label(MonoObject *label) {
  label_text(label, "");
  label_geometry(label, 0.0f, 0.0f, 0.0f, 0.0f);
}

void label_font(MonoObject *label) {
  s_pui.font = s_font;
  AniketosUI::Pui::LabelFont(pui_for(label), label);
}

/* Every label on one option row takes the menu's font. */
void option_row_fonts(size_t index) {
  label_font(s_widgets.rows[index]);
  label_font(s_widgets.values[index]);
  label_font(s_widgets.selector_left[index]);
  label_font(s_widgets.selector_right[index]);
}

void value_label_geometry(MonoObject *label, float x, float y, float width,
                          float height) {
  if (!label || !s_label_class)
    return;
  Set_Property(cls(label), label, "Opacity", 1.0f);
  Set_Property(cls(label), label, "VerticalAlignment", 2);
  Set_Property(cls(label), label, "HorizontalAlignment", 2);
  label_angle(label, 0.0f);
  label_geometry(label, x, y, width, height);
}

std::string selector_display_value(const Option &option) {
  if (option.string_count <= 0)
    return option.value;
  const int current = std::clamp(option.string_index, 0,
                                 option.string_count - 1);
  char position[32];
  std::snprintf(position, sizeof(position), " [%d/%d]", current + 1,
                option.string_count);
  // Keep the complete selector in one native PUI2 label.  Separately placed
  // labels have inconsistent text metrics between ShellUI firmware builds,
  // which repeatedly detached the left marker from its value.
  return std::string("< ") + option.value + position + " >";
}

void hide_selector_marker(MonoObject *label) {
  if (!label)
    return;
  clear_label(label);
  Set_Property(cls(label), label, "Opacity", 0.0f);
  label_angle(label, 0.0f);
}

void clear_selector_markers(size_t visible_index) {
  if (visible_index >= kMaxVisibleOptions)
    return;
  if (!s_submenu_marker_active[visible_index])
    return;
  hide_selector_marker(s_widgets.selector_left[visible_index]);
  hide_selector_marker(s_widgets.selector_right[visible_index]);
  s_submenu_marker_active[visible_index] = false;
}

void update_submenu_marker(size_t visible_index, float x, float y, float size,
                           float angle, Color color) {
  if (visible_index >= kMaxVisibleOptions)
    return;
  MonoObject *marker = s_widgets.selector_right[visible_index];
  if (!marker)
    return;
  if (!s_submenu_marker_active[visible_index]) {
    label_text(marker, "∴");
    Set_Property(s_label_class, marker, "HorizontalAlignment", 1);
    Set_Property(s_label_class, marker, "VerticalAlignment", 2);
    Set_Property(s_label_class, marker, "Opacity", 1.0f);
    s_submenu_marker_active[visible_index] = true;
  }
  label_geometry(marker, x, y, size, size);
  label_angle(marker, angle);
  set_color(s_label_class, marker, "TextColor", color);
}

#include "menu_look.inc"
#include "text_metrics.inc"
