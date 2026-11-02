uint32_t option_selection_key(const Option &option) {
  if (option.id != OptionId::None)
    return 0x40000000u | static_cast<uint32_t>(option.id);

  // Submenus and informational rows do not have an action id. Hash their
  // immutable description instead of using their changing vector index. The
  // key is only compared within one PageState, so a compact FNV-1a key is
  // sufficient and remains stable across telemetry/settings rebuilds.
  uint32_t hash = 2166136261u;
  const auto add = [&hash](uint32_t value) {
    hash ^= value;
    hash *= 16777619u;
  };
  add(static_cast<uint32_t>(option.type));
  add(static_cast<uint32_t>(option.target));
  for (unsigned char character : option.title) {
    add(character);
  }
  // A control's value is mutable state, not its identity. Including it here
  // made every toggle/selector edit look like a removed row, so
  // ensure_selection() fell back to the first option after each state write.
  // The same holds for a row that only shows a reading (temperature, clock
  // speed, uptime, free memory): the reading changes while the row is looked
  // at, and with it in the key the selection jumped back to the top of the
  // page on every refresh.
  if (option.type != OptionType::Value) {
    add(0xffu);
    for (unsigned char character : option.value) {
      add(character);
    }
  }
  add(static_cast<uint32_t>(option.payload));
  add(static_cast<uint32_t>(option.payload >> 32u));
  for (unsigned char character : option.external_key)
    add(character);
  return hash ? hash : 1u;
}

void clear_selection(PageState &state) {
  state.selected = 0;
  state.scroll = 0;
  state.selected_id = OptionId::None;
  state.selected_key = 0;
  state.selection_initialized = true;
}

/* Scrolls state.selected into view and remembers which row it is. */
void note_selection(PageState &state, const std::vector<Option> &options) {
  if (state.selected < state.scroll)
    state.scroll = state.selected;
  if (state.selected >= state.scroll + kMaxVisibleOptions)
    state.scroll = state.selected - kMaxVisibleOptions + 1;
  state.selected_id = options[state.selected].id;
  state.selected_key = option_selection_key(options[state.selected]);
  state.selection_initialized = true;
}

void ensure_selection(PageState &state, const std::vector<Option> &options) {
  const size_t count = options.size();
  if (!count) {
    clear_selection(state);
    return;
  }

  // Reconcile an already visited page by option identity.  A changed option
  // list must not make the old numeric index focus a different switch.
  if (state.selection_initialized && state.selected_key != 0) {
    // Rows may share a key (the same action offered twice, two readings
    // with one name): of those, the one nearest to where the selection was
    // is the one meant, not the first on the page.
    bool restored = false;
    size_t nearest = 0, nearest_distance = 0;
    for (size_t i = 0; i < count; ++i) {
      if (option_selection_key(options[i]) != state.selected_key)
        continue;
      const size_t distance =
          i > state.selected ? i - state.selected : state.selected - i;
      if (!restored || distance < nearest_distance) {
        nearest = i;
        nearest_distance = distance;
        restored = true;
      }
    }
    if (restored) {
      state.selected = nearest;
    } else {
      state.selected = 0;
      state.scroll = 0;
    }
  }
  if (state.selected >= count)
    state.selected = count - 1;
  note_selection(state, options);
}

void commit_selection(PageState &state, const std::vector<Option> &options,
                      size_t selected) {
  if (options.empty()) {
    clear_selection(state);
    return;
  }
  state.selected = std::min(selected, options.size() - 1);
  note_selection(state, options);
}
