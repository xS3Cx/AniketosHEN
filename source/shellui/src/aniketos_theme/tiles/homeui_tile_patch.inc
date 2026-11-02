/* Rounded Home tiles: Hermes-bytecode edits applied to the HomeUI bundle
 * next to the top-nav patch, on any firmware whose HomeUI is Hermes bytecode.
 *
 * Nothing here belongs to one firmware. Both sites are found by the export
 * and style names their instructions use (looked up in the bundle's own
 * string table), by the registers those instructions pass between them and
 * by values that must agree with each other; the opcodes are learned from
 * the matched instructions themselves. No file offset, function number,
 * register number or bytecode version's opcode numbering is assumed, so a
 * rebuilt HomeUI is still found (13.x added a style to the tile stylesheet,
 * which moved every register). Each edit is applied only when exactly one
 * site matches; otherwise the tiles keep Sony's shape. Included by
 * homeui_top_nav_patch.cpp (same translation unit: it shares HbcView and the
 * bounds helpers). */

static uint16_t read_u16le(const unsigned char *p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

static void write_u16le(unsigned char *p, uint16_t value) {
  p[0] = static_cast<unsigned char>(value & 0xff);
  p[1] = static_cast<unsigned char>(value >> 8);
}

static size_t align_up(size_t value, size_t alignment) {
  if (!alignment || value > SIZE_MAX - (alignment - 1))
    return SIZE_MAX;
  return (value + alignment - 1) & ~(alignment - 1);
}

/* The bundle's string table: string id i is entry i. */
struct HbcStringTable {
  const unsigned char *entries = nullptr;
  const unsigned char *overflow = nullptr;
  const unsigned char *storage = nullptr;
  uint32_t count = 0;
  uint32_t overflow_count = 0;
  uint32_t storage_size = 0;
};

static bool read_hbc_string_table(const HbcView &hbc, HbcStringTable *out) {
  if (hbc.size < 128)
    return false;
  const uint32_t version = read_u32le(hbc.data + kHbcVersionOffset);
  if (version < 56 || version >= 97)
    return false;

  const uint32_t function_count = read_u32le(hbc.data + 40);
  const uint32_t string_kind_count = read_u32le(hbc.data + 44);
  const uint32_t identifier_count = read_u32le(hbc.data + 48);
  const uint32_t string_count = read_u32le(hbc.data + 52);
  const uint32_t overflow_count = read_u32le(hbc.data + 56);
  const uint32_t string_storage_size = read_u32le(hbc.data + 60);

  // Packed BytecodeFileHeader, followed by 32-byte alignment. Every PS5
  // HomeUI seen so far is v89; the layout below also covers the earlier
  // public Hermes layouts from v56 through v96.
  size_t header_size = 64;
  if (version >= 87)
    header_size += 8; // bigint count and storage size
  header_size += 8;  // regexp count and storage size
  header_size += 12; // array/object-key/object-value buffers
  header_size += 8;  // segment id and CommonJS module count
  if (version >= 84)
    header_size += 4; // function-source count
  header_size += 5;  // debug offset and packed options

  /* Each table follows the last one, 4-aligned; null when it does not fit. */
  size_t cursor = align_up(header_size, 32);
  auto next_table = [&](size_t count, size_t stride) -> const unsigned char * {
    cursor = align_up(cursor, 4);
    if (cursor == SIZE_MAX || (count && stride > SIZE_MAX / count) ||
        !range_contains(hbc.size, cursor, count * stride))
      return nullptr;
    const unsigned char *table = hbc.data + cursor;
    cursor += count * stride;
    return table;
  };
  if (!next_table(function_count, 16) || !next_table(string_kind_count, 4) ||
      !next_table(identifier_count, 4))
    return false;
  out->entries = next_table(string_count, 4);
  out->overflow = next_table(overflow_count, 8);
  out->storage = next_table(string_storage_size, 1);
  out->count = string_count;
  out->overflow_count = overflow_count;
  out->storage_size = string_storage_size;
  return out->entries && out->overflow && out->storage;
}

/* The id of an ASCII string. Only ids a 16-bit operand can hold are looked
   at: the instructions matched below never use wider ones. */
static bool hbc_string_id(const HbcStringTable &table, const char *wanted,
                          uint16_t *out) {
  const size_t wanted_len = strlen(wanted);
  const uint32_t count = std::min<uint32_t>(table.count, UINT16_MAX + 1u);
  for (uint32_t id = 0; id < count; ++id) {
    const uint32_t entry = read_u32le(table.entries + id * 4);
    if (entry & 1u)
      continue; // UTF-16
    uint32_t offset = (entry >> 1) & 0x7fffffu;
    uint32_t length = entry >> 24;
    if (length == 0xffu) {
      if (offset >= table.overflow_count)
        continue;
      length = read_u32le(table.overflow + offset * 8 + 4);
      offset = read_u32le(table.overflow + offset * 8);
    }
    if (length != wanted_len || offset > table.storage_size ||
        length > table.storage_size - offset)
      continue;
    if (memcmp(table.storage + offset, wanted, wanted_len) == 0) {
      *out = static_cast<uint16_t>(id);
      return true;
    }
  }
  return false;
}

/* The names both sites are found by, as string ids of this bundle. */
struct HomeTileNames {
  /* TileItem's constants module exports */
  uint16_t experience_size = 0;
  uint16_t scaled_exp_size = 0;
  uint16_t experience_scale = 0;
  uint16_t border_radius_export = 0;
  /* style properties and stylesheet keys */
  uint16_t border_radius = 0;
  uint16_t focus_container = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t position = 0;
  uint16_t overflow = 0;
  uint16_t image_wrapper = 0;
  uint16_t image = 0;
  uint16_t telemetry_container = 0;
  /* style values */
  uint16_t relative = 0;
  uint16_t hidden = 0;
};

/* Whether the bundle is HomeUI's (only it names its own title id and the
   tile constants), and the ids of every name below. */
static bool resolve_home_tile_names(const HbcView &hbc, HomeTileNames *names) {
  HbcStringTable table{};
  uint16_t title = 0;
  if (!read_hbc_string_table(hbc, &table) ||
      !hbc_string_id(table, "NPXS40002", &title) ||
      !hbc_string_id(table, "EXPERIENCE_SCALE", &names->experience_scale))
    return false;
  const struct {
    const char *text;
    uint16_t *id;
  } wanted[] = {
      {"EXPERIENCE_SIZE", &names->experience_size},
      {"SCALED_EXP_SIZE", &names->scaled_exp_size},
      {"BORDER_RADIUS", &names->border_radius_export},
      {"borderRadius", &names->border_radius},
      {"focusContainer", &names->focus_container},
      {"width", &names->width},
      {"height", &names->height},
      {"position", &names->position},
      {"overflow", &names->overflow},
      {"imageWrapper", &names->image_wrapper},
      {"image", &names->image},
      {"telemetryContainer", &names->telemetry_container},
      {"relative", &names->relative},
      {"hidden", &names->hidden},
  };
  for (const auto &name : wanted) {
    if (!hbc_string_id(table, name.text, name.id)) {
      LOG_WARN("home_tiles: HomeUI has no string '%s'; tiles keep their shape",
               name.text);
      return false;
    }
  }
  return true;
}

/* An opcode learned from the first instruction of its kind a site matched;
   every later instruction of that kind in the site must carry the same one. */
struct LearnedOpcode {
  int value = -1;
  bool take(unsigned char op) {
    if (value < 0)
      value = op;
    return value == op;
  }
  bool is(unsigned char op) const { return value == op; }
};

/* Instructions that name a property come in a u8-id ("Short") and a u16-id
   form. Hermes always emits the narrowest form that holds the id, and each
   form has its own opcode. */
struct LearnedNamedOpcode {
  LearnedOpcode narrow;
  LearnedOpcode wide;
  LearnedOpcode &form(uint16_t id) { return id <= UINT8_MAX ? narrow : wide; }
};

static size_t id_width(uint16_t id) { return id <= UINT8_MAX ? 1 : 2; }

static uint16_t read_id(const unsigned char *p, size_t width) {
  return width == 1 ? p[0] : read_u16le(p);
}

static bool room(const unsigned char *p, const unsigned char *end, size_t n) {
  return p <= end && static_cast<size_t>(end - p) >= n;
}

/* Each matcher below returns the instruction's length, or 0 when the bytes
   at `p` are not that instruction. An opcode is learned only on a match. */

/* NewObject dst */
static size_t new_object_at(const unsigned char *p, const unsigned char *end,
                            LearnedOpcode &op, unsigned char *dst) {
  LearnedOpcode learned = op;
  if (!room(p, end, 2) || !learned.take(p[0]))
    return 0;
  op = learned;
  *dst = p[1];
  return 2;
}

/* GetById(Short) dst, object, cache, id */
static size_t get_by_id_at(const unsigned char *p, const unsigned char *end,
                           uint16_t id, LearnedNamedOpcode &op,
                           unsigned char *dst, unsigned char *object) {
  const size_t width = id_width(id);
  LearnedOpcode learned = op.form(id);
  if (!room(p, end, 4 + width) || read_id(p + 4, width) != id ||
      !learned.take(p[0]))
    return 0;
  op.form(id) = learned;
  *dst = p[1];
  *object = p[2];
  return 4 + width;
}

/* PutNewOwnById(Short) object, value, id */
static size_t new_own_at(const unsigned char *p, const unsigned char *end,
                         uint16_t id, LearnedNamedOpcode &op,
                         unsigned char *object, unsigned char *value) {
  const size_t width = id_width(id);
  LearnedOpcode learned = op.form(id);
  if (!room(p, end, 3 + width) || read_id(p + 3, width) != id ||
      !learned.take(p[0]))
    return 0;
  op.form(id) = learned;
  *object = p[1];
  *value = p[2];
  return 3 + width;
}

/* Any PutNewOwnById(Short) on `object`, in a form already learned. */
static size_t any_new_own_at(const unsigned char *p, const unsigned char *end,
                             const LearnedNamedOpcode &op, unsigned char object,
                             unsigned char *value) {
  if (!room(p, end, 1))
    return 0;
  const size_t width = op.narrow.is(p[0]) ? 1 : op.wide.is(p[0]) ? 2 : 0;
  if (!width || !room(p, end, 3 + width) || p[1] != object)
    return 0;
  *value = p[2];
  return 3 + width;
}

/* LoadConstString dst, id */
static size_t load_string_at(const unsigned char *p, const unsigned char *end,
                             uint16_t id, LearnedOpcode &op,
                             unsigned char *dst) {
  LearnedOpcode learned = op;
  if (!room(p, end, 4) || read_u16le(p + 2) != id || !learned.take(p[0]))
    return 0;
  op = learned;
  *dst = p[1];
  return 4;
}

/* One export of TileItem's constants module, `exports.ID = <constant>`:
   LoadConstUInt8|LoadConstDouble rV, constant; PutById rExports, rV, cache,
   ID. Returns the constant's address, or null. */
static unsigned char *export_at(unsigned char *p, const unsigned char *end,
                                uint16_t id, size_t constant_size,
                                LearnedOpcode &load, LearnedOpcode &put,
                                int &exports) {
  const size_t store = 2 + constant_size;
  LearnedOpcode learned_load = load;
  LearnedOpcode learned_put = put;
  if (!room(p, end, store + 6) || read_u16le(p + store + 4) != id ||
      p[store + 2] != p[1] || (exports >= 0 && p[store + 1] != exports) ||
      !learned_load.take(p[0]) || !learned_put.take(p[store]) ||
      learned_load.value == learned_put.value)
    return nullptr;
  load = learned_load;
  put = learned_put;
  exports = p[store + 1];
  return p + 2;
}

static double read_double(const unsigned char *p) {
  double value = 0.0;
  memcpy(&value, p, sizeof(value));
  return value;
}

/* How far from EXPERIENCE_SIZE's export the rest of TileItem's constants
   module, and the tile stylesheet's keys from its image style, may lie. */
constexpr size_t kTileConstantsWindow = 0x400;
constexpr size_t kTileStylesWindow = 0x100;

struct HomeTileShapePatch {
  unsigned char *icon_radius = nullptr;  /* BORDER_RADIUS's u8 */
  unsigned char *focus_radius = nullptr; /* the focus frame's double */
  unsigned icon_diameter = 0;            /* EXPERIENCE_SIZE */
  unsigned focus_diameter = 0;           /* SCALED_EXP_SIZE */
};

/* What the two radius instructions take for one tile shape. */
struct HomeTileShape {
  unsigned char icon_radius = 0;
  double focus_radius = 0.0;
};

/*
 * TileItem's constants module exports the icon size (EXPERIENCE_SIZE, 106 px)
 * and its focused size (SCALED_EXP_SIZE, 168 px), their ratio
 * (EXPERIENCE_SCALE) and the icon's corner (BORDER_RADIUS, 16 px); the same
 * function builds the focus frame's style, whose borderRadius is that corner
 * scaled (25.36 px) and which goes into the stylesheet as focusContainer.
 * The site is the one set of those instructions where the ratio equals the
 * two sizes' and the focus corner equals the scaled icon corner. The corners
 * may hold any value an earlier load wrote, so they are only checked to fit.
 */
static bool match_tile_constants(unsigned char *p, unsigned char *begin,
                                 unsigned char *end, const HomeTileNames &n,
                                 HomeTileShapePatch *out) {
  LearnedOpcode load_u8, load_double, put_by_id;
  int exports = -1;
  unsigned char *icon_size =
      export_at(p, end, n.experience_size, 1, load_u8, put_by_id, exports);
  if (!icon_size || *icon_size == 0)
    return false;
  unsigned char *lo = p - begin > static_cast<ptrdiff_t>(kTileConstantsWindow)
                          ? p - kTileConstantsWindow
                          : begin;
  unsigned char *hi = end - p > static_cast<ptrdiff_t>(kTileConstantsWindow)
                          ? p + kTileConstantsWindow
                          : end;
  /* The u8 exports reuse the anchor's opcodes: nothing new is learned. */
  auto unique_u8 = [&](uint16_t id) -> unsigned char * {
    unsigned char *found = nullptr;
    for (unsigned char *q = lo; q < hi; ++q) {
      unsigned char *constant =
          export_at(q, end, id, 1, load_u8, put_by_id, exports);
      if (!constant)
        continue;
      if (found)
        return nullptr;
      found = constant;
    }
    return found;
  };
  unsigned char *focus_size = unique_u8(n.scaled_exp_size);
  unsigned char *icon_radius = unique_u8(n.border_radius_export);
  if (!focus_size || *focus_size == 0 || !icon_radius ||
      *icon_radius > *icon_size / 2)
    return false;
  const double scale = static_cast<double>(*focus_size) / *icon_size;

  unsigned char *scale_constant = nullptr;
  for (unsigned char *q = lo; q < hi; ++q) {
    LearnedOpcode load = load_double;
    unsigned char *constant =
        export_at(q, end, n.experience_scale, 8, load, put_by_id, exports);
    if (!constant || std::fabs(read_double(constant) - scale) > 1e-9)
      continue;
    if (scale_constant)
      return false;
    scale_constant = constant;
    load_double = load;
  }
  if (!scale_constant)
    return false;

  /* The focus frame: LoadConstDouble rF, corner; PutNewOwnById rS, rF,
     borderRadius; ... PutNewOwnById rStyles, rS, focusContainer. */
  unsigned char *focus_radius = nullptr;
  for (unsigned char *q = lo; q < hi; ++q) {
    if (!room(q, end, 10) || !load_double.is(q[0]))
      continue;
    LearnedNamedOpcode new_own;
    unsigned char style = 0, value = 0, styles = 0;
    const size_t put =
        new_own_at(q + 10, end, n.border_radius, new_own, &style, &value);
    if (!put || value != q[1])
      continue;
    bool keyed = false;
    for (unsigned char *k = q + 10 + put;
         !keyed && k < q + 10 + put + kTileStylesWindow && k < end; ++k) {
      LearnedNamedOpcode learned = new_own;
      keyed = new_own_at(k, end, n.focus_container, learned, &styles,
                         &value) &&
              value == style;
    }
    const double radius = read_double(q + 2);
    if (!keyed || !(radius >= 0.0) || radius > *focus_size / 2.0 ||
        std::fabs(radius - *icon_radius * scale) > 0.5)
      continue;
    if (focus_radius)
      return false;
    focus_radius = q + 2;
  }
  if (!focus_radius)
    return false;

  out->icon_radius = icon_radius;
  out->focus_radius = focus_radius;
  out->icon_diameter = *icon_size;
  out->focus_diameter = *focus_size;
  return true;
}

static bool locate_home_tile_shape_patch(const HbcView &hbc, size_t file_length,
                                         const HomeTileNames &names,
                                         HomeTileShapePatch *out) {
  if (file_length > hbc.size)
    return false;
  unsigned char *const begin = hbc.data;
  unsigned char *const end = hbc.data + file_length;
  HomeTileShapePatch match{};
  unsigned matches = 0;
  for (unsigned char *p = begin; p + 9 <= end; ++p) {
    /* cheap first test: EXPERIENCE_SIZE as a PutById's id */
    if (read_u16le(p + 7) != names.experience_size)
      continue;
    HomeTileShapePatch site{};
    if (match_tile_constants(p, begin, end, names, &site)) {
      match = site;
      ++matches;
    }
  }
  if (matches != 1) {
    LOG_WARN("home_tiles: TileItem radius signature matches=%u; skip", matches);
    return false;
  }
  *out = match;
  return true;
}

/* The radii for a rounding of `percent` (0..100) of a full circle. The focus
   radius follows the icon radius by the module's own focus/icon scale, which
   is how Sony derives it. Sony's shape needs no edit: every load hands over
   a freshly decrypted bundle. */
static HomeTileShape home_tile_shape_for(const HomeTileShapePatch &patch,
                                         int percent) {
  HomeTileShape shape{};
  shape.icon_radius = static_cast<unsigned char>(
      std::lround(percent * (patch.icon_diameter / 2) / 100.0));
  const double focus_scale =
      patch.focus_diameter / static_cast<double>(patch.icon_diameter);
  shape.focus_radius = shape.icon_radius * focus_scale;
  return shape;
}

/* The longest stretch of stylesheet keys the clip edit rewrites. */
constexpr size_t kTileStylesMax = 96;

struct HomeTileContentClipPatch {
  unsigned char *width_owner = nullptr;  /* image style's width put */
  unsigned char *height_owner = nullptr; /* image style's height put */
  unsigned char *styles = nullptr; /* telemetryContainer style .. last key */
  size_t styles_size = 0;
  unsigned char wrapper = 0; /* imageWrapper style's register */
  unsigned char clipped[kTileStylesMax]{};
};

/*
 * The tile stylesheet: imageWrapper = {borderRadius: BORDER_RADIUS}, image =
 * {width: EXPERIENCE_SIZE, height: EXPERIENCE_SIZE}, a few text styles, and
 * telemetryContainer = {position: 'relative'}, then the object of keys
 * handed to StyleSheet.create. The edit makes imageWrapper and image one
 * object holding width, height, the corner and overflow: 'hidden', so the
 * inner Animated.View that paints the artwork gets the mask itself (PS5 PUI
 * ignores a parent's clipping), and drops telemetryContainer (only
 * position: 'relative', React Native's default) to make room. The bytecode
 * keeps its length: the rest is filled with NewObject into the freed
 * register. Only Sony's original layout is matched; a reload always hands
 * over a freshly decrypted bundle.
 */
static bool match_tile_styles(unsigned char *p, unsigned char *end,
                              const HomeTileNames &n,
                              HomeTileContentClipPatch *out) {
  LearnedOpcode new_object, load_string;
  LearnedNamedOpcode get_by_id, new_own;
  unsigned char radius = 0, constants = 0, wrapper = 0, width = 0, height = 0;
  unsigned char image = 0, object = 0, value = 0;
  size_t len = 0;

  /* imageWrapper: {borderRadius: TileItem.BORDER_RADIUS} */
  if (!(len = get_by_id_at(p, end, n.border_radius_export, get_by_id, &radius,
                           &constants)))
    return false;
  p += len;
  if (!(len = new_object_at(p, end, new_object, &wrapper)))
    return false;
  p += len;
  if (!(len = new_own_at(p, end, n.border_radius, new_own, &object, &value)) ||
      object != wrapper || value != radius)
    return false;
  p += len;
  /* image: {width: EXPERIENCE_SIZE, height: EXPERIENCE_SIZE} */
  if (!(len = get_by_id_at(p, end, n.experience_size, get_by_id, &width,
                           &object)) ||
      object != constants)
    return false;
  p += len;
  if (!(len = get_by_id_at(p, end, n.experience_size, get_by_id, &height,
                           &object)) ||
      object != constants)
    return false;
  p += len;
  if (!(len = new_object_at(p, end, new_object, &image)) || image == wrapper)
    return false;
  p += len;
  unsigned char *width_owner = p + 1;
  if (!(len = new_own_at(p, end, n.width, new_own, &object, &value)) ||
      object != image || value != width)
    return false;
  p += len;
  unsigned char *height_owner = p + 1;
  if (!(len = new_own_at(p, end, n.height, new_own, &object, &value)) ||
      object != image || value != height)
    return false;
  p += len;

  /* telemetryContainer: {position: 'relative'}, then the keys object. */
  for (unsigned char *t = p; t < p + kTileStylesWindow && t < end; ++t) {
    LearnedOpcode objects = new_object;
    LearnedOpcode strings = load_string;
    LearnedNamedOpcode puts = new_own;
    unsigned char telemetry = 0, text = 0, keys = 0;
    unsigned char *q = t;
    if (!(len = new_object_at(q, end, objects, &telemetry)))
      continue;
    q += len;
    if (!(len = load_string_at(q, end, n.relative, strings, &text)))
      continue;
    q += len;
    if (!(len = new_own_at(q, end, n.position, puts, &object, &value)) ||
        object != telemetry || value != text)
      continue;
    q += len;
    if (!(len = new_object_at(q, end, objects, &keys)))
      continue;
    q += len;

    /* The keys: each one PutNewOwnById keys, style, name. Gather them,
       minus telemetryContainer, with image pointing at imageWrapper's
       style. */
    unsigned char kept[kTileStylesMax];
    size_t kept_size = 0;
    bool has_wrapper = false, has_image = false, has_telemetry = false;
    while (q < end) {
      uint16_t key = 0;
      if ((len = new_own_at(q, end, n.image_wrapper, puts, &object, &value)))
        key = n.image_wrapper;
      else if ((len = new_own_at(q, end, n.image, puts, &object, &value)))
        key = n.image;
      else if ((len = new_own_at(q, end, n.telemetry_container, puts, &object,
                                 &value)))
        key = n.telemetry_container;
      else if (!(len = any_new_own_at(q, end, puts, keys, &value)))
        break;
      else
        object = keys;
      if (object != keys || kept_size + len > sizeof(kept))
        break;
      if (key == n.telemetry_container) {
        if (value != telemetry || has_telemetry)
          break;
        has_telemetry = true;
      } else {
        if (value == telemetry)
          break;
        memcpy(kept + kept_size, q, len);
        if (key == n.image_wrapper) {
          if (value != wrapper || has_wrapper)
            break;
          has_wrapper = true;
        } else if (key == n.image) {
          if (value != image || has_image)
            break;
          has_image = true;
          kept[kept_size + 2] = wrapper;
        }
        kept_size += len;
      }
      q += len;
    }
    if (!has_wrapper || !has_image || !has_telemetry)
      continue;

    /* The rewrite: overflow: 'hidden' on imageWrapper's style (through the
       freed telemetry register), the keys object and its keys, then
       filler. Either put form of overflow may be needed to make the filler
       a whole number of NewObjects. */
    const size_t styles_size = static_cast<size_t>(q - t);
    if (styles_size > kTileStylesMax)
      continue;
    for (const size_t width_of_id : {size_t{2}, size_t{1}}) {
      const LearnedOpcode &put = width_of_id == 1 ? puts.narrow : puts.wide;
      if (put.value < 0 || (width_of_id == 1 && n.overflow > UINT8_MAX))
        continue;
      const size_t used = 4 + 3 + width_of_id + 2 + kept_size;
      if (used > styles_size || (styles_size - used) % 2 != 0)
        continue;
      unsigned char *w = out->clipped;
      *w++ = static_cast<unsigned char>(strings.value);
      *w++ = telemetry;
      write_u16le(w, n.hidden);
      w += 2;
      *w++ = static_cast<unsigned char>(put.value);
      *w++ = wrapper;
      *w++ = telemetry;
      if (width_of_id == 1) {
        *w++ = static_cast<unsigned char>(n.overflow);
      } else {
        write_u16le(w, n.overflow);
        w += 2;
      }
      *w++ = static_cast<unsigned char>(objects.value);
      *w++ = keys;
      memcpy(w, kept, kept_size);
      w += kept_size;
      while (w < out->clipped + styles_size) {
        *w++ = static_cast<unsigned char>(objects.value);
        *w++ = telemetry;
      }
      out->width_owner = width_owner;
      out->height_owner = height_owner;
      out->styles = t;
      out->styles_size = styles_size;
      out->wrapper = wrapper;
      return true;
    }
  }
  return false;
}

static bool locate_home_tile_content_clip_patch(
    const HbcView &hbc, size_t file_length, const HomeTileNames &names,
    HomeTileContentClipPatch *out) {
  if (file_length > hbc.size)
    return false;
  unsigned char *const end = hbc.data + file_length;
  const size_t width = id_width(names.border_radius_export);
  HomeTileContentClipPatch match{};
  unsigned matches = 0;
  for (unsigned char *p = hbc.data; p + 4 + width <= end; ++p) {
    /* cheap first test: BORDER_RADIUS as a GetById's id */
    if (read_id(p + 4, width) != names.border_radius_export)
      continue;
    HomeTileContentClipPatch site{};
    if (match_tile_styles(p, end, names, &site)) {
      match = site;
      ++matches;
    }
  }
  if (matches != 1) {
    LOG_WARN("home_tiles: Tile content-clip signature matches=%u; skip",
             matches);
    return false;
  }
  *out = match;
  return true;
}

/* Sony's focus-frame corner: the 16 px icon corner scaled to the 168 px
   focused tile. */
constexpr double kSonyTileFocusRadius = 16.0 * 168.0 / 106.0;

struct HomeTilePlan {
  HomeTileShapePatch shape;
  HomeTileContentClipPatch clip;
  HomeTileShape wanted;
  /* The clip site only matches Sony's layout, so finding it means the edit
     is still to be made, even for a rounding whose corner equals Sony's. */
  bool round = false;
};

/* Rounded tiles: the user's rounding, up to a full circle, with the artwork
   clipped to it. A rounded frame around square artwork would look broken, so
   the tiles change only when both sites are found. Writes nothing into the
   bundle; publishes what the bundle's tiles will end up with. */
static void plan_home_tiles(const HbcView &hbc, size_t file_length,
                            const HomeTileNames &names, HomeTilePlan *plan) {
  const int rounding = AniketosTheme::TileRoundingPercent();
  const bool shape_ready =
      locate_home_tile_shape_patch(hbc, file_length, names, &plan->shape);
  const bool clip_ready =
      locate_home_tile_content_clip_patch(hbc, file_length, names, &plan->clip);
  plan->round = rounding >= 0 && shape_ready && clip_ready;
  if (plan->round)
    plan->wanted = home_tile_shape_for(plan->shape, rounding);
  LOG_INFO("home_tiles: rounding=%d shape_site=%d (radius now %d, wanted "
           "%d) clip_site=%d",
           rounding, shape_ready ? 1 : 0,
           shape_ready ? static_cast<int>(*plan->shape.icon_radius) : -1,
           plan->round ? static_cast<int>(plan->wanted.icon_radius) : -1,
           clip_ready ? 1 : 0);
  /* The focus outline and the arc labels (home_tiles.cpp) follow the tiles
     this bundle ends up with. */
  homeui_publish_tiles(plan->round ? rounding : -1,
                       plan->round   ? plan->wanted.focus_radius
                       : shape_ready ? read_double(plan->shape.focus_radius)
                                     : kSonyTileFocusRadius);
}

static void apply_home_tile_plan(const HomeTilePlan &plan) {
  *plan.shape.icon_radius = plan.wanted.icon_radius;
  memcpy(plan.shape.focus_radius, &plan.wanted.focus_radius,
         sizeof(plan.wanted.focus_radius));
  *plan.clip.width_owner = plan.clip.wrapper;
  *plan.clip.height_owner = plan.clip.wrapper;
  memcpy(plan.clip.styles, plan.clip.clipped, plan.clip.styles_size);
  LOG_INFO("home_tiles: shape radius=%u and content mask applied",
           static_cast<unsigned>(plan.wanted.icon_radius));
}
