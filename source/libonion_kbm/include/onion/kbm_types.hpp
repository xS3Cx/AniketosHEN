/* Keyboard & mouse for games: the shared contract.
 *
 * A profile (JSON, edited in the Toolbox, kept in /data/AniketosHEN/kbm) is
 * compiled into one flat, pointer-free ProfileBlob. The daemon writes the
 * blob into the agent's control page inside the game; the agent's engine
 * turns keyboard and mouse input into a controller state every time the game
 * reads its pad. Everything here is plain data: the agent uses it without a
 * heap, exceptions or locks.
 */
#pragma once

#include <stdint.h>

namespace onion::kbm {

/* ---- inputs ----------------------------------------------------------------
 * One 16-bit id per physical input. */
using Input = uint16_t;
constexpr Input kInputNone = 0;
/* 0x0001-0x00ff: keyboard keys by USB HID usage (0xe0-0xe7: LCtrl, LShift,
   LAlt, LGui, RCtrl, RShift, RAlt, RGui). */
constexpr Input kKeyFirst = 0x0001, kKeyLast = 0x00ff;
/* mouse buttons 1-8: left, right, middle, back (side 1), forward (side 2) */
constexpr Input kMouseButtonFirst = 0x0100;
constexpr Input kMouseLeft = 0x0100, kMouseRight = 0x0101, kMouseMiddle = 0x0102,
                kMouseBack = 0x0103, kMouseForward = 0x0104;
constexpr Input kMouseButtonLast = 0x0107;
/* wheel notches and tilt: each notch is a short press (Binding.pulse_ms) */
constexpr Input kWheelUp = 0x0110, kWheelDown = 0x0111, kWheelLeft = 0x0112,
                kWheelRight = 0x0113;
/* mouse motion as digital inputs (held while the mouse moves that way
   faster than Layer.mouse_digital_threshold) */
constexpr Input kMouseMoveRight = 0x0120, kMouseMoveLeft = 0x0121,
                kMouseMoveDown = 0x0122, kMouseMoveUp = 0x0123;
constexpr int kInputCount = 0x0124; /* ids are < this */

/* ---- outputs: the controller ------------------------------------------------
 * Button bits as libScePad reports them (ScePadData.buttons). */
enum PadButton : uint32_t {
  kPadL3 = 0x00000002, kPadR3 = 0x00000004, kPadOptions = 0x00000008,
  kPadUp = 0x00000010, kPadRight = 0x00000020, kPadDown = 0x00000040,
  kPadLeft = 0x00000080, kPadL2 = 0x00000100, kPadR2 = 0x00000200,
  kPadL1 = 0x00000400, kPadR1 = 0x00000800, kPadTriangle = 0x00001000,
  kPadCircle = 0x00002000, kPadCross = 0x00004000, kPadSquare = 0x00008000,
  kPadTouchpad = 0x00100000,
  kPadIntercepted = 0x80000000, /* the system has the pad: never merge */
};
enum Axis : uint8_t { kAxisLX = 0, kAxisLY = 1, kAxisRX = 2, kAxisRY = 3 };
enum Trigger : uint8_t { kTriggerL2 = 0, kTriggerR2 = 1 };

/* The controller state the engine produces. Sticks 0..255 (128 centre, Y
   down positive as the pad reports it), triggers 0..255. */
struct PadState {
  uint32_t buttons;
  uint8_t lx, ly, rx, ry;
  uint8_t l2, r2;
  uint8_t active; /* 0: nothing of ours, pass the real pad untouched */
  uint8_t reserved;
};

/* ---- what one binding does --------------------------------------------- */
enum class OutputKind : uint8_t {
  None = 0,      /* bound to nothing: stops the input falling to lower layers */
  Button,        /* press pad buttons (mask) */
  AxisDirection, /* push a stick axis toward value (-32767..32767) */
  Trigger,       /* pull L2/R2 to value (0..32767); sets its button bit past
                    trigger_button_threshold */
  LayerHold,     /* layer `layer` active while held */
  LayerToggle,   /* layer `layer` toggled on each activation */
  Macro,         /* run macro `macro` */
  StickScale,    /* scale stick `axis` group (0: left, 1: right) by value/32767
                    while held: walk, precise aim */
  MappingToggle, /* suspend / resume the whole mapping */
};

/* How an analog output (AxisDirection, Trigger) reaches its value. */
struct Ramp {
  uint16_t rise_ms;  /* 0: instant; else time from 0 to value while held */
  uint16_t fall_ms;  /* 0: instant; else time back to 0 after release */
  uint16_t curve_x100; /* shape of the rise: 100 linear, >100 slow start
                          (progressive throttle), <100 fast start */
  uint16_t reserved;
};

struct Output {
  OutputKind kind;
  uint8_t axis;    /* Axis for AxisDirection; 0/1 stick for StickScale */
  uint8_t trigger; /* Trigger for Trigger */
  uint8_t layer;   /* LayerHold / LayerToggle */
  uint8_t macro;   /* Macro */
  uint8_t reserved[3];
  uint32_t buttons; /* Button */
  int16_t value;    /* AxisDirection, Trigger, StickScale */
  uint16_t reserved2;
  Ramp ramp;        /* AxisDirection, Trigger */
};

/* When a binding fires. */
enum class Gesture : uint8_t {
  Press = 0,  /* while the input is held */
  Tap,        /* released within tap_ms: fires a pulse of pulse_ms */
  LongPress,  /* held at least long_ms: fires until released */
  DoubleTap,  /* second press within double_ms: fires while held */
  Release,    /* on release: fires a pulse of pulse_ms */
};
/* What firing does. */
enum class Mode : uint8_t {
  Hold = 0, /* outputs on while firing */
  Toggle,   /* each firing flips the outputs on/off */
  Turbo,    /* while firing, outputs pulse at turbo_hz with turbo_duty */
  Pulse,    /* each firing gives one pulse of pulse_ms, however long held */
};

constexpr int kMaxOutputsPerBinding = 4;
constexpr int kMaxModifiers = 2;
struct Binding {
  Input input;
  Input modifiers[kMaxModifiers]; /* also held: chords (Shift+W). A binding
                                     with more modifiers wins over one with
                                     fewer on the same input. */
  uint8_t layer;   /* layer this binding belongs to */
  Gesture gesture;
  Mode mode;
  uint8_t output_count;
  uint8_t turbo_hz;     /* Turbo: 1..30 */
  uint8_t turbo_duty;   /* Turbo: percent on, 10..90 */
  uint16_t tap_ms;      /* Tap: default 200 */
  uint16_t long_ms;     /* LongPress: default 400 */
  uint16_t double_ms;   /* DoubleTap: default 250 */
  uint16_t pulse_ms;    /* Tap, Release, Pulse, wheel: default 60 (>= 2 game
                           frames) */
  Output outputs[kMaxOutputsPerBinding];
};

/* ---- mouse to stick (per layer) ----------------------------------------- */
enum class MouseMode : uint8_t {
  Off = 0,
  Velocity, /* aim / camera: mouse speed → stick deflection (XIM-like) */
  Position, /* steering: mouse travel → stick position, springs back to
               centre at spring_per_s; good for racing with the mouse */
};
enum class DeadzoneShape : uint8_t { Circular = 0, Square = 1 };
struct MouseParams {
  MouseMode mode;
  uint8_t stick;           /* 0 left, 1 right */
  DeadzoneShape deadzone_shape;
  uint8_t invert;          /* bit0: X, bit1: Y */
  uint16_t sensitivity_x100; /* overall, 100 = 1.0 */
  uint16_t yx_ratio_x100;    /* Y relative to X, 100 = same */
  uint16_t exponent_x100;    /* response curve, 100 linear, <100 boosts
                                small moves */
  uint16_t deadzone_x1000;   /* the game's inner deadzone to skip, 0..500 */
  uint16_t outer_x1000;      /* deflection at which output saturates, 500..1000 */
  uint16_t smoothing_ms;     /* speed window, 4..100 */
  uint16_t boost_x100;       /* extra gain on fast flicks, 100 = none */
  uint16_t position_range;   /* Position: mouse counts for full deflection */
  uint16_t spring_per_s_x10; /* Position: return to centre, fraction/s x10 */
  uint16_t reserved;
};

/* Two keys on the same axis at once (A and D). */
enum class Opposing : uint8_t {
  LastWins = 0, /* the newer key wins (SOCD "last") */
  Neutral,      /* both cancel */
  FirstWins,    /* the older key wins */
};

constexpr int kMaxLayers = 8;
struct Layer {
  char name[24];
  MouseParams mouse;          /* mouse_inherit: use the layer below's */
  uint8_t mouse_inherit;
  Opposing opposing;
  uint16_t mouse_digital_threshold; /* counts/s for kMouseMove* inputs */
  uint8_t reserved[4];
};

/* ---- macros ------------------------------------------------------------ */
struct MacroStep {
  uint32_t buttons;  /* held during the step */
  int16_t axes[4];   /* LX LY RX RY, INT16_MIN: untouched */
  uint8_t l2, r2;    /* 0..255 */
  uint16_t duration_ms;
};
constexpr int kMaxMacros = 32;
constexpr int kMaxMacroSteps = 512;
struct Macro {
  uint16_t first_step;
  uint16_t step_count;
  uint8_t repeat_while_held;
  uint8_t reserved[3];
};

/* ---- the compiled profile ---------------------------------------------- */
constexpr uint32_t kBlobMagic = 0x314d424bu; /* "KBM1" */
constexpr uint16_t kBlobVersion = 1;
constexpr int kMaxBindings = 256;

struct ProfileBlob {
  uint32_t magic;
  uint16_t version;
  uint16_t size;            /* sizeof(ProfileBlob) */
  uint8_t enabled;          /* 0: pass the real pad untouched */
  uint8_t layer_count;
  uint8_t macro_count;
  uint8_t reserved0;
  uint16_t binding_count;
  uint16_t macro_step_count;
  Input suspend_key;        /* never remappable; default Pause (0x48) */
  uint16_t trigger_button_threshold; /* 0..255 trigger value that also sets
                                        the L2/R2 bit, default 30 */
  uint8_t merge_real_pad;   /* 1: real controller still works alongside */
  uint8_t reserved1[3];
  Layer layers[kMaxLayers];
  Binding bindings[kMaxBindings];
  Macro macros[kMaxMacros];
  MacroStep macro_steps[kMaxMacroSteps];
};

/* ---- the control page (inside the game) ----------------------------------
 * The agent maps it wherever the game's address space has room (kControlHint
 * is only a hint, never MAP_FIXED over anything) and fills magic,
 * layout_version and page_size last. The daemon finds it by scanning the
 * game's read-write anonymous mappings for kControlMagic + layout_version.
 *
 * Profile delivery is a seqlock: the daemon makes profile_generation odd,
 * writes `profile`, then makes it even (+1 each time). The agent copies the
 * profile only while the generation is even and unchanged across the copy. */
constexpr uint64_t kControlHint = 0x3f0000000ull;
constexpr uint64_t kControlMagic = 0x3243545243424b4dull; /* "MKBCRTC2" */
constexpr uint32_t kControlLayoutVersion = 2;
constexpr int kMaxHookSlots = 16;
/* ControlPage.agent_status */
constexpr uint32_t kStatusRunning = 1u << 0, kStatusHooked = 1u << 1,
                   kStatusMouseOpen = 1u << 2, kStatusKeyboardOpen = 1u << 3;
struct ControlPage {
  uint64_t magic;
  uint32_t layout_version;
  uint32_t page_size;                 /* bytes mapped */
  volatile uint32_t agent_generation; /* a newer agent takes over */
  volatile uint32_t profile_generation; /* seqlock, see above */
  volatile uint32_t agent_status;     /* kStatus* bits */
  volatile uint32_t heartbeat;        /* +1 each reader loop */
  uint32_t agent_build;               /* written by the daemon after it
                                         injects: a checksum of the agent
                                         ELF; it re-injects when its own
                                         embedded agent differs */
  uint32_t reserved0;
  uint64_t hook_slots[kMaxHookSlots]; /* addresses of patched import slots */
  uint64_t hook_originals[kMaxHookSlots];
  uint32_t hook_kinds[kMaxHookSlots]; /* which scePadRead* variant */
  uint32_t hook_count;
  uint32_t reserved[7];
  ProfileBlob profile;
};
static_assert(sizeof(ProfileBlob) < 65536, "ProfileBlob.size is 16-bit");

} // namespace onion::kbm
