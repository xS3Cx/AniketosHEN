cmake_minimum_required (VERSION 3.20)

# Keyboard & mouse agent: injected into a running game by the daemon (which
# embeds bin/kbm_agent.elf), reads the mouse and keyboard there and lays the
# engine's controller state over the game's scePadRead* calls.
set(basename "kbm_agent")
project(${basename} C CXX ASM)

onion_module_prologue(C_STANDARD 11 CXX_STANDARD 17)

set(D_CWD "${CMAKE_CURRENT_SOURCE_DIR}")
onion_output_dirs()
set(D_SRC ${D_CWD}/source)
set(KBM_DIR "${PROJECT_ROOT}/libonion_kbm")

add_executable(${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}.elf")
target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--gc-sections" "-Wl,--icf=safe")

file(GLOB SrcFiles CONFIGURE_DEPENDS ${D_SRC}/*.c ${D_SRC}/*.cpp)
# Only the runtime engine: the profile model, JSON and store stay in the daemon.
set(KbmEngineSrc "${KBM_DIR}/source/kbm_engine.cpp" "${KBM_DIR}/source/kbm_analog.cpp")

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-keyword-macro -fPIC -fPIE -march=znver2 -Wall -Werror -fstack-protector-all -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-exceptions -fno-rtti -gfull -gdwarf-2")
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")

target_sources(${PROJECT_NAME} PRIVATE ${SrcFiles} ${KbmEngineSrc})
onion_target_debug_define(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE
	"${D_SRC}"
	"${KBM_DIR}/include"
	"${KBM_DIR}/source"
	"${PROJECT_ROOT}/include"
)
target_link_directories	(${PROJECT_NAME} PUBLIC "${ONIONHEN_PS5_STUB_DIR}")
target_link_libraries	(${PROJECT_NAME} PUBLIC kernel_sys pthread)

onion_strip_debug(${PROJECT_NAME})
