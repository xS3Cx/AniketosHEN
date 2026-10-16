/* Circular home tiles: Hermes-bytecode edits applied to the HomeUI bundle
 * together with the top-nav patch.
 *
 * Both edits are located by instruction structure and string-table ids, never
 * by function number or file offset, and each is applied only when exactly
 * one site matches; otherwise the tiles keep Sony's rounded-square shape.
 * Included by homeui_top_nav_patch.cpp (same translation unit: it shares
 * HbcView and the bounds helpers). */

static uint16_t read_u16le(const unsigned char *p) {
  return static_cast<uint16_t>(p[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
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

/*
 * Resolve Hermes string IDs from the bytecode's own string table. IDs are
 * deliberately not copied from one firmware: HomeUI rebundles can reorder the
 * table while retaining the same component structure.
 */
static bool find_hbc_string_id(const HbcView &hbc, const char *wanted,
                               uint16_t *out) {
  if (!wanted || !out || hbc.size < 128)
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
  if (string_count > UINT16_MAX)
    return false;

  // Packed BytecodeFileHeader, followed by 32-byte alignment. All supported
  // PS5 HomeUI profiles currently use v89; the layout below also covers the
  // earlier public Hermes layouts from v56 through v96.
  size_t header_size = 64;
  if (version >= 87)
    header_size += 8; // bigint count and storage size
  header_size += 8;  // regexp count and storage size
  header_size += 12; // array/object-key/object-value buffers
  header_size += 8;  // segment id and CommonJS module count
  if (version >= 84)
    header_size += 4; // function-source count
  header_size += 5;  // debug offset and packed options

  size_t cursor = align_up(header_size, 32);
  auto advance = [&](size_t count, size_t stride) -> bool {
    if (count && stride > SIZE_MAX / count)
      return false;
    const size_t bytes = count * stride;
    if (!range_contains(hbc.size, cursor, bytes))
      return false;
    cursor += bytes;
    return true;
  };
  if (cursor == SIZE_MAX || !advance(function_count, 16))
    return false;
  cursor = align_up(cursor, 4);
  if (cursor == SIZE_MAX || !advance(string_kind_count, 4))
    return false;
  cursor = align_up(cursor, 4);
  if (cursor == SIZE_MAX || !advance(identifier_count, 4))
    return false;
  cursor = align_up(cursor, 4);
  if (cursor == SIZE_MAX || !range_contains(hbc.size, cursor,
                                             static_cast<size_t>(string_count) * 4))
    return false;
  const unsigned char *small_table = hbc.data + cursor;
  cursor += static_cast<size_t>(string_count) * 4;
  cursor = align_up(cursor, 4);
  if (cursor == SIZE_MAX || !range_contains(hbc.size, cursor,
                                             static_cast<size_t>(overflow_count) * 8))
    return false;
  const unsigned char *overflow_table = hbc.data + cursor;
  cursor += static_cast<size_t>(overflow_count) * 8;
  cursor = align_up(cursor, 4);
  if (cursor == SIZE_MAX ||
      !range_contains(hbc.size, cursor, string_storage_size))
    return false;
  const unsigned char *storage = hbc.data + cursor;
  const size_t wanted_len = strlen(wanted);

  for (uint32_t id = 0; id < string_count; ++id) {
    const uint32_t entry = read_u32le(small_table + id * 4);
    const bool utf16 = (entry & 1u) != 0;
    uint32_t offset = (entry >> 1) & 0x7fffffu;
    uint32_t length = entry >> 24;
    if (length == 0xffu) {
      if (offset >= overflow_count)
        continue;
      offset = read_u32le(overflow_table + offset * 8);
      length = read_u32le(overflow_table + ((entry >> 1) & 0x7fffffu) * 8 + 4);
    }
    if (utf16 || length != wanted_len || offset > string_storage_size ||
        length > string_storage_size - offset)
      continue;
    if (memcmp(storage + offset, wanted, wanted_len) == 0) {
      *out = static_cast<uint16_t>(id);
      return true;
    }
  }
  return false;
}

struct HomeTileShapePatch {
  unsigned char *icon_radius = nullptr;
  unsigned char *focus_radius = nullptr;
  unsigned char *icon_diameter = nullptr;
  unsigned char *focus_diameter = nullptr;
};

/* The bytes the two radius instructions take for one tile shape. */
struct HomeTileShape {
  unsigned char icon_radius = 0;
  unsigned char focus_radius[8]{};
};

struct HomeTileContentClipPatch {
  unsigned char *tail = nullptr;
  unsigned char *image_width_owner = nullptr;
  unsigned char *image_height_owner = nullptr;
  uint16_t hidden_id = 0;
  uint16_t overflow_id = 0;
  uint16_t relative_id = 0;
  uint16_t position_id = 0;
  uint16_t image_wrapper_id = 0;
  uint16_t image_id = 0;
  uint16_t title_id = 0;
  uint16_t telemetry_container_id = 0;
  unsigned char op_new_object = 0;
  unsigned char op_load_string = 0;
  unsigned char op_put_short = 0;
  unsigned char op_put_long = 0;
  unsigned char op_load_undefined = 0;
  bool tail_clipped = false;
  bool image_style_shared = false;
};

constexpr size_t kTileStyleTailSize = 31;

static void emit_tile_style_tail(unsigned char *out,
                                 const HomeTileContentClipPatch &patch,
                                 bool clipped) {
  // Opcodes are copied from the structurally matched function itself. This
  // keeps the rewrite independent of numeric opcode changes between Hermes
  // bytecode generations.
  size_t at = 0;
  auto byte = [&](unsigned char value) { out[at++] = value; };
  auto word = [&](uint16_t value) {
    write_u16le(out + at, value);
    at += 2;
  };
  if (clipped) {
    // imageWrapper = {borderRadius: BORDER_RADIUS, overflow: 'hidden'}.
    // The wrapper is the common ancestor of artwork, animated mat, Title ID
    // and version, so every internal visual uses the exact same native mask.
    byte(patch.op_load_string);
    byte(5);
    word(patch.hidden_id);
    byte(patch.op_put_long);
    byte(8);
    byte(5);
    word(patch.overflow_id);
    byte(patch.op_new_object);
    byte(1);
    byte(patch.op_put_long);
    byte(1);
    byte(8);
    word(patch.image_wrapper_id);
    byte(patch.op_put_long);
    byte(1);
    // image and imageWrapper deliberately reference the same object. That
    // object contains width, height, the native BORDER_RADIUS and overflow,
    // so the inner Animated.View that paints matStyle receives the mask
    // directly instead of relying on parent clipping (ignored by PS5 PUI).
    byte(8);
    word(patch.image_id);
    byte(patch.op_put_short);
    byte(1);
    byte(6);
    byte(static_cast<unsigned char>(patch.title_id));
    // The removed telemetryContainer style was only position:'relative';
    // relative is React Native's default. Fill its six bytes with harmless
    // LoadConstUndefined instructions so bytecode offsets stay immutable.
    for (int i = 0; i < 3; ++i) {
      byte(patch.op_load_undefined);
      byte(0);
    }
  } else {
    byte(patch.op_new_object);
    byte(5);
    byte(patch.op_load_string);
    byte(1);
    word(patch.relative_id);
    byte(patch.op_put_short);
    byte(5);
    byte(1);
    byte(static_cast<unsigned char>(patch.position_id));
    byte(patch.op_new_object);
    byte(1);
    byte(patch.op_put_long);
    byte(1);
    byte(8);
    word(patch.image_wrapper_id);
    byte(patch.op_put_long);
    byte(1);
    byte(7);
    word(patch.image_id);
    byte(patch.op_put_short);
    byte(1);
    byte(6);
    byte(static_cast<unsigned char>(patch.title_id));
    byte(patch.op_put_long);
    byte(1);
    byte(5);
    word(patch.telemetry_container_id);
  }
}

/*
 * Locate the HomeUI tile stylesheet by instruction structure, not a function
 * number or file offset. The matched module creates imageWrapper from the
 * shared BORDER_RADIUS and image from the same EXPERIENCE_SIZE on both axes.
 */
static bool locate_home_tile_content_clip_patch(
    const HbcView &hbc, size_t file_length, HomeTileContentClipPatch *out) {
  if (!out || file_length > hbc.size)
    return false;

  HomeTileContentClipPatch candidate{};
  if (!find_hbc_string_id(hbc, "hidden", &candidate.hidden_id) ||
      !find_hbc_string_id(hbc, "overflow", &candidate.overflow_id) ||
      !find_hbc_string_id(hbc, "relative", &candidate.relative_id) ||
      !find_hbc_string_id(hbc, "position", &candidate.position_id) ||
      !find_hbc_string_id(hbc, "imageWrapper", &candidate.image_wrapper_id) ||
      !find_hbc_string_id(hbc, "image", &candidate.image_id) ||
      !find_hbc_string_id(hbc, "titleId", &candidate.title_id) ||
      !find_hbc_string_id(hbc, "telemetryContainer",
                          &candidate.telemetry_container_id) ||
      candidate.position_id > UINT8_MAX || candidate.title_id > UINT8_MAX) {
    LOG_WARN("home_tiles: Hermes clip strings unresolved; skip content mask");
    return false;
  }

  unsigned matches = 0;
  for (size_t p = 101; p + 93 < file_length; ++p) {
    const unsigned char *b = hbc.data + p;
    // GetById r6,r5; NewObject r8; PutNewOwnById r8,r6;
    // two equal EXPERIENCE_SIZE lookups; image width/height; title style;
    // telemetryContainer tail followed by StyleSheet.create.
    if (b[1] != 6 || b[2] != 5 || b[6] != 3 || b[7] != 8 ||
        b[9] != 8 || b[10] != 6 || b[14] != 6 || b[15] != 5 ||
        b[20] != 5 || b[21] != 5 ||
        read_u16le(b + 17) != read_u16le(b + 23) || b[25] != 3 ||
        b[26] != 7 || (b[28] != 7 && b[28] != 8) || b[29] != 6 ||
        (b[32] != 7 && b[32] != 8) || b[33] != 5 || b[28] != b[32])
      continue;

    unsigned char stock[kTileStyleTailSize]{};
    unsigned char clipped[kTileStyleTailSize]{};
    unsigned char legacy_clipped[kTileStyleTailSize]{};
    candidate.tail = hbc.data + p + 62;
    candidate.image_width_owner = hbc.data + p + 28;
    candidate.image_height_owner = hbc.data + p + 32;
    candidate.op_new_object = b[6];
    candidate.op_put_long = b[8];
    candidate.op_put_short = b[27];
    candidate.op_load_undefined = b[-39];
    // Stock starts with NewObject,patched starts with LoadConstString.
    candidate.op_load_string = candidate.tail[0] == candidate.op_new_object
                                   ? candidate.tail[2]
                                   : candidate.tail[0];
    emit_tile_style_tail(stock, candidate, false);
    emit_tile_style_tail(clipped, candidate, true);
    memcpy(legacy_clipped, clipped, sizeof(legacy_clipped));
    legacy_clipped[18] = 7; // previous parent-only clipping implementation
    if (memcmp(candidate.tail, stock, sizeof(stock)) == 0) {
      candidate.tail_clipped = false;
    } else if (memcmp(candidate.tail, clipped, sizeof(clipped)) == 0 ||
               memcmp(candidate.tail, legacy_clipped,
                      sizeof(legacy_clipped)) == 0) {
      candidate.tail_clipped = true;
    } else {
      continue;
    }
    candidate.image_style_shared = b[28] == 8;
    ++matches;
  }
  if (matches != 1) {
    LOG_WARN("home_tiles: Tile content-clip signature matches=%u; skip",
             matches);
    return false;
  }
  *out = candidate;
  return true;
}

/*
 * TileItem stores the 106px icon radius and its scaled 168px focus radius in
 * separate instructions. Find that constants module by its unique scale
 * double and verify all surrounding opcode/register bytes before writing.
 * The radii themselves may hold any value an earlier load wrote, so they are
 * only checked to fit their tile.
 */
static bool locate_home_tile_shape_patch(const HbcView &hbc, size_t file_length,
                                         HomeTileShapePatch *out) {
  static constexpr unsigned char kExperienceScale[] = {0x7e, 0x0e, 0xa9, 0x09,
                                                       0xc6, 0x5b, 0xf9, 0x3f};
  constexpr size_t kIconRadiusDelta = 0x4a;
  constexpr size_t kFocusRadiusDelta = 0x79;
  constexpr size_t kIconDiameterBack = 0x12;
  constexpr size_t kFocusDiameterBack = 0x09;

  if (!out || file_length > hbc.size)
    return false;
  HomeTileShapePatch match{};
  unsigned matches = 0;
  for (size_t offset = kIconDiameterBack;
       offset + kFocusRadiusDelta + sizeof(double) <= file_length; ++offset) {
    if (memcmp(hbc.data + offset, kExperienceScale, sizeof(kExperienceScale)) !=
            0 ||
        hbc.data[offset - 2] != 0x6e || hbc.data[offset - 1] != 0x04 ||
        hbc.data[offset + kIconRadiusDelta - 2] != 0x6c ||
        hbc.data[offset + kIconRadiusDelta - 1] != 0x04 ||
        hbc.data[offset + kFocusRadiusDelta - 2] != 0x6e ||
        hbc.data[offset + kFocusRadiusDelta - 1] != 0x01)
      continue;
    unsigned char *icon = hbc.data + offset + kIconRadiusDelta;
    unsigned char *focus = hbc.data + offset + kFocusRadiusDelta;
    const unsigned icon_diameter = hbc.data[offset - kIconDiameterBack];
    const unsigned focus_diameter = hbc.data[offset - kFocusDiameterBack];
    double focus_radius = 0.0;
    memcpy(&focus_radius, focus, sizeof(focus_radius));
    if (icon_diameter == 0 || focus_diameter == 0 ||
        *icon > icon_diameter / 2 || !(focus_radius >= 0.0) ||
        focus_radius > focus_diameter / 2.0)
      continue;
    match.icon_radius = icon;
    match.focus_radius = focus;
    match.icon_diameter = hbc.data + offset - kIconDiameterBack;
    match.focus_diameter = hbc.data + offset - kFocusDiameterBack;
    ++matches;
  }
  if (matches != 1) {
    LOG_WARN("home_tiles: TileItem radius signature matches=%u; skip", matches);
    return false;
  }
  *out = match;
  return true;
}

/* The radii for a rounding of `percent` of a full circle, or Sony's 16px
   corners for a negative one. The focus radius follows the icon radius by
   the module's own focus/icon scale, which is how Sony derives it. */
static HomeTileShape home_tile_shape_for(const HomeTileShapePatch &patch,
                                         int percent) {
  static constexpr unsigned char kStockFocusRadius[] = {0x7e, 0x0e, 0xa9, 0x09,
                                                        0xc6, 0x5b, 0x39, 0x40};
  HomeTileShape shape{};
  if (percent < 0) {
    shape.icon_radius = 16;
    memcpy(shape.focus_radius, kStockFocusRadius, sizeof(kStockFocusRadius));
    return shape;
  }
  const unsigned icon_diameter = *patch.icon_diameter;
  const double focus_diameter = *patch.focus_diameter;
  shape.icon_radius = static_cast<unsigned char>(
      std::lround(std::min(percent, 100) * (icon_diameter / 2) / 100.0));
  const double focus_radius =
      shape.icon_radius * (focus_diameter / static_cast<double>(icon_diameter));
  memcpy(shape.focus_radius, &focus_radius, sizeof(focus_radius));
  return shape;
}

static bool home_tile_shape_is(const HomeTileShapePatch &patch,
                               const HomeTileShape &shape) {
  return *patch.icon_radius == shape.icon_radius &&
         memcmp(patch.focus_radius, shape.focus_radius,
                sizeof(shape.focus_radius)) == 0;
}

static void apply_home_tile_shape_patch(const HomeTileShapePatch &patch,
                                        const HomeTileShape &shape) {
  *patch.icon_radius = shape.icon_radius;
  memcpy(patch.focus_radius, shape.focus_radius, sizeof(shape.focus_radius));
}

static void apply_home_tile_content_clip_patch(
    const HomeTileContentClipPatch &patch, bool clipped) {
  unsigned char replacement[kTileStyleTailSize]{};
  emit_tile_style_tail(replacement, patch, clipped);
  *patch.image_width_owner = clipped ? 8 : 7;
  *patch.image_height_owner = clipped ? 8 : 7;
  memcpy(patch.tail, replacement, sizeof(replacement));
}
