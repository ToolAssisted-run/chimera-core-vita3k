# DialogTest: the system's message dialog over a plain picture. The dialog is
# not drawn by the program: the Vita draws it, and in an emulator the emulator
# does, with a renderer of its own for each graphics API. CC0.
cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
endif()
project(dialogtest)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Dialog Test")
set(VITA_TITLEID  "CHMR00006")
set(VITA_VERSION  "01.00")

add_executable(${PROJECT_NAME} src/main.c)
target_link_libraries(${PROJECT_NAME}
  vita2d
  SceGxm_stub SceDisplay_stub SceCtrl_stub SceSysmodule_stub SceCommonDialog_stub
  SceAppUtil_stub SceAppMgr_stub png z m
)
vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
