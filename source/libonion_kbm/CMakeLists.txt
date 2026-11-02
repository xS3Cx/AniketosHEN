###################################################################################################
# AniketosHEN keyboard & mouse for games
#   onion_kbm_profile  editable profile, names, JSON store, compiler (shellui, daemon)
# The engine (kbm_engine, kbm_analog) runs inside the game, no heap, no JSON: kbm_agent
# compiles those two sources itself.
###################################################################################################

cmake_minimum_required(VERSION 3.20)

set(basename "onion_kbm")
project(${basename} C CXX)

onion_module_prologue(C_STANDARD 11 CXX_STANDARD 20)

set(D_CWD "${PROJECT_SOURCE_DIR}")

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -fstack-protector-all")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wextra -Werror -fno-exceptions")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g0 -O2")

add_library(onion_kbm_profile STATIC
  source/kbm_profile.cpp
  source/kbm_names.cpp
  source/kbm_store.cpp
)
set_target_properties(onion_kbm_profile PROPERTIES OUTPUT_NAME "onion_kbm_profile")
target_include_directories(onion_kbm_profile
  PUBLIC  "${D_CWD}/include"
  PUBLIC  "${D_CWD}/../include"
  PRIVATE "${D_CWD}/../libonion_platform/include"
  PRIVATE "${ONIONHEN_THIRD_PARTY_DIR}/cjson"
)
add_dependencies(onion_kbm_profile onion_platform)

# cJSON and onion_platform symbols come from the final link unit (shellui and
# the daemon compile third_party/cjson into their ELF), as in libonion_ipc.
