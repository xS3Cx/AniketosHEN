/* Copyright (C) 2026 AniketosHEN — Toolbox "Controller Information" page.
 *
 * This file contains no reverse-engineering detail of any kind. It reads the
 * normalized snapshot ControllerManager publishes and renders it. Anything the
 * backend could not prove is shown as explicitly unavailable rather than
 * omitted or guessed, so the page never presents a value we cannot stand
 * behind.
 */
#include "aniketos_shell/controller/controller_toolbox.hpp"

#include "aniketos_shell/controller/controller_manager.hpp"
#include "ps5_settings_ui.hpp"
#include "toolbox_i18n.hpp"

#include <optional>
#include <string>

namespace AniketosHEN::Controller {
namespace {

std::string row(const char *label_key, const std::string &value) {
  return std::string(toolbox_i18n::tr(label_key)) + ": " + value;
}

std::string unavailable() {
  return toolbox_i18n::tr("controller.info.unavailable");
}

const char *class_key(ControllerClass value) {
  /* Only the standard pad has a translated name today. Anything else is
   * reported by its system class number rather than invented wording. */
  return value == ControllerClass::StandardPad ? "controller.info.class.pad"
                                               : "controller.info.unknown";
}

} // namespace

void AppendInformationGroup(ps5ui::Group &g) {
  const ControllerInfo info = GetSnapshot();

  g.group(
      "id_controller_info", toolbox_i18n::tr("controller.info"),
      [info](ps5ui::Group &page) {
        page.label("id_controller_info_source",
                   row("controller.info.source",
                       toolbox_i18n::tr(info.backend_ready
                                            ? "controller.info.source.ok"
                                            : "controller.info.source.fail")),
                   ps5ui::Style::Left);

        if (!info.backend_ready || !info.connected) {
          page.label("id_controller_info_status",
                     row("controller.info.status",
                         toolbox_i18n::tr("controller.info.none")),
                     ps5ui::Style::Left);
          return;
        }

        page.label("id_controller_info_status",
                   row("controller.info.status",
                       toolbox_i18n::tr("controller.info.connected")),
                   ps5ui::Style::Left)
            .label("id_controller_info_count",
                   row("controller.info.count",
                       std::to_string(static_cast<unsigned>(info.connected_count))),
                   ps5ui::Style::Left)
            .label("id_controller_info_class",
                   row("controller.info.class",
                       toolbox_i18n::tr(class_key(info.device_class))),
                   ps5ui::Style::Left);

        if (info.touchpad_state == Availability::Verified) {
          page.label("id_controller_info_touchpad",
                     row("controller.info.touchpad",
                         std::string(toolbox_i18n::tr("controller.info.supported")) +
                             " — " + std::to_string(info.touchpad_width) + " x " +
                             std::to_string(info.touchpad_height)),
                     ps5ui::Style::Left);
        }

        if (info.deadzone_state == Availability::Verified) {
          page.label("id_controller_info_deadzone",
                     row("controller.info.deadzone",
                         std::to_string(static_cast<unsigned>(info.deadzone_left)) +
                             " / " +
                             std::to_string(static_cast<unsigned>(info.deadzone_right))),
                     ps5ui::Style::Left);
        }

        if (info.user_valid) {
          page.label("id_controller_info_user",
                     row("controller.info.user",
                         info.user_name.empty()
                             ? std::to_string(info.user_id)
                             : info.user_name),
                     ps5ui::Style::Left);
        }

        /* connection_type is read successfully but its USB/Bluetooth meaning is
         * not yet proven by a controlled transition, so it is deliberately not
         * rendered here. It stays in the diagnostic log until it is. */

        page.label("id_controller_info_connection",
                   row("controller.info.connection", unavailable()),
                   ps5ui::Style::Left)
            .label("id_controller_info_battery",
                   row("controller.info.battery", unavailable()),
                   ps5ui::Style::Left)
            .label("id_controller_info_charging",
                   row("controller.info.charging", unavailable()),
                   ps5ui::Style::Left)
            .label("id_controller_info_firmware",
                   row("controller.info.firmware", unavailable()),
                   ps5ui::Style::Left)
            .label("id_controller_info_note",
                   toolbox_i18n::tr("controller.info.unavailable.sub"),
                   ps5ui::Style::Left);
      },
      toolbox_i18n::tr("controller.info.sub"), std::nullopt,
      "id_controller_info_source");
}

} // namespace AniketosHEN::Controller
