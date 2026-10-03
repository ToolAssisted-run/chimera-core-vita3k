# InputTest: what the machine reads of the frame's input, written down - the
# pad (both kinds of read), both touch panels and the motion sensors, once a
# frame into ux0:data/inputtest/input.txt - and a save that counts the runs
# (savedata0:count.bin). CC0.
cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
endif()
project(inputtest)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Input Test")
set(VITA_TITLEID  "CHMR00002")
set(VITA_VERSION  "01.00")

add_executable(${PROJECT_NAME} src/main.c)
target_link_libraries(${PROJECT_NAME}
  SceDisplay_stub SceCtrl_stub SceTouch_stub SceMotion_stub SceAppUtil_stub m
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
