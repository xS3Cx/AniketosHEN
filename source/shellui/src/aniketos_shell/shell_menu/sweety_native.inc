/* Sweety: the Shell Menu drawn by the console itself.
 *
 * The menu's pages, options and actions are the same as ever (build_page,
 * activate, change_option); only the way a page is put on screen differs.
 * A page is one of PUI's own pop-up menus, filled with its own rows: a button
 * for a sub-page, an action or a value, a switch for a toggle, a slider for a
 * number, a drop-down for a choice. The card, its rows, the focus frame and
 * its movement, scrolling, sounds and the controller are all PUI's; this side
 * says what the rows are, where the card goes, and acts on what was pressed.
 * When PUI's pop-up menu cannot be used, the menu is drawn as Akari. */
namespace SweetyNative {

/* The card: to the right of a point 50 px from the left edge, vertically
   centred, as wide as the Control Center's cards; PUI's own look. */
constexpr float kMargin = 50.0f;
constexpr float kCardWidth = 652.0f;

bool s_active = false;
/* PUI's menu did not open once: Akari draws the menu until the next start,
   rather than trying again (and closing) on every frame. */
bool s_failed = false;
std::vector<Option> s_shown; /* the options behind the rows on screen */
/* The scene that had the focus when the menu was called up. Every page is
   opened on it: once a page is up, the scene with the focus is that page's
   own, which goes away with it. */
uint32_t s_base_scene = 0;

MonoObject *base_scene() {
  return AniketosUI::Reflect::Target(s_base_scene);
}

void forget_base_scene() {
  if (s_base_scene)
    mono_gchandle_free(s_base_scene);
  s_base_scene = 0;
}

bool Wanted() {
  return active_menu_theme() == kThemeSweety && !s_failed &&
         SystemDialogs::PopupMenuAvailable();
}

float slider_number(const Option &option) {
  return option.type == OptionType::SliderInt
             ? static_cast<float>(int_target(option.id))
             : float_target(option.id);
}

float slider_step(const Option &option) {
  if (option.type == OptionType::SliderInt)
    return static_cast<float>(std::max(1, option.step_i));
  return option.step_f > 0.0f ? option.step_f : 0.1f;
}

void Hide() {
  if (!s_active)
    return;
  SystemDialogs::CloseNativeMenu();
  s_active = false;
  forget_base_scene();
  s_armed_option = OptionId::None;
  request_save();
}

/* The names of everything a choice option can be set to, for a drop-down
   list. The page model only says what is chosen now and how many there are,
   so each one is tried in turn and the page asked what it would show; the
   setting is put back as it was. Only for options whose choice is a plain
   number in the settings (the test: setting that number moves the choice);
   the others stay buttons that step to the next one. */
bool choice_names(Page page, size_t row, const Option &option,
                  std::vector<std::string> &names, int &chosen) {
  if (option.type != OptionType::Strings || option.string_count < 2 ||
      option.string_count > 40)
    return false;
  int &number = int_target(option.id);
  const int original = number;
  bool plain = true;
  for (int probe = 0; probe < 2 && plain; ++probe) {
    number = probe;
    plain = selector_index_for(option.id) == probe;
  }
  if (plain) {
    for (int index = 0; index < option.string_count && plain; ++index) {
      number = index;
      const std::vector<Option> probed = build_page(page);
      plain = row < probed.size() && probed[row].id == option.id;
      if (plain)
        names.push_back(probed[row].value);
    }
  }
  number = original;
  chosen = std::clamp(original, 0, option.string_count - 1);
  if (!plain)
    names.clear();
  return plain;
}

bool open_page(int focus) {
  if (s_stack.empty())
    s_stack.push_back(PageState{});
  const Page page = s_stack.back().page;
  s_shown = build_page(page);
  std::vector<SystemDialogs::NativeMenuItem> rows;
  rows.reserve(s_shown.size());
  for (const Option &option : s_shown) {
    SystemDialogs::NativeMenuItem row;
    row.title = option.title;
    switch (option.type) {
    case OptionType::Toggle:
      row.kind = SystemDialogs::NativeMenuItem::Kind::Switch;
      row.on = option.value == "1";
      break;
    case OptionType::SliderInt:
      row.kind = SystemDialogs::NativeMenuItem::Kind::Slider;
      row.minimum = static_cast<float>(option.minimum_i);
      row.maximum = static_cast<float>(option.maximum_i);
      row.step = slider_step(option);
      row.number = slider_number(option);
      break;
    case OptionType::SliderFloat:
      row.kind = SystemDialogs::NativeMenuItem::Kind::Slider;
      row.minimum = option.minimum_f;
      row.maximum = option.maximum_f;
      row.step = slider_step(option);
      row.number = slider_number(option);
      break;
    case OptionType::Submenu:
      row.value = "\xE2\x80\xBA"; /* a chevron: there is more behind it */
      break;
    case OptionType::Value:
      row.value = option.value;
      row.enabled = false; /* shown, not pressed */
      break;
    case OptionType::Strings:
      if (choice_names(page, rows.size(), option, row.choices, row.chosen))
        row.kind = SystemDialogs::NativeMenuItem::Kind::Choice;
      else
        row.value = option.value; /* pressed to step to the next one */
      break;
    default:
      row.value = option.value;
      break;
    }
    rows.push_back(std::move(row));
  }
  if (rows.empty()) {
    SystemDialogs::NativeMenuItem row;
    row.title = "-";
    row.enabled = false;
    rows.push_back(row);
  }
  SystemDialogs::NativeMenuPlacement placement;
  placement.left = true;
  placement.margin = std::round(kMargin * s_viewport_height / 1080.0f);
  placement.min_width = std::round(kCardWidth * s_viewport_height / 1080.0f);
  std::string failure;
  const bool opened = SystemDialogs::OpenNativeMenu(
      rows, page_title(page),
      std::clamp(focus, 0, static_cast<int>(rows.size()) - 1), &failure,
      base_scene(), placement);
  if (!opened) {
    LOG_ERROR("[ANIVSH:SWEETY] page '%s' not opened: %s", page_title(page),
              failure.c_str());
    s_failed = true;
    Hide(); /* mid-session: no page will ever report Closed */
  }
  return opened;
}

bool Show() {
  if (s_save_pending)
    (void)flush_save();
  (void)LoadSettings();
  s_stack.assign(1, PageState{});
  forget_base_scene();
  if (MonoObject *scene = SystemDialogs::FocusedScene())
    s_base_scene = mono_gchandle_new(scene, 0);
  s_active = open_page(0);
  if (!s_active)
    forget_base_scene();
  LOG_INFO("[ANIVSH:SWEETY] native menu opened=%d", s_active ? 1 : 0);
  return s_active;
}

void pressed(int index) {
  if (index < 0 || static_cast<size_t>(index) >= s_shown.size())
    return;
  const Option option = s_shown[static_cast<size_t>(index)];
  switch (option.type) {
  case OptionType::Submenu:
    s_stack.back().selected = static_cast<size_t>(index);
    if (option.target == Page::HomeTabDetail)
      remember_tab_detail(option);
    {
      PageState next{};
      next.page = option.target;
      s_stack.push_back(next);
    }
    open_page(0);
    return;
  case OptionType::Toggle:
    /* the switch has flipped on screen already; this is the setting */
    activate(option);
    return;
  case OptionType::Value:
  case OptionType::SliderInt:
  case OptionType::SliderFloat:
    return;
  case OptionType::Strings: {
    int chosen = 0;
    if (SystemDialogs::NativeMenuChoice(index, &chosen))
      return; /* a drop-down: follow_inputs() picks the choice up */
    [[fallthrough]];
  }
  default:
    activate(option);
    /* Hide() from an action (unload, power) ends the session */
    if (s_active)
      open_page(index); /* what the row shows may have changed */
    return;
  }
}

/* A slider was moved or another choice picked: the option is stepped to
   where the row is now, through the same code a Left or Right press takes,
   so every side effect still runs. */
void follow_inputs() {
  for (size_t index = 0; index < s_shown.size(); ++index) {
    const Option &option = s_shown[index];
    int steps = 0;
    if (option.type == OptionType::SliderInt ||
        option.type == OptionType::SliderFloat) {
      float shown = 0.0f;
      if (!SystemDialogs::NativeMenuSlider(static_cast<int>(index), &shown))
        continue;
      steps = static_cast<int>(std::lround(
          (shown - slider_number(option)) / slider_step(option)));
    } else if (option.type == OptionType::Strings) {
      int chosen = 0;
      if (!SystemDialogs::NativeMenuChoice(static_cast<int>(index), &chosen))
        continue;
      steps = chosen - selector_index_for(option.id);
    } else {
      continue;
    }
    if (steps == 0)
      continue;
    LOG_INFO("[ANIVSH:SWEETY] '%s' moved by %d", option.title.c_str(), steps);
    steps = std::clamp(steps, -400, 400);
    for (; steps > 0; --steps)
      change_option(option, 1);
    for (; steps < 0; ++steps)
      change_option(option, -1);
  }
}

void Tick() {
  if (!s_active)
    return;
  int index = -1;
  const SystemDialogs::NativeMenuEvent event =
      SystemDialogs::PollNativeMenu(&index);
  follow_inputs(); /* after the poll, which reads the rows */
  switch (event) {
  case SystemDialogs::NativeMenuEvent::Selected:
    pressed(index);
    break;
  case SystemDialogs::NativeMenuEvent::Closed:
    /* Circle: back a page, or out of the menu from the first one */
    s_armed_option = OptionId::None;
    if (s_stack.size() > 1) {
      s_stack.pop_back();
      open_page(static_cast<int>(s_stack.back().selected));
    } else {
      s_active = false;
      forget_base_scene();
      request_save();
      LOG_INFO("[ANIVSH:SWEETY] native menu closed");
    }
    break;
  default:
    break;
  }
}

} // namespace SweetyNative
