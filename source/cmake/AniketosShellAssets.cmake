# ShellUI: our sources and embedded assets, included by shellui/CMakeLists.txt
# in its scope (D_CWD, D_SRC, I18N_GENERATED_DIR, Python3).

# Our code. Any depth below these folders is picked up: a new file needs no edit here.
file(GLOB_RECURSE AniketosSrc CONFIGURE_DEPENDS
	${D_SRC}/aniketos_shell/*.c ${D_SRC}/aniketos_shell/*.cpp
	${D_SRC}/aniketos_theme/*.c ${D_SRC}/aniketos_theme/*.cpp
	${D_SRC}/aniketos_ui/*.c ${D_SRC}/aniketos_ui/*.cpp)

set(ANIKETOS_ICON_PNG "${D_CWD}/../bootstrapper/assets/aniketoshen_sicon.png")
set(HOME_MENU_ASSETS "${D_CWD}/assets/home_menu")
set(PARTICLE_ASSETS "${D_CWD}/assets/particles")
set(HOME_BACKGROUND_ASSETS "${D_CWD}/assets/home_background")
set(ANIKETOS_ICON_RGBA "${I18N_GENERATED_DIR}/aniketos_icon.rgba")
add_custom_command(
	OUTPUT "${ANIKETOS_ICON_RGBA}"
	COMMAND "${Python3_EXECUTABLE}" "${D_CWD}/assets/png_to_rgba.py"
		"${ANIKETOS_ICON_PNG}" "${ANIKETOS_ICON_RGBA}"
	DEPENDS "${D_CWD}/assets/png_to_rgba.py" "${ANIKETOS_ICON_PNG}"
	COMMENT "Decoding the logo for its colour setting"
	VERBATIM
)
set(EMBEDDED_ASSETS_ASSEMBLY "${I18N_GENERATED_DIR}/embedded_assets.s")
configure_file("${D_SRC}/aniketos_shell/core/embedded_assets.s.in" "${EMBEDDED_ASSETS_ASSEMBLY}" @ONLY)
set(EMBEDDED_ASSET_FILES
	"${ANIKETOS_ICON_RGBA}"
	"${ANIKETOS_ICON_PNG}"
	"${HOME_MENU_ASSETS}/internet_on.png"
	"${HOME_MENU_ASSETS}/internet_off.png"
	"${HOME_MENU_ASSETS}/color_track.png"
	"${HOME_MENU_ASSETS}/color_track_gray.png"
	"${HOME_MENU_ASSETS}/color_track_solid.png"
	"${HOME_MENU_ASSETS}/color_track_fade.png"
	"${PARTICLE_ASSETS}/triangle.png"
	"${PARTICLE_ASSETS}/circle.png"
	"${PARTICLE_ASSETS}/cross.png"
	"${PARTICLE_ASSETS}/square.png"
	"${PARTICLE_ASSETS}/dot.png"
	"${HOME_BACKGROUND_ASSETS}/vignette.png"
	"${HOME_BACKGROUND_ASSETS}/edges.png"
)
set_source_files_properties("${EMBEDDED_ASSETS_ASSEMBLY}" PROPERTIES
	OBJECT_DEPENDS "${EMBEDDED_ASSET_FILES}")

set_source_files_properties("${D_SRC}/aniketos_shell/system_ui/avatar_codec.c" PROPERTIES
	COMPILE_OPTIONS "-w;-std=gnu17"
	C_STANDARD 17
	C_STANDARD_REQUIRED ON)
