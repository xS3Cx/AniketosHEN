Option option_row(const char *title, OptionType type,
                  OptionId id = OptionId::None) {
  Option option{};
  option.title = title;
  option.type = type;
  option.id = id;
  return option;
}

/* The row that opens a page carries that page's title. */
Option submenu(Page target) {
  Option option = option_row(page_title(target), OptionType::Submenu);
  option.target = target;
  return option;
}

Option toggle(const char *title, OptionId id, bool value) {
  Option option = option_row(title, OptionType::Toggle, id);
  option.value = value ? "1" : "0";
  return option;
}

Option value(const char *title, std::string text) {
  Option option = option_row(title, OptionType::Value);
  option.value = std::move(text);
  return option;
}

Option action(const char *title, OptionId id) {
  return option_row(title, OptionType::Action, id);
}

Option slider_int(const char *title, OptionId id, int current, int minimum,
                  int maximum, int step) {
  Option option = option_row(title, OptionType::SliderInt, id);
  option.value = std::to_string(current);
  option.minimum_i = minimum;
  option.maximum_i = maximum;
  option.step_i = step;
  return option;
}

Option animation_speed_slider(OptionId id, int current) {
  return slider_int(tr("vsh.common.animation_speed"), id, current, 25, 400, 25);
}

Option slider_float(const char *title, OptionId id, float current,
                    float minimum, float maximum, float step, int precision) {
  Option option = option_row(title, OptionType::SliderFloat, id);
  option.value = number(current, precision);
  option.minimum_f = minimum;
  option.maximum_f = maximum;
  option.step_f = step;
  return option;
}

Option strings(const char *title, OptionId id, const char *current, int count) {
  Option option = option_row(title, OptionType::Strings, id);
  option.value = current;
  option.string_count = count;
  option.string_index = selector_index_for(id);
  return option;
}

/* One of a list of names; a setting outside the list shows the nearest. */
template <size_t Count>
Option choice(const char *title, OptionId id,
              const char *const (&names)[Count], int chosen) {
  return strings(title, id,
                 names[std::clamp(chosen, 0, static_cast<int>(Count) - 1)],
                 static_cast<int>(Count));
}

/* One of the user's files, the first when the saved path is gone, or a line
   saying the folder is empty. */
Option file_choice(const char *title, OptionId id,
                   const std::vector<std::string> &files,
                   const std::string &chosen, const char *no_files) {
  if (files.empty())
    return value(title, no_files);
  const auto index = static_cast<size_t>(std::max(0, index_of(files, chosen)));
  const std::string name = AniketosUI::FileName(files[index]);
  return strings(title, id, name.c_str(), static_cast<int>(files.size()));
}

/* The system's and this menu's colour animation are one setting. */
Option color_animation_choice(const char *title, OptionId id) {
  const char *modes[] = {
      tr("vsh.animation.static"), tr("vsh.animation.rainbow_solid"),
      tr("vsh.animation.rainbow_gradient"), tr("vsh.animation.pulse"),
      tr("vsh.animation.ocean")};
  return choice(title, id, modes, g_settings.vsh_color_animation);
}

const char *theme_display_name(int theme) {
  switch (theme) {
  case 2: return "Anomaly";
  case 5: return "Apparition";
  case kThemeSweety: return "Sweety";
  default: return "Akari";
  }
}

// Persisted theme ids 1, 3 and 4 belonged to styles that were removed. They
// stay reserved so an old config cannot silently turn into a different one.
constexpr int kThemeChoices[] = {0, 2, 5, kThemeSweety};
constexpr int kThemeChoiceCount = static_cast<int>(std::size(kThemeChoices));

int theme_choice_index(int theme) {
  for (int i = 0; i < kThemeChoiceCount; ++i)
    if (kThemeChoices[i] == theme)
      return i;
  return 0;
}

int cycle_theme(int current, int direction) {
  const int step = direction < 0 ? kThemeChoiceCount - 1 : 1;
  const int index = theme_choice_index(current) + step;
  return kThemeChoices[index % kThemeChoiceCount];
}

/* The colour gradient slider. Its positions are hues five degrees apart
   (0..71) and white at the end (kColorWhite); a colour is shown at the
   position nearest to it. */
constexpr int kColorWhite = 72;

AniketosTheme::RGB rgb_of(int r, int g, int b) {
  return {r / 255.0f, g / 255.0f, b / 255.0f};
}

/* The colour a slider edits, as it is now. */
AniketosTheme::RGB color_of(OptionId id) {
  switch (id) {
  case OptionId::ColorGlobal:
  case OptionId::ColorMenuLeft:
    return {g_settings.vsh_left_r, g_settings.vsh_left_g, g_settings.vsh_left_b};
  case OptionId::ColorMenuRight:
    return {g_settings.vsh_right_r, g_settings.vsh_right_g,
            g_settings.vsh_right_b};
  case OptionId::ColorController:
    return rgb_of(controller_get_custom_channel(false, 0),
                  controller_get_custom_channel(false, 1),
                  controller_get_custom_channel(false, 2));
  case OptionId::ColorController2:
    return rgb_of(controller_get_custom_channel(true, 0),
                  controller_get_custom_channel(true, 1),
                  controller_get_custom_channel(true, 2));
  case OptionId::ColorBackground:
    return rgb_of(g_settings.shell_background_base_r,
                  g_settings.shell_background_base_g,
                  g_settings.shell_background_base_b);
  case OptionId::ColorGlow:
    return rgb_of(g_settings.shell_background_glow_r,
                  g_settings.shell_background_glow_g,
                  g_settings.shell_background_glow_b);
  case OptionId::ColorOutline:
    return rgb_of(g_settings.shell_tiles_outline_r,
                  g_settings.shell_tiles_outline_g,
                  g_settings.shell_tiles_outline_b);
  case OptionId::ColorPanels:
    return rgb_of(g_settings.shell_ui_panels_r, g_settings.shell_ui_panels_g,
                  g_settings.shell_ui_panels_b);
  case OptionId::ColorIcon:
    return rgb_of(g_settings.system_icon_color_r,
                  g_settings.system_icon_color_g,
                  g_settings.system_icon_color_b);
  case OptionId::ColorClockTime:
    return rgb_of(g_settings.shell_clock_time_r, g_settings.shell_clock_time_g,
                  g_settings.shell_clock_time_b);
  case OptionId::ColorClockDate:
    return rgb_of(g_settings.shell_clock_date_r, g_settings.shell_clock_date_g,
                  g_settings.shell_clock_date_b);
  case OptionId::ColorLogo:
    return rgb_of(g_settings.logo_color_r, g_settings.logo_color_g,
                  g_settings.logo_color_b);
  default:
    return {1.0f, 1.0f, 1.0f};
  }
}

/* Which part of each colour its slider moves now; not saved. */
ColorMode s_color_modes[static_cast<int>(OptionId::ColorMenuRight) -
                        static_cast<int>(OptionId::ColorGlobal) + 1]{};

ColorMode &color_mode(OptionId id) {
  const int index = std::clamp(
      static_cast<int>(id) - static_cast<int>(OptionId::ColorGlobal), 0,
      static_cast<int>(std::size(s_color_modes)) - 1);
  return s_color_modes[index];
}

int color_position(const AniketosTheme::RGB &rgb, ColorMode mode) {
  const AniketosTheme::HSV hsv = AniketosTheme::RGBToHSV(rgb);
  switch (mode) {
  case ColorMode::Brightness:
    return static_cast<int>(std::lround(hsv.v * kColorLevels));
  case ColorMode::Saturation:
    return static_cast<int>(std::lround(hsv.s * kColorLevels));
  default:
    if (hsv.s < 0.15f)
      return kColorWhite;
    return static_cast<int>(std::lround(hsv.h / 5.0f)) % kColorWhite;
  }
}

/* The colour a slider position makes of the current one. Moving the hue
   keeps its brightness and saturation (a grey takes full saturation, so
   the hue shows); the white end of the hue track takes the saturation out. */
AniketosTheme::RGB color_with(const AniketosTheme::RGB &current,
                              ColorMode mode, int position) {
  AniketosTheme::HSV hsv = AniketosTheme::RGBToHSV(current);
  switch (mode) {
  case ColorMode::Brightness:
    hsv.v = static_cast<float>(position) / kColorLevels;
    break;
  case ColorMode::Saturation:
    hsv.s = static_cast<float>(position) / kColorLevels;
    break;
  default:
    if (position >= kColorWhite) {
      hsv.s = 0.0f;
    } else {
      hsv.h = static_cast<float>(position * 5);
      if (hsv.s < 0.15f)
        hsv.s = 1.0f;
    }
    break;
  }
  return AniketosTheme::HSVToRGB(hsv);
}

Option color_slider(const char *title, OptionId id) {
  Option option = option_row(title, OptionType::ColorSlider, id);
  const ColorMode mode = color_mode(id);
  option.value = std::to_string(color_position(color_of(id), mode));
  option.minimum_i = 0;
  option.maximum_i = mode == ColorMode::Hue ? kColorWhite : kColorLevels;
  option.step_i = 1;
  /* the mode, shown as "[n/3]" */
  option.string_index = static_cast<int>(mode);
  option.string_count = static_cast<int>(ColorMode::Count);
  return option;
}
