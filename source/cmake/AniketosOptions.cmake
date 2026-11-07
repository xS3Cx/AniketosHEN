# AniketosHEN build options, included by source/CMakeLists.txt.

set(ONIONHEN_SHADOWMOUNT_ELF "${ONIONHEN_ROOT}/.cache/dependencies/shadowmountplus.elf"
	CACHE FILEPATH "Path to the ShadowMountPlus ELF embedded by the bootstrapper")

# Bug-report address shown in crash notifications (see <onion/distribution.h>).
# Left empty, the notification names the tracker without a link.
set(ANIKETOSHEN_ISSUES_URL "" CACHE STRING
	"Issue tracker URL shown in AniketosHEN crash notifications")
if(ANIKETOSHEN_ISSUES_URL)
	if(NOT ANIKETOSHEN_ISSUES_URL MATCHES "^https://[A-Za-z0-9._~:/?#@!$&'()*+,;=-]+$")
		message(FATAL_ERROR
			"ANIKETOSHEN_ISSUES_URL must be an https:// URL: ${ANIKETOSHEN_ISSUES_URL}")
	endif()
	add_compile_definitions(ONION_DIST_ISSUES_URL="${ANIKETOSHEN_ISSUES_URL}")
endif()
# Where util sends error reports (<onion/report.h>): the maintainers' relay,
# which forwards them to Discord. Empty (the default) builds without sending;
# reports then stay on the console. Never put a bot token here: the relay
# holds it, the payload only knows this URL.
set(ANIKETOSHEN_REPORT_URL "" CACHE STRING
	"http(s) URL of the error report relay (empty: do not send)")
if(ANIKETOSHEN_REPORT_URL)
	if(NOT ANIKETOSHEN_REPORT_URL MATCHES "^https?://[A-Za-z0-9._~:/?#@!$&'()*+,;=-]+$")
		message(FATAL_ERROR
			"ANIKETOSHEN_REPORT_URL must be an http(s):// URL: ${ANIKETOSHEN_REPORT_URL}")
	endif()
	add_compile_definitions(ONION_REPORT_URL="${ANIKETOSHEN_REPORT_URL}")
endif()
# Version shown to users (welcome notification, About page, Settings banner,
# error reports). Bump it for every build handed out. Empty falls back to the
# git tag, or dirty-<sha> for an untagged tree.
set(ANIKETOSHEN_VERSION "0.04q" CACHE STRING
	"Release version shown to users (empty: derive from git)")
