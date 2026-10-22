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

# CC_NET: the co-op app (Wi-Fi play), installed next to the main one with its own title ID
option(CC_NET "Wi-Fi co-op build (its own app, CCRV00003)" OFF)
if(CC_NET)
set(VITA_APP_NAME "Castle Crashers Co-op")
set(VITA_TITLEID  "CCRV00003")
else()
set(VITA_APP_NAME "Castle Crashers")
set(VITA_TITLEID  "CCRV00001")
endif()
set(VITA_VERSION  "00.02")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -std=gnu11")

include_directories(third_party/libtess2/Include)

set(APP_SOURCES src/main.c src/as_core.c src/as_mc.c src/as_lib.c src/as_render.c src/cc_host.c src/cc_native.c src/hostfont.c src/audio.c src/strtab.c src/cc_buttons.c src/pause_menu.c src/help_book.c)

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
  SceTouch_stub
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

if(CC_NET)
target_sources(${PROJECT_NAME} PRIVATE src/net.c src/netplay.c src/upnp.c)
target_compile_definitions(${PROJECT_NAME} PRIVATE CC_NET)
target_link_libraries(${PROJECT_NAME} SceNet_stub SceNetCtl_stub SceAppUtil_stub pthread)
endif()

# Vita trophies (need the NoTrpDrm plugin): an unsigned trophy pack and its NP communication id.
# TROPHY_ICONS=dir: your own ICON0.PNG / TROPnnn.PNG instead of the drawn ones.
# On hold (2026-10-20): every build with the trophy code, even without the pack, failed to start
# on Jah's Vita ("Could not prepare the application to start") with NoTrpDrm installed.
option(CC_TROPHIES "Vita trophies through NoTrpDrm (experimental)" OFF)
set(TROPHY_ICONS "" CACHE PATH "folder with trophy icons")
if(CC_TROPHIES)
target_sources(${PROJECT_NAME} PRIVATE src/trophies.c)
target_compile_definitions(${PROJECT_NAME} PRIVATE CC_TROPHIES)
target_link_libraries(${PROJECT_NAME} SceNpTrophy_stub_weak)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/TROPHY.TRP
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_trp.py ${CMAKE_CURRENT_BINARY_DIR}/TROPHY.TRP ${TROPHY_ICONS}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_trp.py)
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -s NP_COMMUNICATION_ID=CCRV00001_00")
set(TROPHY_FILES FILE ${CMAKE_CURRENT_BINARY_DIR}/TROPHY.TRP sce_sys/trophy/CCRV00001_00/TROPHY.TRP)
endif()

# LiveArea (bubble icon, background, tap-to-start logo): LIVEAREA_DIR=dir made by
# tools/livearea/make_livearea.sh from your own game files (never committed). Without it the VPK
# gets VitaSDK's default bubble.
set(LIVEAREA_DIR "" CACHE PATH "folder with sce_sys/ LiveArea art")
if(LIVEAREA_DIR)
set(LIVEAREA_FILES
  FILE ${LIVEAREA_DIR}/sce_sys/icon0.png sce_sys/icon0.png
  FILE ${LIVEAREA_DIR}/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE ${LIVEAREA_DIR}/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml)
endif()

# Wi-Fi hosting binds ports, which a "safe" homebrew may not (sceNetBind gives EACCES)
if(CC_NET)
vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)
else()
vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})
endif()
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  ${TROPHY_FILES}
  ${LIVEAREA_FILES}
)
