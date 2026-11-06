cmake_minimum_required(VERSION 3.20)

set(basename "onion_payload_catalog")
project(${basename} CXX)

onion_module_prologue(CXX_STANDARD 20)

set(D_CWD "${PROJECT_SOURCE_DIR}")

add_library(${PROJECT_NAME} STATIC
  source/payload_catalog.cpp
  source/payload_catalog_policy.cpp
  source/payload_sources_json.cpp
)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

set(CMAKE_CXX_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} ")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -fstack-protector-all -O2 -Wall -Wextra -Werror -pedantic-errors -fno-exceptions")

target_include_directories(${PROJECT_NAME}
  PUBLIC "${D_CWD}/include"
  PUBLIC "${ONIONHEN_THIRD_PARTY_DIR}/cjson"
)
