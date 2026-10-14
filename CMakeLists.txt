cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(cc_vita C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "CC Vita Renderer Test")
set(VITA_TITLEID  "CCRV00001")
set(VITA_VERSION  "00.02")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -std=gnu11")

include_directories(third_party/libtess2/Include)

# Default: the game's own scripts on the ActionScript engine.
# -DCC_VIEWER=ON: the earlier level/animation viewer with hand-written gameplay.
option(CC_VIEWER "build the level and animation viewer" OFF)
if(CC_VIEWER)
  set(APP_SOURCES src/viewer.c src/level.c src/player.c src/enemy.c)
else()
  set(APP_SOURCES src/main.c src/as_core.c src/as_mc.c src/as_lib.c src/as_render.c src/cc_host.c src/cc_native.c src/hostfont.c src/audio.c)
endif()

add_executable(${PROJECT_NAME}
  ${APP_SOURCES}
  src/swf.c
  src/render.c
  third_party/libtess2/Source/bucketalloc.c
  third_party/libtess2/Source/dict.c
  third_party/libtess2/Source/geom.c
  third_party/libtess2/Source/mesh.c
  third_party/libtess2/Source/priorityq.c
  third_party/libtess2/Source/sweep.c
  third_party/libtess2/Source/tess.c
)

# see sce_gap.ld (needs CMake 3.13+ for target_link_options)
target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/sce_gap.ld")

target_link_libraries(${PROJECT_NAME}
  vita2d
  mpg123
  SceAudio_stub
  SceDisplay_stub
  SceGxm_stub
  SceSysmodule_stub
  SceCtrl_stub
  ScePgf_stub
  ScePvf_stub
  SceCommonDialog_stub
  SceAppMgr_stub
  ScePower_stub
  freetype
  png
  jpeg
  z
  m
  c
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
)
