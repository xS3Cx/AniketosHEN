#include "aniketos_shell/system_ui/migration_notice.hpp"

#include "aniketos_shell/system_ui/system_dialogs.hpp"
#include "aniketos_theme/clock/shell_clock.hpp"
#include "toolbox_i18n.hpp"

#include <onion/distribution.h>
#include <onion/fs.h>
#include <onion/log.h>

#include <string>

namespace MigrationNotice {
namespace {

/* Left by the migration when it copied an OnionHEN root (see migrate.c). */
constexpr const char *kMigrated[] = {
    ONION_DIST_USER_DATA_ROOT "/" ONION_DIST_MIGRATION_MARKER,
    ONION_DIST_DATA_ROOT "/" ONION_DIST_MIGRATION_MARKER,
};
/* Ours: the user has read the notice. */
constexpr const char *kSeen = ONION_DIST_USER_DATA_ROOT "/.migration_notice_seen";

enum class State { Check, WaitForHome, Showing, Done };
State s_state = State::Check;
int s_home_frames = 0;
int s_attempts = 0;

bool show() {
  SystemDialogs::AlertOptions notice;
  notice.style = SystemDialogs::AlertStyle::Popup;
  notice.title = toolbox_i18n::tr("migration_notice.title");
  notice.message = toolbox_i18n::tr("migration_notice.body");
  notice.positive = toolbox_i18n::tr("migration_notice.ok");
  std::string failure;
  const bool shown = SystemDialogs::ShowAlert(notice, &failure);
  LOG_INFO("[MIGRATION-NOTICE] show=%d %s", shown ? 1 : 0, failure.c_str());
  return shown;
}

} // namespace

void Tick() {
  switch (s_state) {
  case State::Check: {
    bool migrated = false;
    for (const char *marker : kMigrated)
      migrated = migrated || if_exists(marker);
    s_state = migrated && !if_exists(kSeen) ? State::WaitForHome : State::Done;
    break;
  }
  case State::WaitForHome: {
    /* Not during boot: wait until Home has been on screen for a few seconds,
       so the pop-up opens on a scene that is there to be seen. */
    float opacity = 0.0f;
    const bool home_visible = ShellClock::GetHomeRoot() &&
                              ShellClock::GetNativeClockOpacity(&opacity) &&
                              opacity > 0.95f;
    s_home_frames = home_visible ? s_home_frames + 1 : 0;
    if (s_home_frames < 240)
      break;
    s_home_frames = 0;
    if (show())
      s_state = State::Showing;
    else if (++s_attempts >= 3)
      s_state = State::Done; /* this session; asked again next boot */
    break;
  }
  case State::Showing: {
    int button = -1;
    if (!SystemDialogs::PollAlert(&button))
      break;
    LOG_INFO("[MIGRATION-NOTICE] closed button=%d", button);
    /* Only pressing the button counts as read; backing out of the pop-up
       shows it again on the next start. */
    if (button == 0)
      touch_file(kSeen);
    s_state = State::Done;
    break;
  }
  case State::Done:
    break;
  }
}

} // namespace MigrationNotice
