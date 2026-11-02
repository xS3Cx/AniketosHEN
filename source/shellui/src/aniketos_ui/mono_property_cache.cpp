#include "aniketos_ui/mono_property_cache.hpp"

#include "hooked_funcs.hpp"

#include <atomic>
#include <cstring>

namespace AniketosUI::MonoPropertyCache {
namespace {

constexpr size_t kSlots = 1024; /* power of two; ~60 entries are used */
constexpr size_t kNameCapacity = 40;

struct Slot {
  MonoClass *klass;
  int kind;
  char name[kNameCapacity];
  void *value;
};

Slot s_slots[kSlots];
std::atomic_flag s_lock = ATOMIC_FLAG_INIT;

struct Guard {
  Guard() {
    while (s_lock.test_and_set(std::memory_order_acquire)) {
    }
  }
  ~Guard() { s_lock.clear(std::memory_order_release); }
};

bool matches(const Slot &slot, MonoClass *klass, int kind, const char *name) {
  return slot.klass == klass && slot.kind == kind &&
         std::strcmp(slot.name, name) == 0;
}

size_t hash_key(MonoClass *klass, const char *name, int kind) {
  uint64_t hash = 1469598103934665603ull ^ reinterpret_cast<uintptr_t>(klass);
  hash *= 1099511628211ull;
  hash ^= static_cast<uint64_t>(kind);
  hash *= 1099511628211ull;
  for (const char *p = name; *p; ++p) {
    hash ^= static_cast<unsigned char>(*p);
    hash *= 1099511628211ull;
  }
  return static_cast<size_t>(hash ^ (hash >> 29));
}

void *resolve(MonoClass *klass, const char *property, Kind kind) {
  MonoProperty *prop = mono_class_get_property_from_name(klass, property);
  if (!prop)
    return nullptr;
  MonoMethod *method = kind == Kind::GetThunk
                           ? mono_property_get_get_method(prop)
                           : mono_property_get_set_method(prop);
  if (!method)
    return nullptr;
  if (kind == Kind::SetMethod)
    return method;
  return reinterpret_cast<void *>(mono_compile_method(method));
}

} // namespace

void *Accessor(MonoClass *klass, const char *property, Kind kind) {
  if (!klass || !property)
    return nullptr;
  const size_t length = std::strlen(property);
  if (length >= kNameCapacity)
    return resolve(klass, property, kind);

  const int kind_id = static_cast<int>(kind);
  size_t index = hash_key(klass, property, kind_id) & (kSlots - 1);
  {
    Guard guard;
    for (size_t probe = 0; probe < 16; ++probe) {
      const Slot &slot = s_slots[(index + probe) & (kSlots - 1)];
      if (!slot.klass)
        break;
      if (matches(slot, klass, kind_id, property))
        return slot.value;
    }
  }

  /* Resolve outside the lock: the runtime takes its own locks, and a second
     thread resolving the same key simply stores the same answer. Failures
     are not cached, so a property that appears later is still found. */
  void *value = resolve(klass, property, kind);
  if (!value)
    return nullptr;

  Guard guard;
  for (size_t probe = 0; probe < 16; ++probe) {
    Slot &slot = s_slots[(index + probe) & (kSlots - 1)];
    if (matches(slot, klass, kind_id, property))
      return slot.value;
    if (!slot.klass) {
      std::memcpy(slot.name, property, length + 1);
      slot.kind = kind_id;
      slot.value = value;
      slot.klass = klass;
      break;
    }
  }
  return value;
}

MonoObject *ScratchColor(float r, float g, float b, float a) {
  static void (*constructor)(MonoObject *, float, float, float, float) =
      nullptr;
  static MonoObject *payload = nullptr;
  static bool failed = false;

  if (!payload && !failed) {
    MonoClass *color_class =
        pui_img ? mono_class_from_name(pui_img, "Sce.PlayStation.PUI", "UIColor")
                : nullptr;
    MonoObject *boxed = CreateUIColorBoxed(0.0f, 0.0f, 0.0f, 0.0f);
    const uint64_t address =
        color_class ? Get_Address_of_Method(pui_img, color_class, ".ctor", 4)
                    : 0;
    /* Pinned for the life of the process: `payload` points into the box. */
    if (boxed && address && mono_gchandle_new &&
        mono_gchandle_new(boxed, /*pinned=*/1)) {
      constructor = decltype(constructor)(address);
      payload = static_cast<MonoObject *>(mono_object_unbox(boxed));
    }
    failed = payload == nullptr;
  }
  if (!payload)
    return CreateUIColor(r, g, b, a);
  constructor(payload, r, g, b, a);
  return payload;
}

} // namespace AniketosUI::MonoPropertyCache
