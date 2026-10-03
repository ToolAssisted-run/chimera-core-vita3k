# The guest half: Vita3K's libraries plus the core (driver/: the boot code
# upstream's program has, the machine's filesystem, the GPU bridge's guest
# half and the exports), as archives build-core.sh links into core.wbx. Built
# by Vita3K's own CMake with CHIMERA_HEADLESS (patch 0001), in place of the
# Vita3K program; CMAKE_SOURCE_DIR is extern/vita3k.
set(W ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(GEN ${W}/../build/generated-gl)
set(MB "$ENV{HOME}/chimera/extern/chimera-common-minibox" CACHE PATH "miniBox checkout")

add_library(vita3k-guest-core STATIC
	${CMAKE_SOURCE_DIR}/vita3k/interface.cpp
	${CMAKE_SOURCE_DIR}/vita3k/performance.cpp
	${W}/driver/vita3k_driver.cpp
	${W}/driver/bridge_frame.cpp
	${W}/driver/wbx-entry.cpp
	${W}/driver/memfs.cpp
	${W}/driver/savedata.cpp
	${W}/driver/guest-libc.cpp
	${W}/../build/generated-assets.cpp
	${GEN}/gl-bridge-guest.cpp
)
target_include_directories(vita3k-guest-core PRIVATE ${CMAKE_SOURCE_DIR}/vita3k ${W}/driver
	${MB}/source/gl ${GEN} ${MB}/extern/emulibc ${MB}/source/guest/include)
# the generated wrappers want glad 2's types: built apart from Vita3K's glad
set_source_files_properties(${GEN}/gl-bridge-guest.cpp PROPERTIES INCLUDE_DIRECTORIES "${W}/glad/include;${GEN};${MB}/source/gl")
target_link_libraries(vita3k-guest-core PUBLIC
	app audio compat config cppcommon ctrl dialog display ime lang gdbstub gxm io miniz
	modules motion packages patch renderer shader touch util psvpfsparser pugixml::pugixml)
