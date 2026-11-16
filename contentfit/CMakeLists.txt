cmake_minimum_required(VERSION 3.18)   # find_library/find_path ... REQUIRED needs 3.18

# ---------------------------------------------------------------------------
# Vita ContentFit - Stage 4 content-fit benchmark.
#
# Separate project, separate build directory (build-contentfit/), separate
# title ID VDNG00005. Reused modules are compiled UNMODIFIED from their own
# locations: Phase 8 (resproto/src), the GDAT Proto reader (gdatproto/src:
# gp_common, gp_gdat, gp_wrap) and the shared UI/input modules (src/).
#
# Everything it draws, reads or allocates is synthetic. It reads only its own
# fixtures (tools/make_contentfit_fixtures.py) and makes no claim about
# Mary Skelter Finale.
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

project(VitaContentFit C CXX)   # CXX only for linking libvitaGL's C++ objects

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Vita ContentFit")
set(VITA_TITLEID  "VDNG00005")
set(VITA_VERSION  "01.00")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

find_package(SDL2 REQUIRED CONFIG)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(RP_SRC "${PROJECT_ROOT}/resproto/src")
set(GP_SRC "${PROJECT_ROOT}/gdatproto/src")
set(CF_FIX "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

find_library(ZLIB_LIB z REQUIRED)
find_path(ZLIB_INC zlib.h REQUIRED)

if(NOT EXISTS "${CF_FIX}/cf_fixtures.h")
  message(FATAL_ERROR "fixtures missing: run python3 -I tools/make_contentfit_fixtures.py (build_contentfit.sh does this)")
endif()

# Package only these synthetic fixtures. cf_fixtures.h is compiled in;
# expected.json is for the PC checker only.
set(CF_REQUIRED_FIXTURES
  io16.bin
  pack08.gdat
  pack16.gdat
  pack32.gdat
  pack64.gdat
)
set(CF_FIXTURE_VPK_ARGS)
foreach(fixture IN LISTS CF_REQUIRED_FIXTURES)
  set(fixture_source "${CF_FIX}/${fixture}")
  if(NOT EXISTS "${fixture_source}" OR IS_DIRECTORY "${fixture_source}")
    message(FATAL_ERROR "Required synthetic ContentFit fixture is missing or not a file: ${fixture_source}")
  endif()
  list(APPEND CF_FIXTURE_VPK_ARGS FILE "${fixture_source}" "fixtures/${fixture}")
endforeach()

add_executable(${PROJECT_NAME}
  src/cf_main.c
  src/cf_common.c
  src/cf_fixinfo.c
  src/cf_stats.c
  src/cf_budget.c
  src/cf_paths.c
  src/cf_zinflate.c
  src/cf_plat_vita.c
  src/cf_iowrap.c
  src/cf_report.c
  src/cf_geom.c
  src/cf_scene.c
  src/cf_pack.c
  src/cf_suite.c
  src/cf_scenes.c
  src/cf_io.c
  src/cf_load.c
  src/cf_memtest.c
  # GDAT Proto reader (unmodified)
  ${GP_SRC}/gp_common.c
  ${GP_SRC}/gp_gdat.c
  ${GP_SRC}/gp_wrap.c
  # Phase 8 modules (unmodified)
  ${RP_SRC}/rp_io.c
  ${RP_SRC}/rp_mem.c
  ${RP_SRC}/rp_inflate.c
  ${RP_SRC}/rp_time.c
  # shared modules (unmodified)
  ${PROJECT_ROOT}/src/graphics/hud2d.c
  ${PROJECT_ROOT}/src/input/input.c
)

# Generated content must be bit-identical on the PC and the Vita: no fused
# or contracted floating-point operations in the generator.
set_source_files_properties(src/cf_geom.c PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CF_FIX}
  ${GP_SRC}
  ${RP_SRC}
  ${PROJECT_ROOT}/src
  ${ZLIB_INC}
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)

# Keep vitaGL's shader cache inside ux0:data/ContentFit/. The installed vitaGL
# uses fixed paths below ux0:data/shader_cache (vglSetShaderCachePath is an
# empty function), so its sceIoOpen/sceIoMkdir calls are routed through
# src/cf_iowrap.c, which rewrites only that prefix. vitaGL is not modified.
target_link_options(${PROJECT_NAME} PRIVATE "LINKER:--wrap=sceIoOpen" "LINKER:--wrap=sceIoMkdir")

target_link_libraries(${PROJECT_NAME} PRIVATE
  SDL2::SDL2-static
  ${ZLIB_LIB}
  SceSysmem_stub            # free memory, memory-block size of the heap
  SceKernelThreadMgr_stub   # sceKernelPollSema, sceKernelDelayThread, system time
  SceLibKernel_stub         # threads, semaphores, sceIoPread, sceIoDevctl
  SceDisplay_stub           # vertical blank counter and wait
  ScePower_stub             # clock and battery queries (read only)
)
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

vita_create_self(eboot.bin ${PROJECT_NAME})

# LiveArea art reuses the project's original placeholder images.
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME    ${VITA_APP_NAME}
  FILE    ${PROJECT_ROOT}/sce_sys/icon0.png                        sce_sys/icon0.png
  FILE    ${PROJECT_ROOT}/sce_sys/livearea/contents/bg.png         sce_sys/livearea/contents/bg.png
  FILE    ${PROJECT_ROOT}/sce_sys/livearea/contents/startup.png    sce_sys/livearea/contents/startup.png
  FILE    ${PROJECT_ROOT}/sce_sys/livearea/contents/template.xml   sce_sys/livearea/contents/template.xml
  ${CF_FIXTURE_VPK_ARGS}
)
