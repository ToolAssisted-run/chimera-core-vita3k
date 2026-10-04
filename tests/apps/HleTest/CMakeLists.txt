# HleTest: what a system call costs the machine (10000 sceKernelGetThreadId
# timed by the machine's own clock), and how late a sleeping thread wakes
# while the main thread keeps the CPU busy in its own code. Results in
# ux0:data/hletest/result.txt. CC0.
cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
endif()
project(hletest)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "HLE Test")
set(VITA_TITLEID  "CHMR00004")
set(VITA_VERSION  "01.00")

add_executable(${PROJECT_NAME} src/main.c)
vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
