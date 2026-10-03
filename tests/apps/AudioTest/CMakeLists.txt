# AudioTest: two ports at once, each a known tone at a known volume: the
# main port (48 kHz stereo) a 1000 Hz sine of 8000 on the left only, the BGM
# port (24 kHz mono) a 250 Hz sine of 4000 at full volume left and half
# right. CC0.
cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
endif()
project(audiotest)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Audio Test")
set(VITA_TITLEID  "CHMR00003")
set(VITA_VERSION  "01.00")

add_executable(${PROJECT_NAME} src/main.c)
target_link_libraries(${PROJECT_NAME}
  SceAudio_stub m
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
