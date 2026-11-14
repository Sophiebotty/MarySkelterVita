cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# Toolchain: use the VitaSDK toolchain file. If VITASDK is not set in the
# environment, fall back to the verified install location /usr/local/vitasdk.
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

# CXX is enabled only for linking: libvitaGL.a contains C++ objects
# (preprocessor.o, expression.o) that need libstdc++/libsupc++.
project(VitaDungeonTest C CXX)

# vita.cmake provides vita_create_self() and vita_create_vpk().
# ${VITASDK} is set by vita.toolchain.cmake.
include("${VITASDK}/share/vita.cmake" REQUIRED)

# ---------------------------------------------------------------------------
# Application metadata (placeholders - change before any public release).
# TITLEID must be 9 characters: 4 uppercase letters + 5 digits.
# ---------------------------------------------------------------------------
set(VITA_APP_NAME "Vita Dungeon Test")
set(VITA_TITLEID  "VDNG00001")
set(VITA_VERSION  "01.00")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)   # gnu99

# SDL2 is installed with a CMake package config in
# ${VITASDK}/arm-vita-eabi/lib/cmake/SDL2. SDL2::SDL2-static already carries
# vitaGL, vitashark, mathneon and the required Sce*_stub libraries in its
# INTERFACE_LINK_LIBRARIES, so they are not repeated here.
find_package(SDL2 REQUIRED CONFIG)

add_executable(${PROJECT_NAME}
  src/main.c
  src/engine/timing.c
  src/gameplay/collision.c
  src/gameplay/player.c
  src/gameplay/room_map.c
  src/graphics/camera.c
  src/graphics/hud2d.c
  src/graphics/level_mesh.c
  src/graphics/texgen.c
  src/input/input.c
)

# Module headers are included as "engine/...", "graphics/...", etc.
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)

# MSAA sample count, read by SDL when it initialises vitaGL.
# 0 = off, 2 = 2x, 4 = 4x (other values are ignored by SDL's Vita backend).
set(VITA_MSAA 0 CACHE STRING "MSAA samples: 0, 2 or 4")
set_property(CACHE VITA_MSAA PROPERTY STRINGS 0 2 4)
if(NOT VITA_MSAA MATCHES "^(0|2|4)$")
  message(FATAL_ERROR "VITA_MSAA must be 0, 2 or 4 (got '${VITA_MSAA}')")
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE VITA_MSAA=${VITA_MSAA})

target_link_libraries(${PROJECT_NAME} PRIVATE
  SDL2::SDL2-static
)

# Link with arm-vita-eabi-g++ so the C++ runtime (needed by vitaGL) is
# appended after all other libraries.
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

# ELF -> SELF (eboot.bin)
vita_create_self(eboot.bin ${PROJECT_NAME})

# SELF -> VPK, with LiveArea assets from sce_sys/
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME    ${VITA_APP_NAME}
  FILE    sce_sys/icon0.png                        sce_sys/icon0.png
  FILE    sce_sys/livearea/contents/bg.png         sce_sys/livearea/contents/bg.png
  FILE    sce_sys/livearea/contents/startup.png    sce_sys/livearea/contents/startup.png
  FILE    sce_sys/livearea/contents/template.xml   sce_sys/livearea/contents/template.xml
)
