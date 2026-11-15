cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# VitaBench - standalone Vita performance-budget benchmark.
#
# Separate project, separate build directory, separate title ID: building or
# installing it never touches the main app (VitaDungeonTest, VDNG00001).
# Shared modules from ../src are compiled here unmodified.
# ---------------------------------------------------------------------------
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "VitaSDK toolchain file")
  elseif(EXISTS "/usr/local/vitasdk/share/vita.toolchain.cmake")
    set(CMAKE_TOOLCHAIN_FILE "/usr/local/vitasdk/share/vita.toolchain.cmake" CACHE PATH "VitaSDK toolchain file")
  else()
    message(FATAL_ERROR "VitaSDK not found. Set VITASDK or pass -DCMAKE_TOOLCHAIN_FILE.")
  endif()
endif()

# CXX only for linking (libvitaGL.a contains C++ objects).
project(VitaBench C CXX)

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Vita Bench")
set(VITA_TITLEID  "VDNG00002")
set(VITA_VERSION  "01.00")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

find_package(SDL2 REQUIRED CONFIG)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

add_executable(${PROJECT_NAME}
  src/bench_main.c
  src/bench_tests.c
  src/bench_report.c
  # shared modules (unmodified)
  ${PROJECT_ROOT}/src/graphics/hud2d.c
  ${PROJECT_ROOT}/src/graphics/texgen.c
  ${PROJECT_ROOT}/src/input/input.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${PROJECT_ROOT}/src
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)

set(VITA_MSAA 0 CACHE STRING "MSAA samples: 0, 2 or 4")
set_property(CACHE VITA_MSAA PROPERTY STRINGS 0 2 4)
if(NOT VITA_MSAA MATCHES "^(0|2|4)$")
  message(FATAL_ERROR "VITA_MSAA must be 0, 2 or 4 (got '${VITA_MSAA}')")
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE VITA_MSAA=${VITA_MSAA})

target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2-static)
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

vita_create_self(eboot.bin ${PROJECT_NAME})

# LiveArea: reuses the project's existing original placeholder art.
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME    ${VITA_APP_NAME}
  FILE    ${PROJECT_ROOT}/sce_sys/icon0.png                        sce_sys/icon0.png
  FILE    ${PROJECT_ROOT}/sce_sys/livearea/contents/bg.png         sce_sys/livearea/contents/bg.png
  FILE    ${PROJECT_ROOT}/sce_sys/livearea/contents/startup.png    sce_sys/livearea/contents/startup.png
  FILE    ${PROJECT_ROOT}/sce_sys/livearea/contents/template.xml   sce_sys/livearea/contents/template.xml
)
