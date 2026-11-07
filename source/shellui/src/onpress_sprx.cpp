#include "onpress.hpp"

#include "external_sprx_ui.hpp"
#include "settings_page_refresh.hpp"
#include "toolbox_i18n.hpp"

namespace {

OnPressResult external_sprx_control(OnPressContext &ctx) {
  ctx.dirty = false;
  const onion::shellui::external_sprx::DispatchResult result =
      onion::shellui::external_sprx::dispatch(ctx.id, ctx.value);
  if (!result.owned) return OnPressResult::NotMine;
  if (result.success) return OnPressResult::Consumed;
  /* Not written: the page shows the switches as they really are. */
  onion::shellui::settings::refresh(ctx.instance);
  notify("%s",
         toolbox_i18n::format("sprx.operation_failed_fmt", result.id.c_str())
             .c_str());
  return OnPressResult::Consumed;
}

const OnPressPrefixEntry kSprxPrefix[] = {
    {"id_external_sprx_", external_sprx_control},
};

} // namespace

const OnPressPrefixEntry *onpress_sprx_prefix(size_t *count) {
  *count = sizeof(kSprxPrefix) / sizeof(kSprxPrefix[0]);
  return kSprxPrefix;
}
