#include "aniketos_shell/osk_glyphs.hpp"

#include "hooked_funcs.hpp"
#include "ps5/kernel.h"

#include <onion/detour.h>
#include <onion/distribution.h>
#include <onion/log.h>
#include <onion/settings.hpp>

#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern int g_libmono_handle;
extern onion::Settings g_settings;

namespace OskGlyphs {
namespace {

constexpr const char *kImage = "Sce.Vsh.ShellUI.Ime";
constexpr const char *kSpace = "Sce.Vsh.ShellUI.Ime";
constexpr int kMaxTries = 600; /* Tick calls, ~every 2 s: 20 minutes */
/* With this file the keyboard stays as the system has it. */
constexpr const char *kOff = ONION_DIST_USER_DATA_ROOT "/no_osk_glyphs";
/* Present while the key is laid out for the first time in a ShellUI run:
   found at the next start, the key is left out for that run. */
constexpr const char *kPending =
    ONION_DIST_USER_DATA_ROOT "/osk_glyphs_pending";

/* Our key: a CtrlButtonType none of the system's keys has. */
constexpr int32_t kGlyphKeyType = 0x60;
/* CtrlButtonType */
constexpr int32_t kShift = 2;
constexpr int32_t kSpaceKey = 7;
constexpr int32_t kBackspace = 9;
constexpr int32_t kCaretFirst = 10; /* CaretDown, CaretUp, CaretLeft, CaretRight */
constexpr int32_t kCaretLast = 13;
constexpr int32_t kDelete = 17;
constexpr int32_t kEnter = 22;
constexpr int32_t kClose = 27;
constexpr int32_t kAccentList = 4;        /* InputKeyType.Accent */
constexpr int32_t kControlNone = 0x10000; /* ImeControlEventId.None */
constexpr uint32_t kKeycodeValid = 0x1;   /* ImeKeycodeStates */
constexpr uint32_t kKeyDown = 1;          /* KeyEventType.Down */
constexpr int32_t kConfirm = 5;           /* KeyType.Enter: the confirm button */
/* The key's label: the PlayStation logo. */
constexpr char kKeyLabel[] = "\xEF\xA2\x92"; /* U+F892 */

constexpr uint16_t kGlyphs[] = {
#include "osk_glyph_table.inc"
};
constexpr int kGlyphCount = sizeof(kGlyphs) / sizeof(kGlyphs[0]);

/* ---- reflection helpers ------------------------------------------------- */

std::string text_of(MonoObject *managed) {
  if (!managed)
    return {};
  const char *utf =
      mono_string_to_utf8(reinterpret_cast<MonoString *>(managed));
  std::string text = utf ? utf : "";
  if (utf && mono_free)
    mono_free(const_cast<char *>(utf));
  return text;
}

std::string exception_text(MonoObject *exception) {
  MonoObject *nested = nullptr;
  MonoString *shown = exception ? mono_object_to_string(exception, &nested)
                                : nullptr;
  std::string text = shown && !nested
                         ? text_of(reinterpret_cast<MonoObject *>(shown))
                         : std::string("managed exception");
  return text.substr(0, 300);
}

MonoMethod *method_of(MonoObject *object, const char *name, int parameters) {
  for (MonoClass *k = object ? mono_object_get_class(object) : nullptr; k;
       k = mono_class_get_parent(k))
    if (MonoMethod *method =
            mono_class_get_method_from_name(k, name, parameters))
      return method;
  return nullptr;
}

MonoObject *call(MonoObject *object, const char *name, void **args = nullptr,
                 int count = 0) {
  MonoMethod *method = method_of(object, name, count);
  if (!method)
    return nullptr;
  MonoObject *exception = nullptr;
  MonoObject *value = mono_runtime_invoke(method, object, args, &exception);
  if (exception) {
    LOG_WARN("[OSK] %s: %s", name, exception_text(exception).c_str());
    return nullptr;
  }
  return value;
}

MonoClassField *find_field(MonoObject *object, const char *name) {
  for (MonoClass *k = object ? mono_object_get_class(object) : nullptr; k;
       k = mono_class_get_parent(k))
    if (MonoClassField *f = mono_class_get_field_from_name(k, name))
      return f;
  return nullptr;
}

MonoObject *field(MonoObject *object, const char *name) {
  static MonoObject *(*value_object)(MonoDomain *, MonoClassField *,
                                     MonoObject *) = nullptr;
  if (!value_object)
    value_object = reinterpret_cast<decltype(value_object)>(
        kernel_dynlib_dlsym(-1, g_libmono_handle,
                            "mono_field_get_value_object"));
  MonoClassField *f = find_field(object, name);
  return f && value_object ? value_object(mono_domain_get(), f, object)
                           : nullptr;
}

template <typename T> T unboxed(MonoObject *boxed, T fallback) {
  return boxed ? *static_cast<T *>(mono_object_unbox(boxed)) : fallback;
}

template <typename T>
T value_field(MonoObject *object, const char *name, T fallback) {
  return unboxed<T>(field(object, name), fallback);
}

/* A field set to `value`: the object itself for a reference, a pointer to
   the value for a value type (mono_field_set_value's rule). */
void set_field(MonoObject *object, const char *name, void *value) {
  if (MonoClassField *f = find_field(object, name))
    mono_field_set_value(object, f, value);
}

int count_of(MonoObject *collection) {
  return unboxed<int32_t>(call(collection, "get_Count"), 0);
}

MonoObject *item_at(MonoObject *list, int32_t index) {
  void *args[1] = {&index};
  return call(list, "get_Item", args, 1);
}

void add_to(MonoObject *list, MonoObject *item) {
  void *args[1] = {item};
  call(list, "Add", args, 1);
}

uintptr_t array_length(MonoObject *array) {
  return array ? *reinterpret_cast<uintptr_t *>(
                     reinterpret_cast<char *>(array) + 3 * sizeof(void *))
               : 0;
}
MonoObject *array_item(MonoObject *array, uintptr_t index) {
  return reinterpret_cast<MonoObject **>(reinterpret_cast<char *>(array) +
                                         4 * sizeof(void *))[index];
}

/* A new object of `klass`, made with its constructor of `count` arguments. */
MonoObject *make(MonoClass *klass, void **args, int count, const char *what) {
  MonoMethod *constructor =
      klass ? mono_class_get_method_from_name(klass, ".ctor", count) : nullptr;
  MonoObject *object =
      constructor ? mono_object_new(mono_domain_get(), klass) : nullptr;
  if (!object)
    return nullptr;
  MonoObject *exception = nullptr;
  mono_runtime_invoke(constructor, object, args, &exception);
  if (exception) {
    LOG_WARN("[OSK] %s: %s", what, exception_text(exception).c_str());
    return nullptr;
  }
  return object;
}

/* An empty list of the same kind as `list`. */
MonoObject *empty_like(MonoObject *list) {
  return list ? make(mono_object_get_class(list), nullptr, 0, "list")
              : nullptr;
}

MonoObject *target(uint32_t handle) {
  return handle ? mono_gchandle_get_target(handle) : nullptr;
}
void keep(uint32_t &handle, MonoObject *object) {
  if (handle)
    mono_gchandle_free(handle);
  handle = object ? mono_gchandle_new_weakref(object, 0) : 0;
}

MonoString *managed(const std::string &text) {
  return mono_string_new(mono_domain_get(), text.c_str());
}

std::string utf8_of(uint16_t unit) {
  const char text[3] = {static_cast<char>(0xe0 | (unit >> 12)),
                        static_cast<char>(0x80 | ((unit >> 6) & 0x3f)),
                        static_cast<char>(0x80 | (unit & 0x3f))};
  return std::string(text, 3);
}

bool exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

/* ---- state -------------------------------------------------------------- */

MonoImage *s_image = nullptr;
MonoClass *s_function_keytop = nullptr; /* FunctionKeytopModel */
uint32_t s_main_presenter = 0; /* MainKeyAreaPresenter on screen */
uint32_t s_key = 0;            /* our ImeButton */
uint32_t s_key_model = 0;      /* our FunctionKeytopModel */
int s_page = -1;               /* glyph page shown; -1: the language's keys */
int s_pages = 0;
unsigned s_applied = 0;        /* pages put on the keys, ever */
bool s_enabled = true;         /* false: this run leaves the keyboard alone */
int s_pending_frames = -1;     /* frames until kPending is removed */

bool active() { return s_enabled && g_settings.osk_glyph_key; }

/* ---- the glyph pages ---------------------------------------------------- */

/* A key of a glyph page: a copy of the language's key at that place whose
   label and text are the glyph, with the key's code cleared (a glyph has
   none). */
MonoObject *glyph_key(MonoObject *letter, uint16_t glyph) {
  MonoObject *letter_events = field(letter, "KeyEvents");
  MonoObject *event =
      count_of(letter_events) ? item_at(letter_events, 0) : nullptr;
  MonoObject *letter_codes = field(event, "KeycodeInfo");
  MonoObject *code =
      count_of(letter_codes) ? item_at(letter_codes, 0) : nullptr;
  MonoObject *key = mono_object_clone(letter);
  MonoObject *events = empty_like(letter_events);
  MonoObject *our_event = event ? mono_object_clone(event) : nullptr;
  MonoObject *codes = empty_like(letter_codes);
  if (!key || !events || !our_event || !codes)
    return nullptr;
  MonoString *text = managed(utf8_of(glyph));
  if (code) {
    MonoObject *our_code = mono_object_clone(code);
    if (!our_code)
      return nullptr;
    uint16_t character = glyph;
    uint16_t keycode = 0; /* KEYCODE_NOEVENT */
    uint32_t state = value_field<uint32_t>(code, "State", 0) & ~kKeycodeValid;
    set_field(our_code, "Character", &character);
    set_field(our_code, "Keycode", &keycode);
    set_field(our_code, "State", &state);
    add_to(codes, our_code);
  }
  set_field(our_event, "TextToSend", text);
  set_field(our_event, "KeycodeInfo", codes);
  add_to(events, our_event);
  set_field(key, "LabelText", text);
  set_field(key, "KeyEvents", events);
  return key;
}

/* A key that shows and sends nothing, as the system's own pages end: taken
   from the language's accent page. None: the last page overlaps the one
   before it instead. */
MonoObject *blank_key(MonoObject *keymap) {
  int32_t accents = kAccentList;
  MonoObject *list = nullptr;
  void *args[2] = {&accents, &list};
  if (!keymap ||
      !unboxed<bool>(call(keymap, "GetKeycodeInfoList", args, 2), false))
    return nullptr;
  for (uintptr_t i = array_length(list); i-- > 0;) {
    MonoObject *key = array_item(list, i);
    if (key && text_of(field(key, "LabelText")).empty())
      return key;
  }
  return nullptr;
}

MonoObject *key_info(MonoObject *keytop) {
  return call(call(keytop, "get_KeyInfo"), "get_Value");
}
void set_key_info(MonoObject *keytop, MonoObject *info) {
  void *args[1] = {info};
  call(call(keytop, "get_KeyInfo"), "set_Value", args, 1);
}

/* The page s_page on the keys the language has just filled: as many glyphs
   a page as the language has keys there. Past the last page: the language's
   keys stay. */
void show_page(MonoObject *model) {
  MonoObject *keytops = call(model, "get_KeytopList");
  const int count = count_of(keytops);
  std::vector<MonoObject *> slots, letters;
  for (int i = 0; i < count; ++i) {
    MonoObject *keytop = item_at(keytops, i);
    MonoObject *info = keytop ? key_info(keytop) : nullptr;
    if (info && !text_of(field(info, "LabelText")).empty() &&
        count_of(field(info, "KeyEvents")) > 0) {
      slots.push_back(keytop);
      letters.push_back(info);
    }
  }
  const int size = static_cast<int>(slots.size());
  s_pages = size ? (kGlyphCount + size - 1) / size : 0;
  if (s_page >= s_pages) {
    s_page = -1;
    return;
  }
  MonoObject *blank = blank_key(call(model, "get_CurrentKeymapModel"));
  int first = s_page * size;
  if (!blank && first + size > kGlyphCount)
    first = kGlyphCount > size ? kGlyphCount - size : 0;
  for (int j = 0; j < size; ++j) {
    const int at = first + j;
    MonoObject *key =
        at < kGlyphCount ? glyph_key(letters[j], kGlyphs[at]) : blank;
    if (key)
      set_key_info(slots[j], key);
  }
  ++s_applied;
}

void show_label() {
  MonoObject *key = target(s_key);
  if (!key)
    return;
  const std::string label =
      s_page < 0 ? std::string(kKeyLabel)
                 : std::to_string(s_page + 1) + "/" + std::to_string(s_pages);
  void *args[1] = {managed(label)};
  call(key, "set_Text", args, 1);
}

/* The main area shown again through its own ChangePanel, whose
   UpdateKeycodeInfo puts the language's keys (or the page) on it. */
void refresh_keys() {
  MonoObject *presenter = target(s_main_presenter);
  MonoObject *model = presenter ? field(presenter, "_model") : nullptr;
  if (!model)
    return;
  int32_t type = unboxed<int32_t>(call(model, "get_CurrentListType"), 0);
  bool keytop_only = true;
  void *args[2] = {&type, &keytop_only};
  const unsigned before = s_applied;
  call(presenter, "ChangePanel", args, 2);
  if (s_page >= 0 && s_applied == before) {
    /* a ChangePanel that did nothing for the panel shown */
    void *update_args[1] = {&type};
    call(model, "UpdateKeycodeInfo", update_args, 1);
  }
}

/* The language's keys back (refresh: for a press that does not refresh
   them itself). */
void leave_pages(bool refresh) {
  s_page = -1;
  show_label();
  if (refresh)
    refresh_keys();
}

/* Our key: the next glyph page; after the last, the language's keys. */
void next_page() {
  if (!target(s_main_presenter))
    return;
  s_page = s_page < 0 ? 0 : s_page + 1;
  refresh_keys();
  show_label();
}

/* ---- the glyphs through the confirm ------------------------------------- */

/* The keyboard's engine keeps a glyph only in the text being typed (the
   preedit). At the confirm it sends that text once more without the
   console's private-use characters, and the field confirms the preedit it
   holds, both in one frame: the glyphs would be lost. The last preedit with
   glyphs is kept, and a confirm of that text without its glyphs gives the
   field the kept preedit back first. A preedit without the glyphs that no
   confirm follows in its frame is the user's own edit (the last glyph
   deleted), and the kept text is dropped. The log has counts, never the
   text (a field may hold a password). */

/* `text` without the console's glyphs (U+E000-U+F8FF); `count` gets how many
   there were. */
std::string without_glyphs(const std::string &text, int *count) {
  std::string out;
  int glyphs = 0;
  for (size_t i = 0; i < text.size();) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    size_t length = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
    if (i + length > text.size())
      length = text.size() - i;
    uint32_t code = 0;
    if (length == 3)
      code = ((lead & 0x0fu) << 12) |
             ((static_cast<unsigned char>(text[i + 1]) & 0x3fu) << 6) |
             (static_cast<unsigned char>(text[i + 2]) & 0x3fu);
    if (length == 3 && code >= 0xe000 && code <= 0xf8ff)
      ++glyphs;
    else
      out.append(text, i, length);
    i += length;
  }
  if (count)
    *count = glyphs;
  return out;
}

uint32_t s_preedit_field = 0; /* the TextField of s_preedit */
std::string s_preedit;        /* its last preedit with glyphs in it */
int s_preedit_glyphs = 0;
uint32_t s_preedit_caret = 0; /* that preedit's caret and clause */
int32_t s_preedit_clause_start = 0;
int32_t s_preedit_clause_end = 0;
int64_t s_frame = 0;          /* Tick calls */
int64_t s_bare_frame = -1;    /* frame of a preedit that was s_preedit
                                 without its glyphs */
int s_confirm_logs = 0;

void forget_preedit() {
  keep(s_preedit_field, nullptr);
  s_preedit.clear();
  s_preedit_glyphs = 0;
  s_preedit_caret = 0;
  s_preedit_clause_start = s_preedit_clause_end = 0;
  s_bare_frame = -1;
}

using UpdatePreedit = void (*)(MonoObject *, MonoString *, uint32_t, int32_t,
                               int32_t);
UpdatePreedit s_update_preedit = nullptr;
void update_preedit_hook(MonoObject *text_field, MonoString *text,
                         uint32_t caret, int32_t start, int32_t end) {
  if (s_update_preedit)
    s_update_preedit(text_field, text, caret, start, end);
  if (!active() && s_preedit_field)
    forget_preedit(); /* the key turned off mid-edit */
  if (!text_field || !active())
    return;
  const std::string typed = text_of(reinterpret_cast<MonoObject *>(text));
  int glyphs = 0;
  const std::string plain = without_glyphs(typed, &glyphs);
  const bool ours = target(s_preedit_field) == text_field;
  if (glyphs) {
    keep(s_preedit_field, text_field);
    s_preedit = typed;
    s_preedit_glyphs = glyphs;
    s_preedit_caret = caret;
    s_preedit_clause_start = start;
    s_preedit_clause_end = end;
    s_bare_frame = -1;
  } else if (ours && plain == without_glyphs(s_preedit, nullptr)) {
    /* the engine right before its confirm, or the last glyph deleted: a
       confirm in this frame tells them apart */
    s_bare_frame = s_frame;
  } else if (ours) {
    /* edited since: the glyphs kept are not the field's any more */
    forget_preedit();
  }
}

using ConfirmPreedit = void (*)(MonoObject *, MonoString *, bool);
ConfirmPreedit s_confirm_preedit = nullptr;
void confirm_preedit_hook(MonoObject *text_field, MonoString *text,
                          bool by_ime) {
  MonoString *confirmed = text;
  if (text_field && active() && target(s_preedit_field) == text_field) {
    const std::string got = text_of(reinterpret_cast<MonoObject *>(text));
    int glyphs = 0;
    without_glyphs(got, &glyphs);
    if (s_preedit_glyphs && !glyphs &&
        (s_bare_frame < 0 || s_bare_frame == s_frame) &&
        got == without_glyphs(s_preedit, nullptr)) {
      /* the bare preedit has already replaced the field's: the one with the
         glyphs goes back before the field confirms what it holds */
      confirmed = managed(s_preedit);
      if (s_update_preedit)
        s_update_preedit(text_field, confirmed, s_preedit_caret,
                         s_preedit_clause_start, s_preedit_clause_end);
      if (s_confirm_logs++ < 20)
        LOG_INFO("[OSK] confirm kept %d glyphs", s_preedit_glyphs);
    }
    forget_preedit();
  }
  if (s_confirm_preedit)
    s_confirm_preedit(text_field, confirmed, by_ime);
}

/* ---- the keyboard's own emoji ------------------------------------------- */

/* TextFieldEditIme.CreateImeOptions (PUI UI3): the options a field opens
   the keyboard with. With EnableUnicodeEmoji the keyboard lays out its own
   emoji key and panel, and its engine takes emoji. A password field is
   left as it is. */
constexpr int32_t kEnableUnicodeEmoji = 0x00040000; /* ImeOptions */
constexpr int32_t kPasswordMode = 0x00000004;
using CreateImeOptions = int32_t (*)(MonoObject *);
CreateImeOptions s_create_ime_options = nullptr;
int s_option_logs = 0;
int32_t create_ime_options_hook(MonoObject *text_field) {
  const int32_t given =
      s_create_ime_options ? s_create_ime_options(text_field) : 0;
  int32_t options = given;
  if (s_enabled && g_settings.osk_emoji && !(options & kPasswordMode))
    options |= kEnableUnicodeEmoji;
  if (options != given && s_option_logs++ < 5)
    LOG_INFO("[OSK] keyboard options 0x%x -> 0x%x (emoji)",
             static_cast<unsigned>(given), static_cast<unsigned>(options));
  return options;
}

/* ---- the hooks ---------------------------------------------------------- */

/* FunctionAreaPanel.AddBasedOnLanguageArea: the language's own function
   keys (Shift first) are listed as (type, flex share, area), each time the
   keyboard is laid out. Shift gives one share of its width to our key,
   which comes right after it. */
using AddArea = void (*)(MonoObject *, uint64_t, uint64_t, uint64_t);
AddArea s_add_area = nullptr;
void add_area_hook(MonoObject *panel, uint64_t type, uint64_t option,
                   uint64_t lang) {
  if (s_add_area)
    s_add_area(panel, type, option, lang);
  s_page = -1;
  if (!panel || !active())
    return;
  MonoObject *layouts = field(panel, "_layoutInfoList");
  const int count = count_of(layouts);
  int shift = -1;
  for (int i = 0; i < count; ++i) {
    const int32_t button =
        value_field<int32_t>(item_at(layouts, i), "ButtonType", -1);
    if (button == kGlyphKeyType)
      return;
    if (button == kShift && shift < 0)
      shift = i;
  }
  MonoObject *info = shift >= 0 ? item_at(layouts, shift) : nullptr;
  float grow = value_field<float>(info, "FrexGrow", 0.0f);
  if (!info || grow < 2.0f)
    return;
  int32_t our_type = kGlyphKeyType;
  float share = 1.0f;
  int32_t area = value_field<int32_t>(info, "AreaType", 0);
  void *args[3] = {&our_type, &share, &area};
  MonoObject *ours = make(mono_object_get_class(info), args, 3, "layout");
  if (!ours)
    return;
  if (s_pending_frames < 0) {
    /* the first time in this run: marked until it has been shown */
    const int fd = open(kPending, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0)
      close(fd);
    s_pending_frames = 300;
  }
  grow -= 1.0f;
  set_field(info, "FrexGrow", &grow);
  int32_t at = shift + 1;
  void *insert_args[2] = {&at, ours};
  call(layouts, "Insert", insert_args, 2);
}

/* Our key's model: a function key of type kGlyphKeyType with the logo,
   sending nothing and standing for no control event. */
MonoObject *make_key_model(MonoObject *map) {
  MonoClass *info_class = mono_class_from_name(s_image, kSpace, "KeytopInfo");
  int32_t shift = kShift;
  void *shift_args[1] = {&shift};
  MonoObject *shift_model = call(map, "get_Item", shift_args, 1);
  /* a List<ImeKeyEvent> like Shift's */
  MonoObject *events =
      empty_like(field(shift_model ? key_info(shift_model) : nullptr,
                       "KeyEvents"));
  if (!info_class || !events)
    return nullptr;
  int32_t texture = 0, guide = 0, our_type = kGlyphKeyType, kind = 0;
  int32_t control = kControlNone;
  void *info_args[7] = {managed(kKeyLabel), events, &texture, &guide,
                        &our_type, &kind, &control};
  MonoObject *info = make(info_class, info_args, 7, "key info");
  void *model_args[1] = {info};
  return info ? make(s_function_keytop, model_args, 1, "key model") : nullptr;
}

/* FunctionKeyAreaPresenter.SetBindings: each key of the model's map is
   bound to its button. Ours is put in the map first, so its button is
   bound like the others (pressed, focused, read out). */
using SetBindings = void (*)(MonoObject *);
SetBindings s_set_bindings = nullptr;
void set_bindings_hook(MonoObject *presenter) {
  MonoObject *view = presenter ? field(presenter, "_view") : nullptr;
  MonoObject *buttons = field(view, "<ButtonList>k__BackingField");
  MonoObject *button = nullptr;
  for (uintptr_t i = 0, n = array_length(buttons); i < n && !button; ++i) {
    MonoObject *candidate = array_item(buttons, i);
    if (candidate &&
        value_field<int32_t>(candidate, "ButtonType", -1) == kGlyphKeyType)
      button = candidate;
  }
  MonoObject *map =
      button ? call(field(presenter, "_model"), "get_ButtonMap") : nullptr;
  int32_t our_type = kGlyphKeyType;
  void *has_args[1] = {&our_type};
  if (map && !unboxed<bool>(call(map, "ContainsKey", has_args, 1), true)) {
    if (MonoObject *model = make_key_model(map)) {
      void *set_args[2] = {&our_type, model};
      call(map, "set_Item", set_args, 2);
      keep(s_key_model, model);
    }
  }
  if (s_set_bindings)
    s_set_bindings(presenter);
  if (button) {
    keep(s_key, button);
    show_label();
  }
}

/* KeytopModel.OnPressKey: a function key pressed (cross, touch, or its
   shortcut). One that changes the keys (Shift, @#:, accents, ...) leaves
   the pages first. */
using OnPressKey = void (*)(MonoObject *, uint64_t, uint64_t, MonoObject *);
OnPressKey s_on_press = nullptr;
void on_press_hook(MonoObject *model, uint64_t event, uint64_t shortcut,
                   MonoObject *option) {
  const bool down = static_cast<uint32_t>(event) == kKeyDown;
  if (model && s_key_model && model == target(s_key_model))
    return; /* ours sends nothing */
  if (s_page >= 0 && down && model &&
      mono_object_get_class(model) == s_function_keytop) {
    const int32_t type =
        value_field<int32_t>(key_info(model), "ButtonType", -1);
    const bool keeps = type == kSpaceKey || type == kBackspace ||
                       type == kDelete ||
                       (type >= kCaretFirst && type <= kCaretLast);
    if (!keeps)
      leave_pages(type != kEnter && type != kClose);
  }
  if (s_on_press)
    s_on_press(model, event, shortcut, option);
}

/* Widget.OnKeyEvent (PUI): the keys a widget receives. The keyboard binds
   our key to its model like the others, but never passes it a press: the
   confirm button on our key is taken here, after the widget has had it. */
using OnKeyEvent = void (*)(MonoObject *, MonoObject *);
OnKeyEvent s_on_key_event = nullptr;
void on_key_event_hook(MonoObject *widget, MonoObject *args) {
  if (s_on_key_event)
    s_on_key_event(widget, args);
  if (!widget || !s_key || widget != target(s_key) || !args)
    return;
  if (unboxed<int32_t>(call(args, "get_KeyType"), -1) == kConfirm &&
      unboxed<uint32_t>(call(args, "get_KeyEventType"), 0) == kKeyDown)
    next_page();
}

/* MainKeyAreaModel.UpdateKeycodeInfo: the language's keys put on the main
   area. With a page shown, the page goes on them instead (Shift released
   after a character, the keyboard refreshed). */
using UpdateKeycode = void (*)(MonoObject *, uint64_t);
UpdateKeycode s_update_keycode = nullptr;
void update_keycode_hook(MonoObject *model, uint64_t type) {
  if (s_update_keycode)
    s_update_keycode(model, type);
  if (model && s_page >= 0)
    show_page(model);
}

/* MainKeyAreaPresenter.ChangePanel: only followed, for the presenter. */
using ChangePanel = void (*)(MonoObject *, uint64_t, uint64_t);
ChangePanel s_change_panel = nullptr;
void change_panel_hook(MonoObject *presenter, uint64_t type, uint64_t only) {
  if (presenter && target(s_main_presenter) != presenter)
    keep(s_main_presenter, presenter);
  if (s_change_panel)
    s_change_panel(presenter, type, only);
}

bool hook(MonoImage *image, const char *klass, const char *method, int params,
          void *destination, void **original) {
  MonoClass *k = image ? mono_class_from_name(image, kSpace, klass) : nullptr;
  const uint64_t address =
      k ? Get_Address_of_Method(image, k, method, params) : 0;
  const bool ok = address && InstallDetour(address, destination, original);
  if (!ok) {
    *original = nullptr;
    LOG_WARN("[OSK] %s.%s not hooked", klass, method);
  }
  return ok;
}

/* The keyboard's assembly is ShellUI's own (its application folder, not the
   shared library folder): the image already loaded is taken by its name, so
   the hooks go on the code ShellUI runs. Not loaded yet: tried again later. */
MonoImage *loaded_image() {
  static MonoImage *(*image_loaded)(const char *) = nullptr;
  if (!image_loaded)
    image_loaded = reinterpret_cast<decltype(image_loaded)>(
        kernel_dynlib_dlsym(-1, g_libmono_handle, "mono_image_loaded"));
  return image_loaded ? image_loaded(kImage) : nullptr;
}

bool s_installed = false;
int s_tries = 0;

} // namespace

void Install() {
  if (s_installed)
    return;
  if (exists(kOff)) {
    s_installed = true;
    LOG_INFO("[OSK] %s present: keyboard left as it is", kOff);
    return;
  }
  MonoImage *image = loaded_image();
  if (!image) {
    ++s_tries;
    return;
  }
  s_installed = true;
  if (exists(kPending)) {
    /* the last run ended while our key was being laid out */
    unlink(kPending);
    s_enabled = false;
    LOG_WARN("[OSK] the last ShellUI run stopped with the glyph key being "
             "laid out; left out until the next start");
    return;
  }
  s_image = image;
  s_function_keytop =
      mono_class_from_name(image, kSpace, "FunctionKeytopModel");
  /* Pressing and paging first: without them the key would be there for
     nothing. */
  if (!s_function_keytop ||
      !hook(image, "KeytopModel", "OnPressKey", 3,
            reinterpret_cast<void *>(&on_press_hook),
            reinterpret_cast<void **>(&s_on_press)) ||
      !hook(image, "MainKeyAreaPresenter", "ChangePanel", 2,
            reinterpret_cast<void *>(&change_panel_hook),
            reinterpret_cast<void **>(&s_change_panel)) ||
      !hook(image, "MainKeyAreaModel", "UpdateKeycodeInfo", 1,
            reinterpret_cast<void *>(&update_keycode_hook),
            reinterpret_cast<void **>(&s_update_keycode)) ||
      !hook(image, "FunctionKeyAreaPresenter", "SetBindings", 0,
            reinterpret_cast<void *>(&set_bindings_hook),
            reinterpret_cast<void **>(&s_set_bindings)))
    return;
  /* the press of our key, on the widget itself */
  MonoImage *pui = getDLLimage("Sce.PlayStation.PUI.dll");
  MonoClass *widget =
      pui ? mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "Widget")
          : nullptr;
  const uint64_t on_key_event =
      widget ? Get_Address_of_Method(pui, widget, "OnKeyEvent", 1) : 0;
  if (!on_key_event ||
      !InstallDetour(on_key_event, reinterpret_cast<void *>(&on_key_event_hook),
                     reinterpret_cast<void **>(&s_on_key_event))) {
    s_on_key_event = nullptr;
    LOG_WARN("[OSK] Widget.OnKeyEvent not hooked");
    return;
  }
  /* the text a field gets from the keyboard's engine */
  MonoClass *text_field =
      pui ? mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3", "TextField")
          : nullptr;
  struct TextHook {
    const char *method;
    int params;
    void *destination;
    void **original;
  };
  const TextHook text_hooks[] = {
      {"UpdatePreedit", 4, reinterpret_cast<void *>(&update_preedit_hook),
       reinterpret_cast<void **>(&s_update_preedit)},
      {"ConfirmPreedit", 2, reinterpret_cast<void *>(&confirm_preedit_hook),
       reinterpret_cast<void **>(&s_confirm_preedit)},
  };
  for (const TextHook &text_hook : text_hooks) {
    const uint64_t address =
        text_field ? Get_Address_of_Method(pui, text_field, text_hook.method,
                                           text_hook.params)
                   : 0;
    const bool ok = address && InstallDetour(address, text_hook.destination,
                                             text_hook.original);
    if (!ok)
      *text_hook.original = nullptr;
    LOG_INFO("[OSK] TextField.%s hooked=%d", text_hook.method, ok ? 1 : 0);
  }
  /* the keyboard's own emoji, for every field that opens it */
  MonoClass *edit_ime =
      pui ? mono_class_from_name(pui, "Sce.PlayStation.PUI.UI3",
                                 "TextFieldEditIme")
          : nullptr;
  const uint64_t create_options =
      edit_ime ? Get_Address_of_Method(pui, edit_ime, "CreateImeOptions", 1)
               : 0;
  const bool options_hooked =
      create_options &&
      InstallDetour(create_options,
                    reinterpret_cast<void *>(&create_ime_options_hook),
                    reinterpret_cast<void **>(&s_create_ime_options));
  if (!options_hooked)
    s_create_ime_options = nullptr;
  LOG_INFO("[OSK] TextFieldEditIme.CreateImeOptions hooked=%d",
           options_hooked ? 1 : 0);
  if (hook(image, "FunctionAreaPanel", "AddBasedOnLanguageArea", 3,
           reinterpret_cast<void *>(&add_area_hook),
           reinterpret_cast<void **>(&s_add_area)))
    LOG_INFO("[OSK] glyph key ready (%d glyphs)", kGlyphCount);
}

void Tick() {
  if (s_pending_frames > 0 && --s_pending_frames == 0)
    unlink(kPending); /* laid out and still running */
  if (s_bare_frame >= 0)
    forget_preedit(); /* no confirm came with it: the user deleted the
                         glyphs (a confirm clears it) */
  ++s_frame;
  /* the keyboard's code is loaded with its first use, not before */
  static int frames = 0;
  if (s_installed || s_tries > kMaxTries || ++frames % 120 != 0)
    return;
  Install();
}

} // namespace OskGlyphs
