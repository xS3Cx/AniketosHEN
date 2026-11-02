cmake_minimum_required (VERSION 3.20)

project("AniketosHEN")

set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(OnionTarget)
get_filename_component(ONIONHEN_ROOT "${PROJECT_ROOT}/.." ABSOLUTE)
set(ONIONHEN_THIRD_PARTY_DIR "${ONIONHEN_ROOT}/third_party" CACHE PATH
	"Third-party dependency source directory")
set(ONIONHEN_COMMON_DIR "${PROJECT_ROOT}/common")
set(ONIONHEN_PS5_STUB_DIR "${PROJECT_ROOT}/platform/ps5/stubs")
set(ONIONHEN_KSTUFF_ELF "${ONIONHEN_ROOT}/.cache/dependencies/kstuff.elf"
	CACHE FILEPATH "Path to the kstuff ELF embedded by the bootstrapper")
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
set(ANIKETOSHEN_VERSION "0.04j" CACHE STRING
	"Release version shown to users (empty: derive from git)")
set(ONIONHEN_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/include")
set(ONIONHEN_VERSION_HEADER
	"${ONIONHEN_GENERATED_INCLUDE_DIR}/onion/version.h")

# Re-evaluate Git state on every build. The script preserves the header mtime
# when the version is unchanged, so incremental compilation remains cheap.
add_custom_target(onion_version
	COMMAND "${CMAKE_COMMAND}"
		"-DREPO_ROOT=${ONIONHEN_ROOT}"
		"-DOUTPUT_FILE=${ONIONHEN_VERSION_HEADER}"
		"-DVERSION_OVERRIDE=${ANIKETOSHEN_VERSION}"
		-P "${PROJECT_ROOT}/cmake/GenerateVersionHeader.cmake"
	BYPRODUCTS "${ONIONHEN_VERSION_HEADER}"
	COMMENT "Generating AniketosHEN build version"
	VERBATIM
)

# ---------------------------------------------------------------------------
# Artifact layout
#   ${CMAKE_BINARY_DIR}/bin  — ELFs (daemon, util, bootstrapper, AniketosHEN, …)
#   ${CMAKE_BINARY_DIR}/lib  — built static libs (libonion_*, …)
# PS5 system-library stubs stay under source/platform/ps5/stubs.
#
# Default scripts/build.sh uses -B <repo>/build so products land at repo/build/.
# ---------------------------------------------------------------------------
set(ONIONHEN_OUT_BIN "${CMAKE_BINARY_DIR}/bin" CACHE PATH "ELF output directory")
set(ONIONHEN_OUT_LIB "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Built static-lib output directory")

file(MAKE_DIRECTORY "${ONIONHEN_OUT_BIN}" "${ONIONHEN_OUT_LIB}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${ONIONHEN_OUT_BIN})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${ONIONHEN_OUT_LIB})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${ONIONHEN_OUT_LIB})

include_directories(BEFORE "${ONIONHEN_GENERATED_INCLUDE_DIR}")
# First-party shared contracts — not SYSTEM, so warnings in them are visible.
include_directories("${PROJECT_ROOT}/include")
include_directories(SYSTEM "${PS5_PAYLOAD_SDK}")
include_directories(SYSTEM "${PS5_PAYLOAD_SDK}/include")

# Built libraries first, then PS5 system stubs.
link_directories("${ONIONHEN_OUT_LIB}" "${ONIONHEN_PS5_STUB_DIR}")

add_subdirectory(libhijacker)
add_subdirectory(libNidResolver)
# add_subdirectory(libRPI)
# libSelfDecryptor removed from AniketosHEN.
# Shared proc/ucred before NineS (inject path links these symbols).
add_subdirectory(libonion_proc)
add_subdirectory(libonion_fps)
add_subdirectory(libonion_platform)
add_subdirectory(libonion_kbm)
add_subdirectory(libonion_payload)
add_subdirectory(libonion_conflict)
add_subdirectory(libonion_ipc)
add_subdirectory(libonion_settings)
add_subdirectory(libonion_detour)
add_subdirectory(libonion_ready)
add_subdirectory(libonion_lifecycle)
add_subdirectory(libonion_plugin_ui)
add_subdirectory(libonion_plugin_bridge)
add_subdirectory(libonion_plugin_session)
add_subdirectory(libonion_plugin_manager)
# Shared ptrace + ELF inject (authid elevated once at inject entry, not per syscall).
add_subdirectory(libonion_elfldr)
add_subdirectory(libonion_sprx)
add_subdirectory(onion_elfldr_server)
add_subdirectory(libNineS)
add_subdirectory(shellui)
add_subdirectory(util)
add_subdirectory(kbm_agent)
add_subdirectory(daemon)
add_subdirectory(bootstrapper)
add_subdirectory(unpacker)
